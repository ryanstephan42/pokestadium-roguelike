"""Exercise canonical node scenes with real controller input and Pak resumes."""

import argparse
import ctypes
from pathlib import Path

from tools.rltest.frontend import Frontend, HarnessError
from tools.rltest.run import default_core
from tools.rltest.save_roundtrip import native_command
from tools.rltest.test_play_run import (
    SCENE_MAP, SCENE_MENU, SCENE_PARTY, SCENE_BATTLE,
    press, read_run, reenter_roguelike, require, scene,
    start_new_run, wait_scene,
)
from tools.rltest.test_custom_battle import Mon

SCENE_NODE = 8
# Generated from rl_run_new; no map, party, result or phase writes are used.
EVENT_ROUTES = {
    0: (71, 1), 1: (10, 1), 2: (4, 0), 3: (16, 0),
    4: (21, 2), 5: (50, 1), 6: (8, 1), 7: (121, 1),
    8: (99, 3), 9: (2, 1), 10: (15, 0), 11: (15, 3),
}


def select(emu, index):
    for _ in range(index):
        press(emu, "DOWN")
    press(emu, "N64_A")


def event_id(seed, act, row, lane):
    x = seed ^ 0x45564944 ^ (act << 24) ^ (row << 12) ^ (lane << 4)
    x ^= x >> 16
    x = x * 0x7FEB352D & 0xFFFFFFFF
    x ^= x >> 15
    x = x * 0x846CA68B & 0xFFFFFFFF
    x ^= x >> 16
    x = x or 1
    x ^= x << 13 & 0xFFFFFFFF
    x ^= x >> 17
    x ^= x << 5 & 0xFFFFFFFF
    return x % 12


def identity(run):
    return (
        run.seed, run.rng, run.act, run.row, run.lane, run.phase,
        run.party_count, run.stats.nodes, run.stats.battles,
        run.money, run.relics, run.tm_count, bytes(run.tm_moves),
        tuple(run.bag), tuple(
            (m.species, m.level, m.cur_hp, m.fainted, m.status,
             tuple(m.moves), tuple(m.pp))
            for m in run.party[:run.party_count]),
        run.daycare_active, run.daycare_nodes,
    )


def save_map_and_reenter(emu):
    run = read_run(emu)
    row = 0 if run.row == 255 else run.row + 1
    select(emu, run.map.lanes[row])
    emu.wait_state(4, frames=600)
    reenter_roguelike(emu)


def suspend_node(emu, label):
    before = identity(read_run(emu))
    emu.screenshot(emu.output / f"{label}-before.png")
    press(emu, "START", after=80)
    emu.wait_state(4, frames=600)
    reenter_roguelike(emu)
    select(emu, 1)
    wait_scene(emu, SCENE_NODE)
    emu.step(30)
    require(identity(read_run(emu)) == before,
            f"{label}: suspend/resume altered selected node or run data")
    emu.screenshot(emu.output / f"{label}-resumed.png")


def enter_node(emu, seed, lane, expected):
    start_new_run(emu, seed, 0)
    before = read_run(emu)
    require(before.map.nodes[lane].type == expected,
            f"seed {seed} lane {lane} did not have node type {expected}")
    select(emu, lane)
    wait_scene(emu, SCENE_NODE)
    emu.step(30)
    require(read_run(emu).phase == 2, "node did not advance through canonical core")
    return before


def verify_relic_inventory(emu, label):
    before = identity(read_run(emu))
    run = read_run(emu)
    select(emu, run.map.lanes[run.row + 1] + 1)
    wait_scene(emu, SCENE_NODE)
    emu.step(30)
    press(emu, "R")
    emu.step(30)
    emu.screenshot(emu.output / f"relic-details-{label}.png")
    require(identity(read_run(emu)) == before,
            "viewing relic details mutated the run")
    press(emu, "N64_B")
    press(emu, "N64_B")
    wait_scene(emu, SCENE_MAP)
    require(identity(read_run(emu)) == before,
            "leaving relic inventory advanced or changed the run")


