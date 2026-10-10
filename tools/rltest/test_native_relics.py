"""Verify battle-affecting relic hooks in real, CPU-vs-CPU native battles.

The harness only writes the documented custom-battle input globals and request
flag. It never edits live battle state, result state, or RNG state.
"""

import argparse
import ctypes
import json
import os
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.rltest.frontend import Frontend, HarnessError
from tools.rltest.test_custom_battle import Mon, Result


TRACE_CAPACITY = 256
STAT_HP, STAT_ATTACK, STAT_DEFENSE, STAT_SPEED, STAT_SPECIAL = range(5)
RELIC = {
    "boulder": 0,
    "cascade": 1,
    "thunder": 2,
    "soul": 4,
    "marsh": 5,
    "volcano": 6,
    "leftovers": 11,
    "focus": 12,
    "scope-lens": 13,
    "pp-pouch": 15,
    "ghost": 18,
    "cursed-power": 19,
}


class BattleRules(ctypes.BigEndianStructure):
    _fields_ = [
        ("stadium", ctypes.c_uint8),
        ("gym", ctypes.c_uint8),
        ("ai_level", ctypes.c_uint8),
        ("player_count", ctypes.c_uint8),
        ("player_cpu", ctypes.c_uint8),
        ("show_intro", ctypes.c_uint8),
        ("use_state", ctypes.c_uint8),
        ("pad", ctypes.c_uint8),
        ("relics", ctypes.c_uint32),
    ]


class TraceEvent(ctypes.BigEndianStructure):
    _fields_ = [
        ("kind", ctypes.c_uint32),
        ("turn", ctypes.c_uint32),
        ("a", ctypes.c_uint32),
        ("b", ctypes.c_uint32),
        ("c", ctypes.c_uint32),
        ("d", ctypes.c_uint32),
        ("e", ctypes.c_uint32),
        ("f", ctypes.c_uint32),
    ]


class RelicTrace(ctypes.BigEndianStructure):
    _fields_ = [
        ("count", ctypes.c_uint32),
        ("dropped", ctypes.c_uint32),
        ("events", TraceEvent * TRACE_CAPACITY),
    ]


def _mon(species, level, moves, *, hp=0xFFFF, max_pp=None, pp=63, status=0):
    mon = Mon()
    mon.species = species
    mon.level = level
    mon.dvs = 0xFFFF
    mon.cur_hp = hp
    mon.status = status
    for index, move in enumerate(moves):
        mon.moves[index] = move
        mon.pp[index] = pp
        if max_pp is not None:
            mon.max_pp[index] = max_pp
    return mon


def _scenario(name):
    """Return relic mask and one-mon teams for a focused native battle."""
    if name in ("boulder", "thunder", "marsh"):
        return {
            "mask": 1 << RELIC[name],
            "player": _mon(150, 50, [33]),
            "enemy": _mon(10, 50, [33]),
            "check": {"kind": "stat", "stat": {
                "boulder": STAT_DEFENSE,
                "thunder": STAT_SPEED,
                "marsh": STAT_SPECIAL,
            }[name]},
        }
    damage = {
        "cascade": (1 << RELIC["cascade"], 9, 57, 0x15, 115),
        "volcano": (1 << RELIC["volcano"], 6, 53, 0x14, 115),
        "ghost": (1 << RELIC["ghost"], 94, 122, 8, 115),
        "cursed-power": (1 << RELIC["cursed-power"], 150, 1, 0, 110),
        "cursed-multihit": (1 << RELIC["cursed-power"], 15, 41, 7, 110),
        "cursed-fixed": (1 << RELIC["cursed-power"], 150, 49, 0, 110),
    }
    if name in damage:
        mask, species, move, native_type, percent = damage[name]
        check = {"kind": "damage", "native_type": native_type, "percent": percent}
        if name == "cursed-multihit":
            check["min_hits_per_turn"] = 2
        elif name == "cursed-fixed":
            check["input_damage"] = 20
        return {
            "mask": mask,
            "player": _mon(species, 50, [move]),
            "enemy": _mon(113, 50, [33]) if name in ("cursed-multihit", "cursed-fixed")
            else _mon(10, 50, [33]),
            "check": check,
        }
    if name == "scope-lens":
        return {
            "mask": 1 << RELIC[name],
            "player": _mon(53, 50, [163]),
            "enemy": _mon(10, 50, [33]),
            "check": {"kind": "scope"},
        }
    if name == "focus":
        return {
            "mask": 1 << RELIC[name],
            "player": _mon(1, 5, [1], hp=1),
            "enemy": _mon(150, 100, [33]),
            "check": {"kind": "focus"},
        }
    if name == "leftovers":
        return {
            "mask": 1 << RELIC[name],
            "player": _mon(150, 50, [33], hp=90, max_pp=35, pp=35),
            "enemy": _mon(113, 50, [33], max_pp=35, pp=35),
            "check": {"kind": "leftovers"},
        }
    if name == "soul":
        return {
            "mask": 1 << RELIC[name],
            # Chansey knows only Toxic, so later player HP loss can only be
            # native poison after Soul expires. Duplicate moves stall the engine.
            "player": _mon(150, 100, [33]),
            "enemy": _mon(113, 100, [92]),
            "check": {"kind": "soul"},
        }
    if name == "pp-pouch":
        return {
            "mask": 1 << RELIC[name],
            "player": _mon(150, 100, [94], max_pp=10, pp=3),
            "enemy": _mon(10, 5, [33]),
            "check": {"kind": "pp-pouch", "max_pp": 12, "repeat": True},
        }
    if name == "pp-pouch-capped":
        return {
            "mask": 1 << RELIC["pp-pouch"],
            "player": _mon(150, 100, [94], max_pp=16, pp=3),
            "enemy": _mon(10, 5, [33]),
            "check": {"kind": "pp-pouch", "max_pp": 16, "repeat": False},
        }
    if name == "cursed-tick":
        return {
            "mask": 1 << RELIC["cursed-power"],
            "player": _mon(150, 50, [33], hp=1),
            "enemy": _mon(113, 50, [86]),
            "check": {"kind": "cursed-tick"},
        }
    raise HarnessError(f"Unknown native relic scenario {name!r}")


