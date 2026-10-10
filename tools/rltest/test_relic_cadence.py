"""Prove turn-based relic cadence in human-controlled native battles.

The harness launches custom battles with ``player_cpu = 0``. It drives the
native command menu with controller input only, and judges cadence from the
hooked engine trace and the persisted result. It never writes live battle
state, RNG state or result state.

Trace kinds used here: 3 completed turn, 4 Soul block, 8 command commit
(counted=e), and 9 anomaly (a turn opened before the previous one closed).
"""

import argparse
import ctypes
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.rltest.frontend import Frontend, HarnessError
from tools.rltest.test_custom_battle import Mon, Result
from tools.rltest.test_native_relics import (
    RELIC, BattleRules, _mon, _read_trace, default_paths,
)

SWITCH_VOLUNTARY = 0x0F
SWITCH_FAINTED = 0x10
SIDE_SWITCH_STATE = 0x654 + 0x2D
SIDE_ACTIVE_INDEX = 0x654 + 0x08
PARTY_DIRECTIONS = ((0, -32767), (32767, 0), (0, 32767))
MENU_IDLE_FRAMES = 900
GROWL, PSYCHIC, THUNDER_WAVE, SPLASH = 45, 94, 86, 150
MEWTWO, MAGIKARP, CHANSEY = 150, 129, 113


def _expect(condition, message):
    if not condition:
        raise HarnessError(message)


def scenario(name):
    if name == "enemy-replacement":
        # Turn 0 KOs Magikarp; Chansey's replacement send must not be a turn.
        # Chansey's first Thunder Wave is therefore turn 1 and Soul blocks it.
        return {
            "mask": (1 << RELIC["leftovers"]) | (1 << RELIC["cursed-power"]) | (1 << RELIC["soul"]),
            "party": [_mon(MEWTWO, 100, [PSYCHIC], hp=200)],
            "enemy": [_mon(MAGIKARP, 5, [SPLASH]), _mon(CHANSEY, 100, [THUNDER_WAVE])],
            "plan": [("move",)] * 24,
        }
    if name == "player-switch":
        # Turn 0's Cursed tick KOs slot 0 through the func_84328734 phase-5
        # faint route; the human replacement send (slot 1) is not a turn, the
        # voluntary switch to slot 2 is, and turn 2's KO wins normally.
        return {
            "mask": 1 << RELIC["cursed-power"],
            "party": [_mon(MEWTWO, 100, [GROWL], hp=1), _mon(MEWTWO, 100, [GROWL]),
                      _mon(MEWTWO, 100, [PSYCHIC])],
            "enemy": [_mon(CHANSEY, 30, [GROWL])],
            "plan": [("move",), ("replace", 1), ("switch", 2), ("move",)],
        }
    raise HarnessError(f"Unknown cadence scenario {name!r}")


def side_addr(emu, index):
    return emu.read_u32(emu.resolve("D_84390010") + index * 4)


def counted_opens(events):
    return [e for e in events if e["kind"] == 8 and e["e"] == 1]


def finishes(events):
    return [e for e in events if e["kind"] == 3]


def idle_in_menu(emu, label, output):
    """Menu waits and animations must not advance or tick a turn."""
    before = _read_trace(emu)
    mons = emu.battle_mons()
    emu.step(MENU_IDLE_FRAMES)
    after = _read_trace(emu)
    mons_after = emu.battle_mons()
    _expect(after == before,
            f"{label}: trace changed during {MENU_IDLE_FRAMES} idle menu frames: "
            f"{before[len(before) - 2:]} -> {after[len(before) - 2:]}")
    _expect(mons and mons_after and mons[0]["hp"] == mons_after[0]["hp"],
            f"{label}: live HP changed while waiting in the menu")
    emu.screenshot(output / f"{label}-menu.png")


def wait_turn_settled(emu):
    """Wait until every counted turn has closed, or the battle has ended."""
    def settled(runner):
        if runner.read_u32("gRlBattleActive") == 0:
            return True
        events = _read_trace(runner)
        return len(finishes(events)) >= len(counted_opens(events)) and \
            runner.battle_mons() is not None
    emu.wait(settled, frames=6000, interval=10, description="turn settle")


def wait_open(emu, before, counted, label, output):
    def opened(runner):
        events = _read_trace(runner)
        return any(e["kind"] == 8 and e["e"] == counted for e in events[before:])
    try:
        emu.wait(opened, frames=3000, interval=5, description=f"{label} commit")
    except HarnessError:
        emu.screenshot(output / f"{label}-not-committed.png")
        raise


