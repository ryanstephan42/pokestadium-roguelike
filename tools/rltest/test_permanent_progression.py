"""Native profile UI, earned loss receipts, purchases and cold-process drafts.

The first loss follows ordinary fossil recruitment with unmodified combat.
Funding losses use the explicit legacy one-HP/Growl battle fixture, not
injected meta, terminal outcomes or currency. Ordinary drafts never enable it.
"""

import argparse
import ctypes
import json
from pathlib import Path
import subprocess
import sys
import tempfile

from tools.rltest.frontend import Frontend
from tools.rltest.run import default_core
from tools.rltest.save_roundtrip import boot, save_ram
from tools.rltest.test_custom_battle import Mon
from tools.rltest.test_play_run import (
    enter_roguelike_from_menu, press, read_run, require, run_native_loss_only,
    start_new_run, verify_native_loss, wait_scene,
)


class Meta(ctypes.BigEndianStructure):
    _fields_ = [
        ("unlocked_pools", ctypes.c_uint32),
        ("unlocked_relics", ctypes.c_uint32),
        ("unlocked_events", ctypes.c_uint32),
        ("runs", ctypes.c_uint32), ("wins", ctypes.c_uint32),
        ("best_floor", ctypes.c_uint32), ("ascension", ctypes.c_uint8),
        ("hall_count", ctypes.c_uint8), ("hall", Mon * 6),
        ("currency", ctypes.c_uint32), ("options", ctypes.c_uint32),
        ("last_registration_run", ctypes.c_uint32),
        ("pending_registration_count", ctypes.c_uint32),
        ("registered_species", ctypes.c_uint8 * 19),
        ("pending_species", ctypes.c_uint8 * 19),
        ("last_registered_species", ctypes.c_uint8),
    ]


def read_meta(emu):
    return Meta.from_buffer_copy(emu.read("gRlPlayProfile", ctypes.sizeof(Meta)))


def select(emu, index):
    emu.step(30)
    for _ in range(index):
        press(emu, "DOWN")
    press(emu, "N64_A")


def resolve_pending(emu, register=False):
    """Called at root; consume each receipt through actual A-confirmed UI."""
    if not read_meta(emu).pending_registration_count:
        return
    select(emu, 4)
    wait_scene(emu, 11)
    press(emu, "N64_A")
    wait_scene(emu, 12)
    emu.step(30)
    while read_meta(emu).pending_registration_count:
        meta = read_meta(emu)
        count = sum(byte.bit_count() for byte in meta.pending_species)
        select(emu, 0 if register else count)
        emu.wait(lambda runner: read_meta(runner).pending_registration_count !=
                 meta.pending_registration_count, frames=1800,
                 description="native registration/skip save")
        wait_scene(emu, 12)
        require(read_meta(emu).pending_registration_count ==
                meta.pending_registration_count - 1, "choice consumed twice or not saved")
    press(emu, "N64_B")
    wait_scene(emu, 0)


def draft(emu):
    return (Mon * 6).from_buffer_copy(emu.read("gRlPlayDraft", ctypes.sizeof(Mon) * 6))

def release_to_one(emu, keep_last=False):
    run = read_run(emu)
    row = 0 if run.row == 255 else run.row + 1
    select(emu, run.map.lanes[row] + 2)
    wait_scene(emu, 8)
    while read_run(emu).party_count > 1:
        select(emu, 0 if keep_last else 1)
        select(emu, 1)
        select(emu, 1)
    press(emu, "N64_B")
    wait_scene(emu, 2)


def ordinary_earn(emu):
    boot(emu)
    enter_roguelike_from_menu(emu)
    start_new_run(emu, 99, 0)
    select(emu, 3)
    wait_scene(emu, 8)
    require(emu.read_u32("gRlSceneEvent") == 0, "seed 99 did not offer ordinary fossil event")
    press(emu, "N64_A")
    wait_scene(emu, 2)
    require(read_run(emu).party_count == 4 and read_run(emu).party[3].species == 140,
            "ordinary fossil event did not recruit canonical Kabuto")
    release_to_one(emu, keep_last=True)
    run = read_run(emu)
    require(run.party[0].species == 140 and run.party[0].level == 20 and
            run.map.nodes[7].type == 0, "ordinary fossil-to-trainer route changed")
    for _ in range(3):
        press(emu, "DOWN")
    verify_native_loss(emu, expected_party=1)
    require(emu.read_u32("gRlPlayTestMode") == 0, "ordinary earn used fixture mode")


