"""W3 exact-team launch/result scenario using the W1 libretro frontend."""

import argparse
import json
from pathlib import Path

from tools.rltest.frontend import Frontend, HarnessError


def run(core, rom, map_path, output):
    with Frontend(core, rom, map_path, output=output, timeout=600) as emu:
        emu.wait_state(2, frames=2400)
        emu.step(120)
        before = emu.read_u32("gRlBattleCount")
        emu.write_u32("gRlBattleTestRequest", 1)
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
        emu.wait(lambda runner: runner.read_u32("gRlBattleCount") == before + 1,
                 frames=18000, interval=10, description="custom battle completion")
        result = list(emu.read("gRlLastBattleResult", 3))
        if result != [1, 3, 3]:
            raise HarnessError(f"Expected win/3 player/3 enemy, got {result}")
        if emu.read_u32("gRlBattleActive") != 0:
            raise HarnessError("Custom battle active flag not cleared")
        if emu.read_u32("gCurrentGameState") != 2:
            raise HarnessError("Custom battle did not return to chosen Title state")
        evidence = {"frame": emu.frame, "live": live, "result": result,
                    "battle_count": emu.read_u32("gRlBattleCount")}
        Path(output).mkdir(parents=True, exist_ok=True)
        (Path(output) / "custom-battle.json").write_text(json.dumps(evidence, indent=2) + "\n")
        print("CUSTOM BATTLE PASSED", json.dumps(evidence))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--core", default="/usr/lib/libretro/mupen64plus_next_libretro.so")
    parser.add_argument("--rom", default="build/roguelike/pokestadium-us.z64")
    parser.add_argument("--map", dest="map_path", default="build/roguelike/pokestadium-us.map")
    parser.add_argument("--output", default="build/w3-harness")
    args = parser.parse_args()
    run(args.core, args.rom, args.map_path, args.output)


if __name__ == "__main__":
    main()