def verify_events(emu, routes=EVENT_ROUTES):
    for event, (seed, lane) in routes.items():
        print(f"event {event}: seed={seed} lane={lane}", flush=True)
        before = enter_node(emu, seed, lane, 4)
        suspend_node(emu, f"event-{event:02d}")
        # Berry Forest's money branch is observable even with a healthy draft.
        select(emu, 1 if event == 9 else 0)
        if event in (2, 3, 4):
            press(emu, "N64_A")
        emu.screenshot(emu.output / f"event-{event:02d}-choice.png")
        wait_scene(emu, SCENE_MAP)
        after = read_run(emu)
        require(after.stats.nodes == 1, f"event {event} completed twice")
        if event in (0, 6):
            require(after.party_count == before.party_count + 1,
                    f"event {event} did not recruit")
        elif event == 1:
            require(after.money in (200, 450), "Game Corner bet was not resolved")
        elif event == 2:
            require(after.daycare_active == 1 and after.party_count == 2,
                    "Daycare did not deposit selected party member")
        elif event == 3:
            require(after.party[0].level == before.party[0].level + 5,
                    "Silph trade did not increase level")
        elif event == 5:
            require(after.relics & (1 << 18), "Lavender blessing missing")
            require(after.party[0].cur_hp == before.party[0].cur_hp - 1,
                    "Lavender HP cost missing")
        elif event == 7:
            require(after.bag[8] == 1, "Oak did not grant Rare Candy")
            select(emu, after.map.lanes[after.row + 1] + 1)
            wait_scene(emu, SCENE_NODE)
            press(emu, "N64_A")
            press(emu, "N64_A")
            used = read_run(emu)
            require(used.bag[8] == 0 and
                    used.party[0].level == before.party[0].level + 1,
                    "Bag Rare Candy did not apply canonical progression")
            press(emu, "N64_B")
            wait_scene(emu, SCENE_MAP)
        elif event in (8, 11):
            require(after.relics != 0, f"event {event} did not grant its relic")
        elif event == 9:
            require(after.money == 500, "Berry Forest money choice missing")
        elif event == 10:
            require(after.tm_count == 1 and after.tm_moves[0] != 0,
                    "Move Tutor did not persist TM identity")
            tm = after.tm_moves[0]
            save_map_and_reenter(emu)
            select(emu, 1)
            wait_scene(emu, SCENE_MAP)
            emu.step(30)
            restored = read_run(emu)
            require(restored.tm_count == 1 and restored.tm_moves[0] == tm,
                    "Controller Pak resume changed the stored TM")
            select(emu, restored.map.lanes[restored.row + 1] + 1)
            wait_scene(emu, SCENE_NODE)
            press(emu, "N64_A")
            taught = False
            for slot in range(restored.party_count):
                press(emu, "N64_A")
                press(emu, "N64_A")
                result = read_run(emu)
                if result.tm_count == 0:
                    require(result.party[slot].moves[0] == tm and
                            result.party[slot].pp[0] > 0,
                            "native-compatible TM did not teach move/PP")
                    taught = True
                    break
                require(result.tm_moves[0] == tm,
                        "rejected TM target consumed or changed TM identity")
                press(emu, "DOWN")
            require(taught, "Move Tutor fixture has no compatible starter target")
            press(emu, "N64_B")
            wait_scene(emu, SCENE_MAP)
        if event in (5, 8, 11):
            verify_relic_inventory(emu, f"event-{event:02d}")
        save_map_and_reenter(emu)


