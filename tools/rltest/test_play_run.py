"""Drive the W7 run UI and three chained native battles in CPU test mode."""

import argparse
import ctypes
from pathlib import Path

from tools.rltest.frontend import Frontend, HarnessError
from tools.rltest.run import default_core
from tools.rltest.test_custom_battle import Mon


class Node(ctypes.BigEndianStructure):
    _fields_ = [("type", ctypes.c_uint8), ("edges", ctypes.c_uint8),
                ("target_species", ctypes.c_uint8)]


class Map(ctypes.BigEndianStructure):
    _fields_ = [("lanes", ctypes.c_uint8 * 7), ("nodes", Node * 28)]


class Stats(ctypes.BigEndianStructure):
    _fields_ = [("battles", ctypes.c_uint32), ("wins", ctypes.c_uint32),
                ("nodes", ctypes.c_uint32), ("recruits", ctypes.c_uint32)]


class Run(ctypes.BigEndianStructure):
    _fields_ = [
        ("seed", ctypes.c_uint32), ("rng", ctypes.c_uint32),
        ("act", ctypes.c_uint8), ("row", ctypes.c_uint8),
        ("lane", ctypes.c_uint8), ("phase", ctypes.c_uint8),
        ("ascension", ctypes.c_uint8), ("party_count", ctypes.c_uint8),
        ("map", Map), ("party", Mon * 6),
        ("bag", ctypes.c_uint16 * 13), ("money", ctypes.c_uint32),
        ("relics", ctypes.c_uint32), ("consumed_relics", ctypes.c_uint32),
        ("tm_count", ctypes.c_uint8), ("tm_moves", ctypes.c_uint8 * 16),
        ("stats", Stats), ("daycare", Mon),
        ("daycare_nodes", ctypes.c_uint8), ("daycare_active", ctypes.c_uint8),
    ]


SCENE_MENU = 0
SCENE_DRAFT = 1
SCENE_MAP = 2
SCENE_PARTY = 3
SCENE_REWARD = 4
SCENE_SUMMARY = 5
SCENE_BATTLE = 7


def require(condition, message):
    if not condition:
        raise HarnessError(message)


def scene(emu):
    return emu.read_u32("gRlPlayScene")


def wait_scene(emu, expected, frames=1200):
    emu.wait(lambda runner: scene(runner) == expected, frames=frames,
             description=f"roguelike scene {expected}")


def press(emu, button, after=4):
    emu.press(button, frames=4, after=after)


def submit_first_move(emu):
    emu.press("N64_A", frames=4, after=1)
    emu.stick(0, -32767, right=True)
    emu.step(12)
    emu.stick(0, 0, right=True)
    emu.step(4)


def c_button(emu, x, y):
    emu.stick(x, y, right=True)
    emu.step(12)
    emu.stick(right=True)
    emu.step(120)


def read_run(emu):
    return Run.from_buffer_copy(emu.read("gRlRun", ctypes.sizeof(Run)))