def submit_move(emu):
    emu.press("N64_A", frames=4, after=1)
    emu.stick(0, -32767, right=True)
    emu.step(12)
    emu.stick(0, 0, right=True)
    emu.step(4)


def choose_party(emu, target, opener):
    emu.press(opener, after=120)
    emu.step(600)
    emu.stick(*PARTY_DIRECTIONS[target], right=True)
    emu.step(12)
    emu.stick(right=True)
    emu.step(120)
    emu.press("N64_A", after=120)


def drive(emu, plan, output):
    emu.step(1200)
    step = 0
    for _ in range(64):
        if emu.read_u32("gRlBattleActive") == 0:
            return
        wait_turn_settled(emu)
        if emu.read_u32("gRlBattleActive") == 0:
            return
        emu.step(300)
        player = side_addr(emu, 0)
        action = plan[step] if step < len(plan) else ("move",)
        fainted = emu.read_u8(player + SIDE_SWITCH_STATE) == SWITCH_FAINTED
        if fainted:
            _expect(action[0] == "replace",
                    f"unexpected replacement prompt at plan step {step}: {action}")
        before = len(_read_trace(emu))
        label = f"step{step}-{action[0]}"
        if action[0] == "replace":
            emu.wait(lambda r: r.read_u8(side_addr(r, 0) + SIDE_SWITCH_STATE) == SWITCH_FAINTED,
                     frames=3000, interval=10, description="replacement prompt")
            emu.step(600)
            choose_party(emu, action[1], "N64_A")
            wait_open(emu, before, 0, label, output)
            emu.wait(lambda r: r.read_u32(side_addr(r, 0) + SIDE_ACTIVE_INDEX) == action[1],
                     frames=3000, interval=10, description="replacement switch-in")
        else:
            idle_in_menu(emu, label, output)
            if action[0] == "switch":
                choose_party(emu, action[1], "N64_B")
            else:
                submit_move(emu)
            wait_open(emu, before, 1, label, output)
        step += 1
    raise HarnessError("battle did not finish within 64 human commands")


