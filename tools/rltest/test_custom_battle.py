"""W3 exact-team launch/result scenario using the W1 libretro frontend."""

import argparse
import ctypes
import json
from pathlib import Path

from tools.rltest.frontend import Frontend, HarnessError


class Mon(ctypes.BigEndianStructure):
    _fields_ = [
        ("species", ctypes.c_uint8), ("level", ctypes.c_uint8),
        ("growth", ctypes.c_uint8), ("fainted", ctypes.c_uint8),
        ("exp", ctypes.c_uint32), ("dvs", ctypes.c_uint16),
        ("stat_exp", ctypes.c_uint16 * 5), ("base_stats", ctypes.c_uint8 * 5),
        ("current_stats", ctypes.c_uint16 * 5), ("moves", ctypes.c_uint8 * 4),
        ("pp", ctypes.c_uint8 * 4), ("max_pp", ctypes.c_uint8 * 4),
        ("cur_hp", ctypes.c_uint16), ("max_hp", ctypes.c_uint16),
        ("status", ctypes.c_uint8), ("held_relic", ctypes.c_uint8),
        ("nickname", ctypes.c_char * 11),
    ]


class Result(ctypes.BigEndianStructure):
    _fields_ = [("outcome", ctypes.c_uint8), ("player_count", ctypes.c_uint8),
                ("enemy_count", ctypes.c_uint8), ("indices", ctypes.c_uint8 * 6),
                ("player", Mon * 6), ("enemy", Mon * 6)]


def persistent(mon):
    return [mon.cur_hp, mon.pp[0], mon.status, mon.fainted, mon.held_relic]


def run(core, rom, map_path, output):
    with Frontend(core, rom, map_path, output=output, timeout=600) as emu:
        emu.wait_state(2, frames=2400)
        emu.step(120)
        before = emu.read_u32("gRlBattleCount")
        if emu.read_u32("gRlBattleTestMonSize") != ctypes.sizeof(Mon):
            raise HarnessError("Canonical RlMon ABI mismatch")
        battles = []
        for request in (1, 2):
            battles.append(battle(emu, request, before + request))
        evidence = {"frame": emu.frame, "battles": battles,
                    "battle_count": emu.read_u32("gRlBattleCount")}
        Path(output).mkdir(parents=True, exist_ok=True)
        (Path(output) / "custom-battle.json").write_text(json.dumps(evidence, indent=2) + "\n")
        print("CUSTOM BATTLE PASSED", json.dumps(evidence))


def battle(emu, request, count):
    initial_hp, initial_pp = 72, 9
    if request == 2:
        previous = Result.from_buffer_copy(emu.read("gRlLastBattleResult", ctypes.sizeof(Result)))
        initial_hp, initial_pp = previous.player[0].cur_hp, previous.player[0].pp[0]
    emu.write_u32("gRlBattleTestRequest", request)
    emu.press("START", frames=10, after=10)
    emu.wait(lambda runner: runner.read_u32("gRlBattleActive") == 1,
             frames=2400, description="custom battle launch")
    live = emu.wait_battle(frames=2400)
    if live[0]["species"] != 150 or live[0]["level"] != 100:
        raise HarnessError(f"Wrong custom player: {live[0]}")
    if live[1]["species"] != 10 or live[1]["level"] != 5:
        raise HarnessError(f"Wrong custom enemy: {live[1]}")
    if emu.read(live[0]["side"] + 0x68C + 0x1F, 4) != bytes([94, 0, 0, 0]):
        raise HarnessError("Player custom moves were not preserved")
    if emu.read(live[1]["side"] + 0x68C + 0x1F, 4) != bytes([33, 0, 0, 0]):
        raise HarnessError("Enemy custom moves were not preserved")
    if (live[0]["hp"], live[0]["status"], live[0]["pp"][0]) != (initial_hp, 0x40, initial_pp):
        raise HarnessError(f"Persistent state was healed/overwritten: {live[0]}")
    emu.wait(lambda runner: runner.read_u32("gRlBattleCount") == count,
             frames=18000, interval=10, description="custom battle completion")
    result = list(emu.read("gRlLastBattleResult", 3))
    if result != [1, 2, 3]:
        raise HarnessError(f"Expected win/2 player/3 enemy, got {result}")
    # Three identical species, but the fainted original slot zero is excluded.
    if emu.read(emu.resolve("gRlLastBattleResult") + 3, 2) != bytes([1, 2]):
        raise HarnessError("Duplicate-species player identity was not preserved")
    if emu.read_u32("gRlBattleActive") != 0:
        raise HarnessError("Custom battle active flag not cleared")
    if emu.read_u32("gCurrentGameState") != 2:
        raise HarnessError("Custom battle did not return to chosen Title state")
    stored = Result.from_buffer_copy(emu.read("gRlLastBattleResult", ctypes.sizeof(Result)))
    actual = [persistent(stored.player[i]) for i in range(2)]
    if (not 0 < actual[0][0] <= initial_hp or not 0 <= actual[0][1] <= initial_pp - 3
            or actual[0][2:] != [0x40, 0, 2] or actual[1] != [73, 10, 0x40, 0, 3]):
        raise HarnessError(f"Post-battle state mismatch: {actual}")
    if any(not stored.enemy[i].fainted or stored.enemy[i].cur_hp for i in range(3)):
        raise HarnessError("Native opponent fainting was not persisted")
    emu.step(120)
    return {"live": live, "result": result, "persisted": actual,
            "original_indices": list(stored.indices[:2])}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--core", default="/usr/lib/libretro/mupen64plus_next_libretro.so")
    parser.add_argument("--rom", default="build_rl/pokestadium-rl.z64")
    parser.add_argument("--map", dest="map_path", default="build_rl/pokestadium-rl.map")
    parser.add_argument("--output", default="build/w3-harness")
    args = parser.parse_args()
    run(args.core, args.rom, args.map_path, args.output)


if __name__ == "__main__":
    main()