def choose_active_and_battle(emu, battle_index):
    press(emu, "N64_A")
    wait_scene(emu, SCENE_PARTY)
    emu.screenshot(emu.output / f"active-party-{battle_index}.png")

    # An empty selection cannot launch a battle. B returns to the map without
    # advancing its node; re-entering restores the default living team.
    run = read_run(emu)
    if battle_index == 1:
        require(run.party_count == 3, "expected a three-Pokemon starter party")
        for slot in range(3):
            press(emu, "N64_A")
            if slot < 2:
                press(emu, "DOWN")
        press(emu, "DOWN")
        press(emu, "N64_A")
        require(scene(emu) == SCENE_PARTY,
                "empty active team unexpectedly launched a battle")
    press(emu, "N64_B")
    wait_scene(emu, SCENE_MAP)
    press(emu, "N64_A")
    wait_scene(emu, SCENE_PARTY)

    run = read_run(emu)
    required = min(3, sum(
        mon.species != 0 and mon.cur_hp != 0 and not mon.fainted
        for mon in run.party[:run.party_count]
    ))
    require(required > 0, "run reached active selection with no live party")
    # Clear the default team, then put the deterministic draft's strongest
    # attacker first; all three remain in the battle team.
    for slot in range(run.party_count):
        press(emu, "N64_A")
        if slot < run.party_count - 1:
            press(emu, "DOWN")
    press(emu, "N64_A", after=1)
    press(emu, "UP")
    press(emu, "N64_A")
    press(emu, "UP")
    press(emu, "N64_A")
    for _ in range(run.party_count):
        press(emu, "DOWN")
    press(emu, "N64_A")
    wait_scene(emu, SCENE_BATTLE)
    emu.wait(lambda runner: runner.read_u32("gRlBattleActive") == 1,
             frames=600, description="resident native battle handoff")
    require(emu.read_u32("gRlBattleActive") == 1,
            "native battle adapter did not enter its battle loop")
    live = emu.wait_battle(frames=3600)
    active = run.party[2]
    require(live[0]["species"] == active.species,
            f"native battle used the wrong lead: {live[0]} vs {active.species}")
    require(live[0]["hp"] == active.cur_hp and live[0]["pp"][0] == active.pp[0],
            f"battle did not restore persisted HP/PP: {live[0]} vs {active.cur_hp}/{active.pp[0]}")

    # Side records are initialized before the entrance animation and command
    # prompt finish; wait for the native opening sequence before sending input.
    emu.step(1200)
    emu.save(emu.output / f"native-start-{battle_index}.state")
    emu.screenshot(emu.output / f"native-battle-{battle_index}.png")
    submit_first_move(emu)
    emu.step(2100)
    after_first_move = emu.battle_mons()
    require(after_first_move[0]["pp"][0] < live[0]["pp"][0],
            f"native move command was not accepted: {live[0]} -> {after_first_move[0]}")
    for _ in range(24):
        if emu.read_u32("gRlBattleActive") == 0:
            break
        submit_first_move(emu)
        emu.step(1200)
    emu.wait(lambda runner: scene(runner) != SCENE_BATTLE, frames=45000,
             interval=4, description="native battle result")
    require(emu.read_u32("gRlBattleActive") == 0, "battle-active flag was not cleared")
    after = read_run(emu)
    require(after.stats.battles == battle_index, "battle result was not recorded")
    if battle_index < 3:
        require(scene(emu) == SCENE_REWARD,
                f"test battle {battle_index} did not win into the reward scene: {scene(emu)}")
        damaged = any(
            after.party[i].cur_hp < run.party[i].cur_hp or
            any(after.party[i].pp[move] < run.party[i].pp[move] for move in range(4))
            for i in range(min(run.party_count, after.party_count))
        )
        require(damaged, "native battle did not debit party HP or PP")
        press(emu, "N64_A")
        wait_scene(emu, SCENE_MAP)
    else:
        require(scene(emu) == SCENE_REWARD,
                f"third win did not offer its final reward: scene={scene(emu)}")
        press(emu, "N64_A")
        wait_scene(emu, SCENE_SUMMARY)
        after = read_run(emu)
        require(after.phase == 4, f"three wins did not mark the run complete: phase={after.phase}")
    return after


def verify_loss_summary(emu):
    press(emu, "N64_A")
    wait_scene(emu, SCENE_DRAFT)
    emu.step(30)
    for button in ("N64_A", "DOWN", "N64_A", "DOWN", "N64_A"):
        emu.press(button, frames=4, after=16)
    for _ in range(5):
        emu.press("DOWN", frames=4, after=16)
    emu.press("N64_A", frames=4, after=16)
    if scene(emu) == SCENE_DRAFT:
        raise HarnessError("loss fixture could not confirm three starter rentals")
    wait_scene(emu, SCENE_MAP)

    run = read_run(emu)
    for mon in run.party[:run.party_count]:
        mon.cur_hp = 0
        mon.fainted = 1
    emu.write("gRlRun", bytes(run))
    press(emu, "N64_A")
    wait_scene(emu, SCENE_SUMMARY)
    require(read_run(emu).phase == 5, "all-fainted run did not record a loss")
    press(emu, "N64_B")
    wait_scene(emu, SCENE_MENU)