def verify_center_recruit_and_tools(emu):
    enter_node(emu, 2, 0, 2)
    suspend_node(emu, "center")
    press(emu, "N64_A")
    wait_scene(emu, SCENE_MAP)
    save_map_and_reenter(emu)

    enter_node(emu, 1, 1, 5)
    suspend_node(emu, "recruit")
    press(emu, "N64_A")
    wait_scene(emu, SCENE_MAP)
    run = read_run(emu)
    require(run.party_count == 4, "recruit node did not add selected candidate")
    row = run.row + 1
    # Party management: move the first Pokemon to the second slot.
    select(emu, run.map.lanes[row] + 2)
    wait_scene(emu, SCENE_NODE)
    press(emu, "N64_A")
    press(emu, "N64_A")
    select(emu, 1)
    swapped = read_run(emu)
    require(swapped.party[1].species == run.party[0].species,
            "party reorder did not preserve the selected Pokemon")
    # Release requires the separate confirmation screen.
    press(emu, "N64_A")
    select(emu, 1)
    select(emu, 1)
    released = read_run(emu)
    require(released.party_count == 3 and
            released.party[0].species == swapped.party[1].species,
            "confirmed party release did not compact persistent slots")
    press(emu, "N64_B")
    wait_scene(emu, SCENE_MAP)
    save_map_and_reenter(emu)


def verify_mart(emu):
    enter_node(emu, 1, 0, 3)
    suspend_node(emu, "mart")
    # Every item is displayed, with no arbitrary missing catalog window.
    # Try each slot until the only affordable offer (Potion, 200) is purchased.
    bought = False
    for index in range(12):
        before = read_run(emu)
        if index == 10:
            emu.step(30)
            emu.screenshot(emu.output / "mart-relic-effect.png")
        press(emu, "N64_A")
        after = read_run(emu)
        if after.money != before.money:
            require(after.money == 100 and after.bag[0] == 1,
                    "Mart purchased wrong item or price")
            bought = True
        if index < 11:
            press(emu, "DOWN")
    require(bought, "Mart did not expose the affordable Potion")
    # Cursor is at offer 11; Bag is immediately next.
    press(emu, "DOWN")
    press(emu, "N64_A")
    press(emu, "N64_A")
    press(emu, "N64_A")
    require(read_run(emu).bag[0] == 0, "Bag did not consume selected Potion")
    # Node-accessed Bag returns to the shop root after successful use.
    # B only displays the explicit Leave instruction without moving the cursor.
    press(emu, "N64_B")
    select(emu, 14)
    wait_scene(emu, SCENE_MAP)
    save_map_and_reenter(emu)


def verify_battle_entry(emu, seed, route, expected):
    start_new_run(emu, seed, 0)
    for row, lane in enumerate(route):
        run = read_run(emu)
        node = run.map.nodes[row * 4 + lane]
        if row == len(route) - 1:
            before = identity(run)
            save_map_and_reenter(emu)
            select(emu, 1)
            wait_scene(emu, SCENE_MAP)
            emu.step(30)
            require(identity(read_run(emu)) == before,
                    "pre-battle map suspend did not preserve route")
        if row == len(route) - 1:
            require(node.type == expected, "wrong canonical encounter kind")
            before = identity(read_run(emu))
            for _ in range(lane):
                press(emu, "DOWN")
            emu.step(30)
            emu.screenshot(emu.output / f"route-preview-{expected}.png")
            count = emu.read_u32("gRlPlayPreviewCount")
            preview = emu.read("gRlPlayPreviewTeam", count * ctypes.sizeof(Mon))
            require(count == (1 if run.act == 3 else 6),
                    "route preview did not materialize the complete canonical roster")
            require(identity(read_run(emu)) == before,
                    "route preview consumed RNG or mutated the run")
            press(emu, "N64_A")
            wait_scene(emu, SCENE_PARTY)
            # Back must not commit the selected node.
            before = identity(read_run(emu))
            press(emu, "N64_B")
            wait_scene(emu, SCENE_MAP)
            require(identity(read_run(emu)) == before,
                    "cancelled active selection advanced encounter")
            select(emu, lane)
            wait_scene(emu, SCENE_PARTY)
            active_run = read_run(emu)
            slots = [i for i, mon in enumerate(active_run.party[:active_run.party_count])
                     if mon.cur_hp and not mon.fainted][:3]
            require(len(slots) == 3, "ordered native-entry proof needs three living mons")
            # Move the third entrant to lead using the real order controls.
            for _ in range(slots[2]):
                press(emu, "DOWN")
            press(emu, "LEFT")
            press(emu, "LEFT")
            emu.screenshot(emu.output / f"ordered-team-{expected}.png")
            require(identity(read_run(emu)) == before,
                    "editing entrant order changed run-slot identity/state")
            for _ in range(active_run.party_count - slots[2]):
                press(emu, "DOWN")
            press(emu, "N64_A")
            wait_scene(emu, SCENE_BATTLE)
            pending = emu.resolve("gRlPendingBattle")
            mon_size = ctypes.sizeof(Mon)
            queued_slots = list(emu.read(pending + mon_size * 15 + 8, 3))
            require(queued_slots == [slots[2], slots[0], slots[1]],
                    f"queued entrant order was not preserved: {queued_slots}")
            queued_roster = emu.read(pending + mon_size * 9, count * mon_size)
            require(queued_roster == preview,
                    "native encounter differs from the pre-commit preview")
            live = emu.wait_battle(frames=3600)
            require(live[0]["species"] != 0 and live[1]["species"] != 0,
                    "canonical battle did not initialize both native sides")
            lead = active_run.party[slots[2]]
            require(live[0]["species"] == lead.species and
                    live[0]["hp"] == lead.cur_hp and live[0]["moves"] == list(lead.moves),
                    "native battle did not use the explicitly ordered lead")
            emu.screenshot(emu.output / f"native-node-{expected}.png")
            return
        select(emu, lane)
        wait_scene(emu, SCENE_NODE)
        # This route contains only nonbattle nodes before the target.
        require(node.type in (2, 3, 4, 5), "route unexpectedly requires battle")
        leave = 14 if node.type == 3 else 4
        if node.type == 5 or (node.type == 4 and
                             event_id(seed, run.act, row, lane) == 6):
            leave = 5
        select(emu, leave)
        wait_scene(emu, SCENE_MAP)


