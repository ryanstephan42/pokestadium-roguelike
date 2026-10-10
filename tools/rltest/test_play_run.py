"""Drive the W7 run UI and three chained native battles in CPU test mode."""

import argparse
import ctypes
from pathlib import Path

from tools.rltest.frontend import Frontend, HarnessError
from tools.rltest.run import default_core
from tools.rltest.test_custom_battle import Mon, Result


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
# RlBattleRules: 8 legacy byte fields plus the u32 relic mask.
RL_BATTLE_RULES_SIZE = 12


def require(condition, message):
    if not condition:
        raise HarnessError(message)


def scene(emu):
    return emu.read_u32("gRlPlayScene")


def wait_scene(emu, expected, frames=1200):
    emu.wait(lambda runner: scene(runner) == expected, frames=frames,
             description=f"roguelike scene {expected}")


def wait_draft(emu):
    emu.wait(lambda runner: scene(runner) in (SCENE_DRAFT, 9), frames=1200,
             description="draft or explicit new-profile choice")
    if scene(emu) == 9:
        emu.step(30)
        press(emu, "N64_A")
    wait_scene(emu, SCENE_DRAFT)


def press(emu, button, after=4):
    emu.press(button, frames=4, after=after)


def submit_first_move(emu):
    submit_move(emu, -32767)


def submit_second_move(emu):
    submit_move(emu, 32767)


def submit_move(emu, y):
    emu.press("N64_A", frames=4, after=1)
    emu.stick(0, y, right=True)
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
    require(required == 3,
            f"win scenario needs three living starters, found {required}")
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
    pending_addr = emu.resolve("gRlPendingBattle")
    queued = Mon.from_buffer_copy(emu.read(pending_addr, ctypes.sizeof(Mon)))
    player_slots_offset = ctypes.sizeof(Mon) * 15 + RL_BATTLE_RULES_SIZE
    require(emu.read_u8(pending_addr + player_slots_offset) == 2,
            "damaged carry-over lead did not retain party-slot identity")
    if battle_index > 1:
        require(queued.cur_hp == active.cur_hp and
                list(queued.moves) == list(active.moves) and
                list(queued.pp) == list(active.pp),
                "native battle input did not match carried run-slot state")
    require(live[0]["hp"] == queued.cur_hp and live[0]["moves"] == list(queued.moves)
            and live[0]["pp"] == list(queued.pp),
            f"native battle did not restore queued HP/moves/PP: {live[0]} vs "
            f"{queued.cur_hp}/{list(queued.moves)}/{list(queued.pp)}")

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
        submit_second_move(emu)
        emu.step(1200)
    emu.wait(lambda runner: scene(runner) != SCENE_BATTLE, frames=45000,
             interval=4, description="native battle result")
    require(emu.read_u32("gRlBattleActive") == 0, "battle-active flag was not cleared")
    after = read_run(emu)
    require(after.stats.battles == battle_index, "battle result was not recorded")
    if battle_index < 3:
        require(scene(emu) == SCENE_REWARD,
                f"test battle {battle_index} did not win into the reward scene: {scene(emu)}")
        require(after.party[2].cur_hp < queued.cur_hp,
                "native battle did not damage the selected lead")
        if battle_index == 1:
            require(after.party[2].cur_hp > 0,
                    "first-battle carry lead fainted before the next battle")
        require(any(
            after.party[i].pp[move] < run.party[i].pp[move]
            for i in range(min(run.party_count, after.party_count))
            for move in range(4)
        ), "native battle did not debit persistent party PP")
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
    """Exercise summary UI with a synthetic all-fainted party fixture."""
    emu.write_u32("gRlPlayTestMode", 2)
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


def start_new_run(emu, seed, test_mode):
    emu.write_u32("gRlPlayTestMode", test_mode)
    emu.write_u32("gRlPlayTestSeed", seed)
    press(emu, "N64_A")
    wait_draft(emu)
    emu.step(30)
    emu.screenshot(emu.output / f"draft-moves-{seed:08x}.png")
    for button in ("N64_A", "DOWN", "N64_A", "DOWN", "N64_A"):
        emu.press(button, frames=4, after=16)
    for _ in range(5):
        emu.press("DOWN", frames=4, after=16)
    emu.press("N64_A", frames=4, after=16)
    wait_scene(emu, SCENE_MAP)


