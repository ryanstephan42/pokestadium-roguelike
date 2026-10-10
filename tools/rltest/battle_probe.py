"""Interactive, local-only battle navigation probe using the W1 frontend."""

import argparse
from pathlib import Path

from tools.rltest.frontend import Frontend


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--core", default="/usr/lib/libretro/mupen64plus_next_libretro.so")
    parser.add_argument("--load")
    args = parser.parse_args()
    output = Path("build/rl-battle-probe")
    with Frontend(args.core, "build/pokestadium-us.z64",
                  "build/pokestadium-us.map", output, timeout=1800) as emu:
        if args.load:
            emu.load(output / args.load)
            emu.step(2)
        else:
            emu.wait_state(2, frames=2400)
        while True:
            emu.screenshot(output / "screen.png")
            print("frame", emu.frame, "state", hex(emu.read_u32("gCurrentGameState")),
                  "mons", emu.battle_mons(), flush=True)
            if emu.battle_mons():
                print("moves", list(emu.read(emu.battle_mons()[0]["side"] + 0x6AB, 4)),
                      flush=True)
            command = input("buttons [frames] / wait frames / save name / quit: ").split()
            if not command:
                continue
            if command[0] == "quit":
                break
            if command[0] == "wait":
                emu.step(int(command[1]))
            elif command[0] == "c":
                directions = {"up": (0, -32767), "down": (0, 32767),
                              "left": (-32767, 0), "right": (32767, 0)}
                emu.stick(*directions[command[1]], right=True)
                emu.step(12)
                emu.stick(right=True)
                emu.step(120)
            elif command[0] == "save":
                emu.save(output / command[1])
            else:
                after = int(command.pop()) if command[-1].isdigit() else 120
                emu.hold(*command)
                emu.step(12)
                controller = emu.read_u32("gPlayer1Controller")
                print("native buttons", hex(emu.read_u16(controller + 6)), flush=True)
                emu.release()
                emu.step(after)


if __name__ == "__main__":
    main()