def boot(emu):
    emu.wait_state(2, frames=3600)
    emu.step(120)
    emu.press("START", frames=6, after=120)
    emu.wait_state(4)
    reenter_roguelike(emu)
    press(emu, "N64_B")
    emu.wait_state(4, frames=600)
    emu.press("N64_A", frames=6, after=120)
    emu.step(120)
    result = native_command(emu, 4, create=1)
    require(result[1] == 0, "explicit unlocked-profile fixture creation failed")
    emu.hold("L")
    emu.press("N64_A", frames=6, after=120)
    emu.wait_state(0x30, frames=600)
    emu.release("L")
    wait_scene(emu, SCENE_MENU)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--battle-kind", choices=("trainer", "elite", "boss"))
    parser.add_argument("--event", type=int, choices=range(12))
    args = parser.parse_args()
    output = Path(".local/rltest/nodes")
    with Frontend(default_core(), Path("build_rl/pokestadium-rl.z64"),
                  Path("build_rl/pokestadium-rl.map"), output=output,
                  options={"mupen64plus-pak1": "memory"}, timeout=3600) as emu:
        try:
            boot(emu)
            if args.event is not None:
                verify_events(emu, {args.event: EVENT_ROUTES[args.event]})
            elif args.battle_kind:
                seed, route, kind = {
                    "trainer": (3, [2], 0),
                    "elite": (1, [0, 0, 0, 0, 0], 1),
                    "boss": (1, [0, 0, 0, 0, 1, 1, 1], 6),
                }[args.battle_kind]
                verify_battle_entry(emu, seed, route, kind)
            else:
                verify_events(emu)
                verify_center_recruit_and_tools(emu)
                verify_mart(emu)
        except HarnessError:
            failed = read_run(emu)
            print(f"Node failure frame={emu.frame} scene={scene(emu)} "
                  f"seed={failed.seed} act={failed.act} row={failed.row} "
                  f"lane={failed.lane} phase={failed.phase}", flush=True)
            if emu.framebuffer is not None:
                emu.screenshot(output / "failure.png")
            raise
        print(f"NODE SCENARIOS PASSED at frame {emu.frame}")


if __name__ == "__main__":
    main()