def reenter_roguelike(emu):
    emu.press("N64_A", frames=6, after=120)
    emu.step(120)
    enter_roguelike_from_menu(emu)


def enter_roguelike_from_menu(emu):
    emu.hold("L")
    emu.press("N64_A", frames=6, after=120)
    emu.wait_state(0x30, frames=600)
    emu.release("L")
    wait_scene(emu, SCENE_MENU)


def verify_seed8_suspend_resume(emu):
    start_new_run(emu, 8, 0)
    run = read_run(emu)
    require(run.seed == 8 and run.party_count == 3,
            "seed-8 suspend fixture did not start the expected run")
    for _ in range(run.map.lanes[0]):
        press(emu, "DOWN")
    press(emu, "N64_A")
    emu.wait_state(4, frames=600)
    require(read_run(emu).seed == 8,
            "seed-8 save-and-return did not preserve resident run state")
    reenter_roguelike(emu)
    press(emu, "DOWN")
    press(emu, "N64_A")
    wait_scene(emu, SCENE_MAP)
    restored = read_run(emu)
    require(restored.seed == 8 and restored.phase == 1 and
            restored.party_count == 3,
            "Controller Pak resume did not restore the seed-8 route")


def verify_native_loss(emu):
    emu.step(30)
    press(emu, "N64_A")
    wait_scene(emu, SCENE_PARTY)
    emu.step(30)
    run = read_run(emu)
    require(run.party_count == 3, "native-loss fixture needs three starters")
    for _ in range(3):
        press(emu, "DOWN")
    press(emu, "N64_A")
    wait_scene(emu, SCENE_BATTLE)
    emu.wait(lambda runner: runner.read_u32("gRlBattleActive") == 1,
             frames=600, description="human native-loss battle launch")
    trainer_count_offset = ctypes.sizeof(Mon) * 15 + RL_BATTLE_RULES_SIZE + 5
    trainer_count = emu.read_u8(emu.resolve("gRlPendingBattle") +
                                trainer_count_offset)
    require(trainer_count >= 1, "native-loss battle has no trainer roster")
    emu.step(1200)
    side_addr = emu.resolve("D_84390010")
    for loss_turn in range(30):
        if emu.read_u32("gRlBattleActive") == 0:
            break
        active = emu.battle_mons()
        if active and active[0]["hp"] == 0:
            side = emu.read_u32(side_addr)
            party_addr = emu.read_u32(side + 0x724)
            party_count = emu.read_u8(party_addr + 2)
            active_index = emu.read_u32(side + 0x65C)
            living = [
                i for i in range(party_count)
                if emu.read_u16(party_addr + 0x1E + i * 0x54) != 0
            ]
            emu.step(600)
            active = emu.battle_mons()
            control = {
                "mode": emu.read_u32(side + 0x658),
                "turn": emu.read_u32(side + 0x664),
                "choice": emu.read_u32(side + 0x66C),
                "state": emu.read_u8(side + 0x681),
            }
            print(f"native-loss frame={emu.frame} hp={active[0]['hp'] if active else None} "
                  f"party={party_count} active={active_index} living={living} "
                  f"control={control}")
            emu.screenshot(emu.output / f"native-loss-fainted-{loss_turn}.png")
            if living:
                press(emu, "N64_A", after=120)
                emu.step(600)
                emu.screenshot(emu.output / f"native-loss-pokemon-{loss_turn}.png")
                target = next((i for i in living if i > active_index), living[0])
                if party_count < 4:
                    directions = ((0, -32767), (32767, 0), (0, 32767))
                    c_button(emu, *directions[target])
                else:
                    for _ in range((target - active_index) % party_count):
                        press(emu, "DOWN", after=8)
                press(emu, "N64_A", after=120)
                emu.step(600)
                switched = emu.battle_mons()
                require(switched is not None and switched[0]["hp"] > 0 and
                        emu.read_u32(side + 0x65C) != active_index,
                        f"native battle did not switch to living party slot {target}")
            else:
                press(emu, "N64_A", after=120)
                emu.screenshot(emu.output / "native-loss-no-will.png")
                emu.step(120)
        else:
            submit_first_move(emu)
            emu.step(2100)
    require(emu.read_u32("gRlBattleActive") == 0,
            "native loss did not return from battle within 30 human input turns")
    wait_scene(emu, SCENE_SUMMARY)
    result = Result.from_buffer_copy(
        emu.read("gRlLastBattleResult", ctypes.sizeof(Result)))
    lost = read_run(emu)
    require(result.outcome == 0,
            f"expected a native human battle loss, got outcome {result.outcome}")
    require(result.player_count == 3 and all(
        result.player[i].fainted and result.player[i].cur_hp == 0
        for i in range(result.player_count)
    ), "native adapter did not return all three battle losses")
    require(lost.phase == 5 and lost.stats.battles == 1 and all(
        lost.party[i].fainted and lost.party[i].cur_hp == 0
        for i in range(lost.party_count)
    ), "native defeat did not produce RL_LOST from adapter output")
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
    emu.write_u32("gRlPlayTestMode", 4)
    emu.write_u32("gRlPlayTestSeed", 0x13579BDF)

    emu.hold("L")
    emu.press("N64_A", frames=6, after=120)
    emu.wait_state(0x30, frames=600)
    emu.release("L")
    wait_scene(emu, SCENE_MENU)
    emu.screenshot(output / "run-menu.png")

    press(emu, "N64_A")
    wait_draft(emu)
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
    reenter_roguelike(emu)
    verify_loss_summary(emu)
    press(emu, "N64_B")
    emu.wait_state(4, frames=600)
    reenter_roguelike(emu)
    verify_seed8_suspend_resume(emu)
    # Production resumes the canonical seed-8 map, not the trainer-only route.
    # Start a fresh guarded battle fixture for the distinct native-loss test.
    resumed = read_run(emu)
    row = 0 if resumed.row == 255 else resumed.row + 1
    for _ in range(resumed.map.lanes[row]):
        press(emu, "DOWN")
    press(emu, "N64_A")
    emu.wait_state(4, frames=600)
    reenter_roguelike(emu)
    start_new_run(emu, 0x13579BDF, 3)
    verify_native_loss(emu)
    press(emu, "N64_B")
    emu.wait_state(4, frames=600)
    print(f"PLAYABLE RUN PASSED at frame {emu.frame}; seed={run_state.seed:#010x}; "
          f"battles={run_state.stats.battles}, wins={run_state.stats.wins}")


