#!/usr/bin/env python3
"""Headless boot/scene smoke test for the roguelike ROM (`make rl`).

Boots build_rl/pokestadium-rl.z64 in a libretro core using W1's tools/rltest
frontend and checks: title (state 2) -> Menu Select (4) -> hold L + A ->
STATE_ROGUELIKE (0x30) with frames drawn -> B -> Menu Select, repeated
--cycles times (re-entry exercises pool/font lifetime).

RetroPad mapping on mupen64plus_next: RetroPad B = N64 A, RetroPad Y = N64 B.
"""

import argparse
import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

CORE_CANDIDATES = [
    "~/.config/retroarch/cores/mupen64plus_next_libretro.so",
    "/usr/lib/libretro/mupen64plus_next_libretro.so",
    "/usr/lib/x86_64-linux-gnu/libretro/mupen64plus_next_libretro.so",
]


def find_core():
    for c in [os.environ.get("RL_CORE", "")] + CORE_CANDIDATES:
        if c and Path(c).expanduser().is_file():
            return str(Path(c).expanduser())
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rom", default=str(ROOT / "build_rl/pokestadium-rl.z64"))
    ap.add_argument("--map", default=str(ROOT / "build_rl/pokestadium-rl.map"))
    ap.add_argument("--out", default=str(ROOT / ".local/rl_boot_test"))
    ap.add_argument("--cycles", type=int, default=3)
    args = ap.parse_args()

    try:
        from tools.rltest.frontend import Frontend
    except ImportError:
        print("boot_test: SKIP (tools/rltest frontend not available)")
        return 0
    core = find_core()
    if core is None:
        print("boot_test: SKIP (no mupen64plus_next libretro core found; set RL_CORE)")
        return 0

    with Frontend(core, args.rom, args.map, args.out, timeout=900) as emu:
        state = lambda: emu.read_u32("gCurrentGameState")
        rl = emu.symbols.address("gRlMain")

        emu.wait_state(2, frames=3000)
        print(f"title at frame {emu.frame}")
        emu.step(120)
        emu.press("START", frames=4, after=4)
        emu.wait_state(4, frames=1200)
        print(f"menu select at frame {emu.frame}")
        emu.step(180)
        emu.press("B", frames=4, after=4)  # N64 A: dismiss Game Pak Check
        emu.step(240)
        emu.screenshot(f"{args.out}/menu.png")

        for i in range(args.cycles):
            frames_before = emu.read_u32(rl + 0x10)
            emu.hold("L")
            emu.step(4)
            emu.press("B", frames=4, after=4)  # N64 A while holding L
            emu.wait(lambda e: state() == 0x30, frames=600, description="STATE_ROGUELIKE")
            emu.release("L")
            emu.step(90)
            emu.screenshot(f"{args.out}/rl{i}.png")
            drawn = emu.read_u32(rl + 0x10) - frames_before
            visits = emu.read_u32(rl + 4)
            print(f"cycle {i}: RL entered at frame {emu.frame}, visits={visits}, frames drawn={drawn}")
            if visits != i + 1 or drawn == 0:
                print("boot_test: FAIL (overlay not running)")
                return 1
            emu.press("Y", frames=4, after=4)  # N64 B
            emu.wait_state(4, frames=600)
            emu.step(120)
            print(f"cycle {i}: back at Menu Select, frame {emu.frame}")
    print("boot_test: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