def phase(directory, part):
    with Frontend(default_core(), Path("build_rl/pokestadium-rl.z64"),
                  Path("build_rl/pokestadium-rl.map"), output=directory,
                  options={"mupen64plus-pak1": "memory"}, timeout=3600) as emu:
        if part != "earn":
            pointer, initial = save_ram(emu)
            checkpoint = directory / "funding-save.bin"
            source = checkpoint if part == "shop" and checkpoint.exists() else directory / "permanent-save.bin"
            data = source.read_bytes()
            require(len(data) == len(initial), "aggregate save size changed")
            ctypes.memmove(pointer, data, len(data))
            boot(emu)
            enter_roguelike_from_menu(emu)
        if part == "earn":
            ordinary_earn(emu)
            meta = read_meta(emu)
            require(meta.runs == 1 and meta.currency == 5, "native loss payout wrong")
            require(meta.pending_registration_count == 1 and
                    any(meta.pending_species), "native final team earned no non-base choice")
            _, data = save_ram(emu)
            (directory / "permanent-save.bin").write_bytes(data)
            print("ORDINARY NATIVE EARN PASSED: fossil recruit, confirmed releases, "
                  "unmodified native loss, pending Kabuto, five coins")
            return
        if part == "shop":
            meta = read_meta(emu)
            require(1 <= meta.runs <= 26 and meta.currency == meta.runs * 5,
                    "cold load lost completed funding runs")
            if meta.runs == 1:
                require(meta.pending_registration_count == 1, "cold load lost pending choices")
                resolve_pending(emu, register=True)
            require(any(read_meta(emu).registered_species), "registration did not persist")
            # Complete meaningful native defeats until the real economy pays
            # for all three implemented option categories. Never write meta.
            for number in range(meta.runs + 1, 27):
                resolve_pending(emu, register=number != 3)
                start_new_run(emu, 0x13579BDF + (1 if number == 2 else 0), 3)
                release_to_one(emu)
                verify_native_loss(emu, expected_party=1)
                require(read_meta(emu).runs == number and
                        read_meta(emu).currency == number * 5,
                        f"native run {number} paid or recorded twice")
                print(f"native payout {number}/26", flush=True)
                (directory / "funding-save.bin").write_bytes(save_ram(emu)[1])
            resolve_pending(emu)
            require(read_meta(emu).currency == 130, "earned balance not 130")
            # Retain a real active suspension while purchasing at profile root.
            start_new_run(emu, 8, 0)
            suspended = bytes(read_run(emu))
            select(emu, read_run(emu).map.lanes[0])
            emu.wait_state(4, frames=600)
            from tools.rltest.test_play_run import reenter_roguelike
            reenter_roguelike(emu)
            select(emu, 4)
            wait_scene(emu, 11)
            for index, cost in ((2, 30), (5, 50), (9, 50)):
                before = read_meta(emu).currency
                select(emu, index)
                emu.wait(lambda runner: read_meta(runner).currency != before,
                         frames=1800, description="native permanent purchase save")
                require(read_meta(emu).currency == before - cost, "purchase debit wrong")
                require(bytes(read_run(emu)) == suspended, "profile edit mutated resident run")
            require(read_meta(emu).options & ((1 << 1) | (1 << 4) | (1 << 8)) ==
                    ((1 << 1) | (1 << 4) | (1 << 8)), "implemented options missing")
            emu.screenshot(directory / "permanent-shop.png")
            press(emu, "N64_B")
            select(emu, 1)
            wait_scene(emu, 2)
            require(bytes(read_run(emu)) == suspended, "purchase erased or changed suspension")
            select(emu, read_run(emu).map.lanes[0])
            emu.wait_state(4, frames=600)
            _, data = save_ram(emu)
            (directory / "permanent-save.bin").write_bytes(data)
            print("NATIVE SHOP PASSED: 26 real losses funded loadout/theme/kit; suspend retained")
            return
        require(read_meta(emu).currency == 0 and read_meta(emu).options & 274 == 274,
                "cold process lost purchases")
        base_species = {
            1, 4, 7, 10, 13, 16, 19, 21, 23, 25, 27, 29, 32, 37, 39, 41,
            43, 46, 48, 50, 52, 56, 60, 63, 66, 69, 74, 79, 84, 90, 104,
            116, 118, 129,
        }
        meta = read_meta(emu)
        registered = {species for species in range(1, 152)
                      if meta.registered_species[(species - 1) >> 3] &
                      (1 << ((species - 1) & 7))}
        observed = set()
        for seed in range(1, 65):
            emu.write_u32("gRlPlayTestMode", 0)
            emu.write_u32("gRlPlayTestSeed", seed)
            press(emu, "N64_A")
            wait_scene(emu, 1)
            selected = set(emu.read("gRlPlayDraftSpecies", 6))
            require(len(selected) == 6 and selected <= base_species | registered,
                    f"ordinary seed {seed} selected locked or repeated species")
            observed.update(selected)
            press(emu, "N64_B")
            wait_scene(emu, 0)
        require(observed & base_species and 140 in observed,
                "ordinary seeded drafts never offered registered Kabuto alongside base species")
        # Real settings controls, then ordinary weighted/canonical draft.
        select(emu, 3)
        wait_scene(emu, 10)
        select(emu, 4)
        select(emu, 5)
        press(emu, "N64_B")
        emu.write_u32("gRlPlayTestMode", 0)
        emu.write_u32("gRlPlayTestSeed", 123)
        press(emu, "N64_A")
        wait_scene(emu, 1)
        run = read_run(emu)
        require(run.money == 100 and run.bag[0] == 1, "Potion Cache not applied")
        forest = {1, 10, 11, 12, 13, 14, 15, 43, 44, 45, 46, 47,
                  48, 49, 69, 70, 71, 102, 103, 123, 127}
        before = draft(emu)
        require(all(mon.species in forest and mon.level == 30 and mon.dvs == 0x9999
                    and not any(mon.stat_exp) for mon in before),
                "ordinary themed draft inherited boosted source stats")
        changed = False
        rentals = json.loads(Path("build/rl_gen/rl_rosters.json").read_text())["rental_sets"]
        prime = next(pool for pool in rentals if pool["id"] == "prime")
        legal_moves = {mon["species"]: tuple(mon["moves"]) for mon in prime["mons"]}
        for slot in range(6):
            press(emu, "RIGHT")
            after = draft(emu)
            changed |= tuple(after[slot].moves) != tuple(before[slot].moves)
            require(after[slot].species == before[slot].species and
                    after[slot].dvs == before[slot].dvs and
                    tuple(after[slot].stat_exp) == tuple(before[slot].stat_exp),
                    "move option changed stats")
            require(tuple(after[slot].moves) == legal_moves[after[slot].species] and
                    tuple(after[slot].pp) == tuple(after[slot].max_pp),
                    "variant is not the matching canonical Prime rental loadout")
            if slot < 5:
                press(emu, "DOWN")
        require(changed, "purchased canonical loadout had no actual effect")
        emu.screenshot(directory / "permanent-draft.png")
        print("NATIVE OPTIONS COLD RELOAD PASSED: actual theme, variant and equal-budget kit")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--phase", choices=("earn", "shop", "reload"))
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.phase:
        phase(args.output, args.phase)
        return
    root = Path(".local/rltest")
    root.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="permanent-", dir=root) as temporary:
        for part in ("earn", "shop", "reload"):
            subprocess.run([sys.executable, "-m", "tools.rltest.test_permanent_progression",
                            "--phase", part, "--output", temporary], check=True)
    print("PERMANENT PROGRESSION PASSED")


if __name__ == "__main__":
    main()
