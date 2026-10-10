"""Exercise the native roguelike UI demo through Menu Select."""

import argparse
import json
import re
import struct
from pathlib import Path

from tools.rltest.frontend import Frontend, HarnessError


def object_bss(map_path, object_name):
    pattern = rf"^\s*\.bss\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)\s+\S*/{re.escape(object_name)}$"
    match = re.search(pattern, Path(map_path).read_text(), re.MULTILINE)
    if not match:
        raise HarnessError(f"No BSS section for {object_name}")
    return int(match[1], 16), int(match[2], 16)


def demo_state(emu, bss):
    data = emu.read(*bss)
    title = emu.read_u32(emu.symbols.address("rl_text") + 4)
    for offset in range(0, len(data) - 23, 4):
        fields = struct.unpack_from(">6I", data, offset)
        if fields[0] == title and fields[2:4] == (10, 5):
            return fields[4], fields[5]
    raise HarnessError("Initialized demo list not found")


def icon_cache(emu, bss):
    data = emu.read(*bss)
    match = re.search(r"^\s*0x([0-9a-fA-F]+)\s+pokemon_icons_ROM_START\s*=",
                      emu.symbols.path.read_text(), re.MULTILINE)
    if not match:
        raise HarnessError("Pokemon icon ROM symbol not found")
    rom = int(match[1], 16)
    for offset in range(0, len(data), 4):
        pointer = struct.unpack_from(">I", data, offset)[0]
        if 0x80000000 <= pointer < 0x80800000:
            if emu.read_u32(pointer + 4) == rom and emu.read_u32(pointer + 12) >= 151:
                return tuple(emu.read_u32(pointer + 16 + index * 16 + 8)
                             for index in (0, 3, 6))
    raise HarnessError("Pokemon icon archive not found")


def require(actual, expected, label):
    if actual != expected:
        raise HarnessError(f"{label}: expected {expected!r}, got {actual!r}")


def run(emu, map_path, output):
    bss = object_bss(map_path, "rl_ui.o")
    emu.wait_state(2)
    emu.step(120)
    emu.press("START", frames=6, after=120)
    emu.wait_state(4)
    # The first state-4 screen is Game Pak Check, not Menu Select.
    emu.press("B", frames=6, after=120)
    emu.step(120)
    emu.screenshot(output / "menu.png")
    print(f"Menu Select at frame {emu.frame}", flush=True)
    visits = []
    for visit in range(2):
        # Mupen's RetroPad B/Y map to native N64 A/B; L must survive the fade.
        emu.hold("L")
        emu.press("B", frames=6, after=120)
        emu.wait_state(0x30, frames=600)
        emu.release("L")
        emu.step(240)
        require(demo_state(emu, bss), (0, 0), "Initial list")
        emu.screenshot(output / f"entry-{visit + 1}.png")
        print(f"RL entry {visit + 1} at frame {emu.frame}", flush=True)
        for cursor in range(1, 10):
            emu.press("DOWN", frames=4, after=4)
            require(demo_state(emu, bss), (cursor, max(0, cursor - 4)), "Down/scroll")
            if cursor in (1, 2):
                emu.screenshot(output / f"icon-{cursor}-visit-{visit + 1}.png")
            if cursor == 5:
                emu.press("B", frames=4, after=4)
                require(demo_state(emu, bss), (5, 1), "Disabled selection")
        emu.screenshot(output / f"scroll-{visit + 1}.png")
        emu.press("LEFT", frames=4, after=4)
        require(demo_state(emu, bss), (4, 4), "Page left")
        emu.press("RIGHT", frames=4, after=4)
        require(demo_state(emu, bss), (9, 5), "Page right")
        cache = icon_cache(emu, bss)
        if not all(cache) or len(set(cache)) != 3:
            raise HarnessError(f"Missing cached icons: {cache!r}")
        pool_bss = object_bss(map_path, "memory_main.o")
        available = emu.read_u32(pool_bss[0] + 0x1C)
        for _ in range(12):
            for button in ("UP", "UP", "DOWN", "DOWN"):
                emu.press(button, frames=4, after=4)
        emu.step(120)
        require(icon_cache(emu, bss), cache, "Icon cache reuse")
        require(emu.read_u32(pool_bss[0] + 0x1C), available, "Stable pool allocation")
        if visits:
            require(available, visits[0]["pool_available"], "Pool reclaimed on re-entry")
        emu.press("B", frames=4, after=8)
        emu.screenshot(output / f"selected-{visit + 1}.png")
        visits.append({"frame": emu.frame, "icon_pointers": cache, "pool_available": available})
        emu.press("Y", frames=4, after=120)
        emu.wait_state(4, frames=600)
        emu.step(120)
        print(f"Returned to Menu Select at frame {emu.frame}", flush=True)
    emu.screenshot(output / "returned.png")
    (output / "ui-results.json").write_text(json.dumps(visits, indent=2) + "\n")
    print("UI DEMO PASSED", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--core", default="/usr/lib/libretro/mupen64plus_next_libretro.so")
    parser.add_argument("--rom", default="build_rl/pokestadium-rl.z64")
    parser.add_argument("--map", default="build_rl/pokestadium-rl.map")
    parser.add_argument("--output", default=".local/rltest/ui")
    parser.add_argument("--timeout", type=int, default=180)
    args = parser.parse_args()
    output = Path(args.output)
    with Frontend(args.core, args.rom, args.map, output, timeout=args.timeout) as emu:
        try:
            run(emu, args.map, output)
        except HarnessError:
            print(f"Failure at frame {emu.frame}, state {emu.read_u32('gCurrentGameState'):#x}", flush=True)
            if emu.framebuffer is not None:
                emu.screenshot(output / "failure.png")
            raise


if __name__ == "__main__":
    main()