def check_enemy_replacement(events, result):
    opens = [e for e in events if e["kind"] == 8]
    replacement = [e for e in opens if e["e"] == 0]
    counted = counted_opens(events)
    rounds = finishes(events)
    _expect(len(replacement) >= 1 and all(e["e"] == 0 for e in opens if e["d"] == SWITCH_FAINTED),
            f"Chansey's replacement send was not classified as a non-turn: {opens}")
    _expect(len(rounds) == len(counted) - 1,
            f"expected one tick per non-terminal turn: {len(counted)} turns, {rounds}")
    _expect([e["turn"] for e in rounds] == list(range(len(rounds))),
            f"turn indices are not consecutive: {rounds}")
    hp = 200
    for event in rounds:
        _expect(event["a"] == hp, f"HP changed between turn ticks (extra tick?): {event}")
        _expect(event["b"] == min(event["a"] + event["c"] // 16, event["c"]) - 1,
                f"Leftovers then Cursed did not apply exactly once: {event}")
        hp = event["b"]
    debits = [prev["d"] - cur["d"] for prev, cur in zip(
        [{"d": rounds[0]["d"] + 1}] + rounds, rounds)]
    _expect(debits[:2] == [1, 1], f"first two turns did not each spend one PP: {debits}")
    _expect(all(d in (0, 1) for d in debits), f"a tick did not match one turn: {debits}")
    blocks = [e for e in events if e["kind"] == 4]
    _expect(any(e["turn"] == 1 and e["c"] == THUNDER_WAVE for e in blocks),
            f"Soul did not block Chansey's first Thunder Wave on turn 1: {blocks}")
    _expect(all(e["turn"] < 2 for e in blocks), f"Soul blocked after expiry: {blocks}")
    _expect(result.outcome == 1, f"expected a win, got {result.outcome}")
    _expect(result.player[0].status & 0x40, "Thunder Wave never landed after Soul expired")
    _expect(result.player[0].cur_hp == hp,
            f"persisted HP {result.player[0].cur_hp} != last settled tick {hp}")


def check_player_switch(events, result):
    opens = [e for e in events if e["kind"] == 8]
    counted = counted_opens(events)
    rounds = finishes(events)
    _expect([e["a"] for e in counted] == [1, 2, 1],
            f"expected fight, voluntary switch, fight turns: {counted}")
    _expect(counted[1]["c"] == SWITCH_VOLUNTARY, f"switch turn state: {counted[1]}")
    replacement = [e for e in opens if e["e"] == 0 and e["c"] == SWITCH_FAINTED]
    _expect(len(replacement) == 1, f"expected one uncounted player replacement: {opens}")
    _expect(len(rounds) == 2 and rounds[0]["turn"] == 0 and rounds[1]["turn"] == 1,
            f"expected ticks only after turns 0 and 1: {rounds}")
    _expect(rounds[0]["a"] == 1 and rounds[0]["b"] == 0, f"Cursed KO tick: {rounds[0]}")
    _expect(rounds[1]["b"] == rounds[1]["a"] - 1 == rounds[1]["c"] - 1,
            f"switched-in mon did not take exactly one tick: {rounds[1]}")
    _expect(result.outcome == 1 and result.player_count == 3, f"result: {result.outcome}")
    p = result.player
    _expect(p[0].fainted and p[0].cur_hp == 0, "Cursed KO did not persist")
    _expect(p[1].cur_hp == p[1].max_hp, f"replacement-only mon was ticked: {p[1].cur_hp}")
    _expect(p[2].cur_hp == p[2].max_hp - 1, f"switched-in mon HP {p[2].cur_hp}")


CHECKS = {"enemy-replacement": check_enemy_replacement,
          "player-switch": check_player_switch}


def run_one(name, core, rom, map_path, output, timeout):
    case = scenario(name)
    output = Path(output) / name
    output.mkdir(parents=True, exist_ok=True)
    with Frontend(core, rom, map_path, output=output, timeout=timeout) as emu:
        emu.wait_state(2, frames=2400)
        emu.step(120)
        party = (Mon * 6)(*case["party"])
        enemy = (Mon * 6)(*case["enemy"])
        rules = BattleRules()
        rules.stadium = 3
        rules.ai_level = 11
        rules.player_count = 1
        rules.player_cpu = 0
        rules.use_state = 1
        rules.relics = case["mask"]
        before = emu.read_u32("gRlBattleCount")
        emu.write("gRlRelicTestParty", bytes(party))
        emu.write("gRlRelicTestEnemy", bytes(enemy))
        emu.write("gRlRelicTestRules", bytes(rules))
        emu.write_u32("gRlRelicTestPartyCount", len(case["party"]))
        emu.write_u32("gRlRelicTestEnemyCount", len(case["enemy"]))
        emu.write_u32("gRlRelicTestMask", case["mask"])
        emu.write_u32("gRlRelicTestConsumed", 0)
        emu.write_u32("gRlBattleTestRequest", 3)
        emu.press("START", frames=10, after=10)
        emu.wait_battle(frames=2400)
        try:
            drive(emu, case["plan"], output)
            emu.wait(lambda r: r.read_u32("gRlBattleCount") == before + 1,
                     frames=36000, interval=10, description=f"{name} battle completion")
        except HarnessError:
            (output / "failure-trace.json").write_text(
                json.dumps({"frame": emu.frame, "events": _read_trace(emu)}) + "\n")
            emu.screenshot(output / "failure.png")
            raise
        result = Result.from_buffer_copy(
            emu.read("gRlLastBattleResult", ctypes.sizeof(Result)))
        events = _read_trace(emu)
        (output / "cadence.json").write_text(json.dumps({
            "scenario": name, "frame": emu.frame, "outcome": result.outcome,
            "player": [[m.species, m.cur_hp, m.max_hp, m.status, m.fainted]
                       for m in result.player[:result.player_count]],
            "events": events}, indent=1) + "\n")
        _expect(not any(e["kind"] == 9 for e in events),
                f"a turn opened before the previous turn closed: {events}")
        CHECKS[name](events, result)
        print(f"RELIC CADENCE PASSED {name}: frame={emu.frame}, "
              f"turns={len(counted_opens(events))}, ticks={len(finishes(events))}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scenario", choices=("all", *CHECKS), default="all")
    core, rom, map_path, output = default_paths("relic-cadence")
    parser.add_argument("--core", default=core)
    parser.add_argument("--rom", default=rom)
    parser.add_argument("--map", dest="map_path", default=map_path)
    parser.add_argument("--output", default=output)
    parser.add_argument("--timeout", type=int, default=900)
    args = parser.parse_args()
    names = tuple(CHECKS) if args.scenario == "all" else (args.scenario,)
    failures = []
    for name in names:
        try:
            run_one(name, args.core, args.rom, args.map_path, args.output, args.timeout)
        except HarnessError as exc:
            failures.append(name)
            print(f"RELIC CADENCE {name} FAILED: {exc}", flush=True)
    if failures:
        raise SystemExit(f"RELIC CADENCE FAILED: {', '.join(failures)}")
    print(f"RELIC CADENCE PASSED ({len(names)} human-controlled battles)")


if __name__ == "__main__":
    main()