def run(emu, output):
    require(ctypes.sizeof(Mon) == emu.read_u32("gRlBattleTestMonSize"),
            "canonical RlMon layout mismatch")
    require(ctypes.sizeof(Run) > ctypes.sizeof(Mon) * 6,
            "invalid RunState layout")

    emu.wait_state(2, frames=3600)
    emu.step(120)
    emu.press("START", frames=6, after=120)
    emu.wait_state(4)
    emu.press("N64_A", frames=6, after=120)
    emu.step(120)
    # Test mode boosts the party but leaves battle decisions on the human
    # controller, so this scenario exercises the same native battle UI as play.
    emu.write_u32("gRlPlayTestMode", 2)
    emu.write_u32("gRlPlayTestSeed", 0x13579BDF)

    emu.hold("L")
    emu.press("N64_A", frames=6, after=120)
    emu.wait_state(0x30, frames=600)
    emu.release("L")
    wait_scene(emu, SCENE_MENU)
    emu.screenshot(output / "run-menu.png")

    press(emu, "N64_A")
    wait_scene(emu, SCENE_DRAFT)
    first_draft = emu.read("gRlPlayDraftSpecies", 6)
    press(emu, "N64_B")
    wait_scene(emu, SCENE_MENU)
    press(emu, "N64_A")
    wait_scene(emu, SCENE_DRAFT)
    require(emu.read("gRlPlayDraftSpecies", 6) == first_draft,
            "same seed did not reproduce the rental draft")

    before_reroll = read_run(emu).rng
    original_species = set(first_draft)
    for _ in range(6):
        press(emu, "DOWN")
    press(emu, "N64_A")
    wait_scene(emu, SCENE_DRAFT)
    require(read_run(emu).rng != before_reroll, "free reroll did not advance run RNG")
    require(not (set(emu.read("gRlPlayDraftSpecies", 6)) & original_species),
            "free reroll repeated a species from the original draft")

    for button in ("N64_A", "DOWN", "N64_A", "DOWN", "N64_A"):
        press(emu, button)
    for _ in range(5):
        press(emu, "DOWN")
    press(emu, "N64_A")
    wait_scene(emu, SCENE_MAP)
    run_state = read_run(emu)
    require(run_state.party_count == 3, f"starter draft did not select three: {run_state.party_count}")
    require(all(run_state.party[i].species for i in range(3)),
            "starter draft included an empty party slot")

    press(emu, "N64_B")
    require(scene(emu) == SCENE_MAP, "B unexpectedly discarded an active run")
    for battle_index in range(1, 4):
        run_state = choose_active_and_battle(emu, battle_index)
        emu.screenshot(output / f"after-battle-{battle_index}.png")

    emu.screenshot(output / "run-summary.png")
    require(run_state.stats.wins == 3, f"expected three native wins, got {run_state.stats.wins}")
    press(emu, "N64_B")
    wait_scene(emu, SCENE_MENU)
    press(emu, "N64_B")
    emu.wait_state(4, frames=600)
    press(emu, "N64_A")
    emu.step(120)
    emu.hold("L")
    press(emu, "N64_A", after=120)
    emu.wait_state(0x30, frames=600)
    emu.release("L")
    wait_scene(emu, SCENE_MENU)
    verify_loss_summary(emu)
    press(emu, "N64_B")
    emu.wait_state(4, frames=600)
    print(f"PLAYABLE RUN PASSED at frame {emu.frame}; seed={run_state.seed:#010x}; "
          f"battles={run_state.stats.battles}, wins={run_state.stats.wins}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--core", default=default_core())
    parser.add_argument("--rom", default="build_rl/pokestadium-rl.z64")
    parser.add_argument("--map", dest="map_path", default="build_rl/pokestadium-rl.map")
    parser.add_argument("--output", default=".local/rltest/play-run")
    args = parser.parse_args()
    output = Path(args.output)
    with Frontend(args.core, args.rom, args.map_path, output, timeout=900) as emu:
        try:
            run(emu, output)
        except HarnessError:
            print(f"Failure at frame {emu.frame}, state "
                  f"{emu.read_u32('gCurrentGameState'):#x}, scene={scene(emu)}",
                  flush=True)
            print(f"battle_active={emu.read_u32('gRlBattleActive')}, "
                  f"battle_count={emu.read_u32('gRlBattleCount')}, "
                  f"battle_sides={emu.battle_mons()}", flush=True)
            try:
                sides = emu.resolve("D_84390010")
                print(f"battle_side_table={emu.read(sides, 8).hex()}",
                      flush=True)
            except HarnessError as exc:
                print(f"battle_side_table unavailable: {exc}", flush=True)
            fragments = emu.resolve("gFragments")
            for fragment in (34, 62, 63):
                print(f"fragment{fragment}={emu.read(fragments + fragment * 8, 8).hex()}",
                      flush=True)
            if emu.framebuffer is not None:
                emu.screenshot(output / "failure.png")
            raise


if __name__ == "__main__":
    main()