def run_native_loss_only(emu):
    emu.wait_state(2, frames=3600)
    emu.step(120)
    emu.press("START", frames=6, after=120)
    emu.wait_state(4)
    emu.press("N64_A", frames=6, after=120)
    emu.step(120)
    emu.hold("L")
    emu.press("N64_A", frames=6, after=120)
    emu.wait_state(0x30, frames=600)
    emu.release("L")
    wait_scene(emu, SCENE_MENU)
    start_new_run(emu, 0x13579BDF, 3)
    verify_native_loss(emu)
    press(emu, "N64_B")
    emu.wait_state(4, frames=600)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--core", default=default_core())
    parser.add_argument("--rom", default="build_rl/pokestadium-rl.z64")
    parser.add_argument("--map", dest="map_path", default="build_rl/pokestadium-rl.map")
    parser.add_argument("--output", default=".local/rltest/play-run")
    parser.add_argument("--native-loss-only", action="store_true")
    args = parser.parse_args()
    output = Path(args.output)
    with Frontend(args.core, args.rom, args.map_path, output, timeout=1800,
                  options={"mupen64plus-pak1": "memory"}) as emu:
        try:
            if args.native_loss_only:
                run_native_loss_only(emu)
                print(f"NATIVE LOSS PASSED at frame {emu.frame}")
            else:
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