def _read_trace(emu):
    trace = RelicTrace.from_buffer_copy(emu.read("gRlRelicTrace", ctypes.sizeof(RelicTrace)))
    if trace.count > TRACE_CAPACITY:
        raise HarnessError(f"Native trace count exceeds capacity: {trace.count}")
    if trace.dropped:
        raise HarnessError(f"Native relic trace dropped {trace.dropped} event(s)")
    return [
        {"kind": event.kind, "turn": event.turn, "a": event.a, "b": event.b,
         "c": event.c, "d": event.d, "e": event.e, "f": event.f}
        for event in trace.events[:trace.count]
    ]


def _expect(condition, message):
    if not condition:
        raise HarnessError(message)


def _check_stat(events, player_species, stat):
    matches = [event for event in events
               if event["kind"] == 1 and event["a"] == stat
               and event["f"] == player_species]
    _expect(matches, f"No native stat trace for stat {stat}, species {player_species}")
    for event in matches:
        expected = min(999, (event["b"] * 110 + 50) // 100)
        _expect(event["c"] == expected,
                f"Stat relic did not apply exact rounded +10% to the native base stat "
                f"used by stage recalculation: {event}, expected {expected}")
        _expect(event["e"] == event["c"],
                f"Switch-in effective stat does not match boosted base: {event}")


def _check_damage(events, player_species, native_type, percent, *,
                  min_hits_per_turn=1, input_damage=None):
    matches = [event for event in events
               if event["kind"] == 2 and event["a"] == native_type
               and event["f"] == player_species and event["b"] > 0]
    _expect(matches, f"No committed move trace for native type {native_type:#x}")
    if input_damage is not None:
        _expect(any(event["b"] == input_damage for event in matches),
                f"Fixed-damage move did not retain its native input damage "
                f"{input_damage}: {matches}")
    for event in matches:
        expected = (event["b"] * percent + 50) // 100
        _expect(event["c"] == expected,
                f"Damage relic math/type mapping mismatch: {event}, expected {expected}")
        _expect(0 < event["e"] <= event["d"],
                f"Damage trace has invalid committed HP loss: {event}")
    if min_hits_per_turn > 1:
        counts = {}
        for event in matches:
            counts[event["turn"]] = counts.get(event["turn"], 0) + 1
        _expect(max(counts.values(), default=0) >= min_hits_per_turn,
                f"Multihit move did not produce {min_hits_per_turn} hit commits "
                f"in one turn: {matches}")


def _check_scope(events, player_species):
    matches = [event for event in events
               if event["kind"] == 6 and event["e"] == player_species and event["f"] == 163]
    _expect(matches, "Scope Lens produced no native Slash critical-threshold trace")
    for event in matches:
        expected = min(255, event["a"] * 2)
        _expect(event["b"] == expected,
                f"Scope Lens threshold was not doubled/capped: {event}, expected {expected}")
        _expect(0 <= event["c"] <= 0xFFFF and event["d"] in (0, 1),
                f"Invalid native critical draw/result: {event}")


def _check_focus(events, result):
    matches = [event for event in events if event["kind"] == 5]
    _expect(len(matches) == 1, f"Focus must save exactly once, saw {len(matches)}")
    event = matches[0]
    _expect(event["a"] > 0 and event["b"] >= event["a"] and event["c"] == 1,
            f"Focus did not prevent an actually lethal hit at 1 HP: {event}")
    _expect(event["d"] == result.player[0].species,
            f"Focus trace defender does not match persisted player: {event}")
    _expect(result.player[0].fainted and result.player[0].cur_hp == 0,
            "The subsequent native hit did not KO the Focus-protected Pokemon")


def _check_leftovers(events, result, player_species, enemy_species, initial_pp):
    turns = sorted((event for event in events if event["kind"] == 3),
                   key=lambda event: event["turn"])
    _expect(len(turns) >= 2, "Leftovers battle did not complete at least two full rounds")
    _expect([event["turn"] for event in turns] == list(range(len(turns))),
            f"Turn trace is not one event per completed consecutive round: {turns}")
    previous_hp = None
    for turn in turns:
        # Event "a" is the settled HP after both actions; "b" is after Leftovers.
        incoming = sum(event["e"] for event in events
                       if event["kind"] == 2 and event["turn"] == turn["turn"]
                       and event["f"] == enemy_species)
        if previous_hp is not None:
            _expect(turn["a"] == max(0, previous_hp - incoming),
                    f"Round {turn['turn']} start HP does not chain from the previous "
                    f"Leftovers result minus native damage: {turn}, incoming {incoming}")
        heal = max(1, turn["c"] // 16)
        expected_hp = min(turn["c"], turn["a"] + heal) if turn["a"] else 0
        _expect(turn["b"] == expected_hp,
                f"Leftovers cadence/math mismatch in round {turn['turn']}: "
                f"expected HP {expected_hp}, event {turn}")
        _expect(turn["e"] in (0, 1), f"Invalid second-action flag: {turn}")
        previous_hp = turn["b"]
    _expect(result.player[0].species == player_species
            and result.enemy[0].species == enemy_species,
            "Leftovers battle did not persist both configured native combatants")
    player_used = initial_pp - result.player[0].pp[0]
    enemy_used = initial_pp - result.enemy[0].pp[0]
    _expect(player_used >= len(turns) and enemy_used >= len(turns),
            "Native PP debits do not evidence both sides acting each completed round")


def _check_soul(events, result):
    blocked = [event for event in events if event["kind"] == 4]
    _expect(blocked, "Soul battle produced no blocked enemy status attempt")
    _expect(all(event["c"] == 92 and event["a"] != 0 for event in blocked),
            f"Soul trace was not an enemy Toxic status attempt: {blocked}")
    _expect(all(event["turn"] < 2 for event in blocked),
            f"Soul blocked a status attempt after the first two turns: {blocked}")
    rounds = [event for event in events if event["kind"] == 3]
    _expect(len(rounds) > 2, f"Soul battle ended before the immunity expired: {rounds}")
    _expect(all(event["a"] == event["c"] for event in rounds[:2]),
            f"Player lost HP while Soul blocked the Toxic-only opponent: {rounds[:2]}")
    poisoned = [event for event in rounds[2:] if event["a"] < event["c"]]
    _expect(poisoned or result.player[0].cur_hp < result.player[0].max_hp,
            f"Toxic never landed after Soul expired: {rounds}")


def _check_pp_pouch(result, expected):
    # Psychic has 10 base PP; one native PP-Up adds two and the native
    # three-level cap yields 16. The relic changes capacity, not current PP.
    actual = result.player[0].max_pp[0]
    _expect(actual == expected,
            f"PP Pouch native PP-Up capacity mismatch: expected {expected}, got {actual}")
    _expect(result.player[0].pp[0] <= actual,
            "Persisted PP exceeds the native PP-Up maximum")


def _check_cursed_tick(events, result):
    end_events = [event for event in events if event["kind"] == 7]
    _expect(len(end_events) == 1, f"Expected one persisted native end event, got {end_events}")
    _expect(result.player[0].cur_hp == 0 and result.player[0].fainted,
            "Cursed lethal turn-end damage did not enter native faint handling")
    _expect(end_events[0]["a"] == 0,
            f"Native end telemetry did not persist the cursed KO: {end_events[0]}")


def _verify(name, case, events, result, outcome):
    check = case["check"]
    if check["kind"] == "stat":
        _check_stat(events, case["player"].species, check["stat"])
    elif check["kind"] == "damage":
        _check_damage(events, case["player"].species, check["native_type"], check["percent"],
                      min_hits_per_turn=check.get("min_hits_per_turn", 1),
                      input_damage=check.get("input_damage"))
    elif check["kind"] == "scope":
        _check_scope(events, case["player"].species)
    elif check["kind"] == "focus":
        _check_focus(events, result)
    elif check["kind"] == "leftovers":
        _check_leftovers(events, result, case["player"].species,
                         case["enemy"].species, case["player"].pp[0])
    elif check["kind"] == "soul":
        _check_soul(events, result)
    elif check["kind"] == "pp-pouch":
        _check_pp_pouch(result, check["max_pp"])
    elif check["kind"] == "cursed-tick":
        _check_cursed_tick(events, result)
    _expect(outcome in (0, 1, 2), f"Invalid native battle outcome {outcome}")
    _expect(any(event["kind"] == 7 for event in events),
            "No native persisted end-of-battle trace")
    return {"scenario": name, "mask": case["mask"], "outcome": outcome,
            "player_hp": result.player[0].cur_hp, "enemy_hp": result.enemy[0].cur_hp,
            "events": events}


def run_one(name, core, rom, map_path, output, timeout):
    case = _scenario(name)
    output = Path(output) / name
    output.mkdir(parents=True, exist_ok=True)
    with Frontend(core, rom, map_path, output=output, timeout=timeout) as emu:
        emu.wait_state(2, frames=2400)
        emu.step(120)
        if emu.read_u32("gRlBattleTestMonSize") != ctypes.sizeof(Mon):
            raise HarnessError("Canonical RlMon ABI mismatch")
        party = (Mon * 6)()
        enemy = (Mon * 6)()
        party[0] = case["player"]
        enemy[0] = case["enemy"]
        rules = BattleRules()
        rules.stadium = 3
        rules.ai_level = 11
        rules.player_count = 1
        rules.player_cpu = 1
        rules.use_state = 1
        rules.relics = case["mask"]

        before = emu.read_u32("gRlBattleCount")
        emu.write("gRlRelicTestParty", bytes(party))
        emu.write("gRlRelicTestEnemy", bytes(enemy))
        emu.write("gRlRelicTestRules", bytes(rules))
        emu.write_u32("gRlRelicTestPartyCount", 1)
        emu.write_u32("gRlRelicTestEnemyCount", 1)
        emu.write_u32("gRlRelicTestMask", case["mask"])
        emu.write_u32("gRlRelicTestConsumed", 0)
        emu.write_u32("gRlBattleTestRequest", 3)
        emu.press("START", frames=10, after=10)
        live = emu.wait_battle(frames=2400)
        if case["check"]["kind"] == "pp-pouch":
            _expect(live[0]["pp"][0] == case["player"].pp[0],
                    f"PP Pouch refilled current PP before battle: {live[0]}")
        try:
            emu.wait(lambda runner: runner.read_u32("gRlBattleCount") == before + 1,
                     frames=36000, interval=10, description=f"{name} native battle completion")
        except HarnessError:
            trace = RelicTrace.from_buffer_copy(
                emu.read("gRlRelicTrace", ctypes.sizeof(RelicTrace)))
            diagnostic = {
                "frame": emu.frame,
                "state": emu.read_u32("gCurrentGameState"),
                "trace_count": trace.count,
                "events": [[event.kind, event.turn, event.a, event.b, event.c,
                            event.d, event.e, event.f]
                           for event in trace.events[:min(trace.count, TRACE_CAPACITY)]],
            }
            (output / "timeout-diagnostic.json").write_text(json.dumps(diagnostic) + "\n")
            emu.screenshot(output / "timeout.png")
            raise

        result = Result.from_buffer_copy(
            emu.read("gRlLastBattleResult", ctypes.sizeof(Result)))
        if result.player_count != 1 or result.enemy_count != 1:
            raise HarnessError(
                f"{name} did not complete with the configured 1v1 teams: "
                f"{result.player_count}v{result.enemy_count}")
        if emu.read_u32("gRlBattleActive") != 0 or emu.read_u32("gCurrentGameState") != 2:
            raise HarnessError(f"{name} battle did not return to title with inactive battle flag")
        events = _read_trace(emu)
        record = _verify(name, case, events, result, result.outcome)
        if case["check"]["kind"] == "pp-pouch" and case["check"]["repeat"]:
            first_max_pp = result.player[0].max_pp[0]
            emu.write_u32("gRlBattleTestRequest", 3)
            emu.press("START", frames=10, after=10)
            emu.wait_battle(frames=2400)
            emu.wait(lambda runner: runner.read_u32("gRlBattleCount") == before + 2,
                     frames=36000, interval=10, description="repeated PP Pouch battle completion")
            repeated = Result.from_buffer_copy(
                emu.read("gRlLastBattleResult", ctypes.sizeof(Result)))
            _expect(repeated.player_count == 1 and repeated.enemy_count == 1,
                    "Repeated PP Pouch request did not complete a native battle")
            _expect(repeated.player[0].max_pp[0] == first_max_pp == case["check"]["max_pp"],
                    f"Repeated request applied PP Pouch more than once: "
                    f"{first_max_pp} -> {repeated.player[0].max_pp[0]}")
            record["repeat_max_pp"] = repeated.player[0].max_pp[0]
        record["result"] = {
            "outcome": result.outcome,
            "player": {"species": result.player[0].species, "hp": result.player[0].cur_hp,
                       "fainted": result.player[0].fainted, "pp": list(result.player[0].pp),
                       "max_pp": list(result.player[0].max_pp)},
            "enemy": {"species": result.enemy[0].species, "hp": result.enemy[0].cur_hp,
                      "fainted": result.enemy[0].fainted, "pp": list(result.enemy[0].pp),
                      "max_pp": list(result.enemy[0].max_pp)},
        }
        (output / "native-relic.json").write_text(json.dumps(record, indent=2) + "\n")
        print(f"NATIVE RELIC PASSED {name}: outcome={result.outcome}, "
              f"events={len(events)}, frame={emu.frame}")
        return record


SCENARIOS = (
    "boulder", "thunder", "marsh", "cascade", "volcano", "ghost",
    "cursed-power", "cursed-multihit", "cursed-fixed",
    "scope-lens", "focus", "leftovers", "soul",
    "pp-pouch", "pp-pouch-capped", "cursed-tick",
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scenario", choices=("all", *SCENARIOS), default="all",
                        help="run all checks or a targeted native battle")
    core_candidates = (
        Path.home() / ".config/retroarch/cores/mupen64plus_next_libretro.so",
        Path("/usr/lib/libretro/mupen64plus_next_libretro.so"),
        Path.home() / ".config/retroarch/cores/parallel_n64_libretro.so",
        Path("/usr/lib/libretro/parallel_n64_libretro.so"),
    )
    configured_core = os.environ.get("RLTEST_CORE")
    default_core = next((path for path in
                         ([Path(configured_core)] if configured_core else []) + list(core_candidates)
                         if path.is_file()), core_candidates[0])
    parser.add_argument("--core", default=str(default_core))
    parser.add_argument("--rom", default=os.environ.get(
        "RLTEST_ROM", str(ROOT / "build_rl/pokestadium-rl.z64")))
    parser.add_argument("--map", dest="map_path", default=os.environ.get(
        "RLTEST_MAP", str(ROOT / "build_rl/pokestadium-rl.map")))
    parser.add_argument("--output", default=os.environ.get(
        "RLTEST_OUTPUT", str(ROOT / ".local/rltest/native-relics")))
    parser.add_argument("--timeout", type=int, default=600)
    args = parser.parse_args()
    names = SCENARIOS if args.scenario == "all" else (args.scenario,)
    results = []
    failures = []
    for name in names:
        try:
            results.append(run_one(name, args.core, args.rom, args.map_path,
                                   args.output, args.timeout))
        except HarnessError as exc:
            failures.append(name)
            print(f"NATIVE RELIC {name} FAILED: {exc}", flush=True)
    if failures:
        raise SystemExit(f"NATIVE RELICS FAILED: {', '.join(failures)}")
    print(f"NATIVE RELICS PASSED ({len(results)} real battles)")


if __name__ == "__main__":
    main()
