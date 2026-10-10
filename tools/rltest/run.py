"""CLI runner and small JSON scenario interpreter."""

import argparse
import json
import os
from pathlib import Path

from .frontend import Frontend, HarnessError


ROOT = Path(__file__).resolve().parents[2]
CORE_CANDIDATES = (
    Path.home() / ".config/retroarch/cores/mupen64plus_next_libretro.so",
    Path("/usr/lib/libretro/mupen64plus_next_libretro.so"),
    Path.home() / ".config/retroarch/cores/parallel_n64_libretro.so",
    Path("/usr/lib/libretro/parallel_n64_libretro.so"),
)


def default_core():
    configured = os.environ.get("RLTEST_CORE")
    for path in ([Path(configured)] if configured else []) + list(CORE_CANDIDATES):
        if path.is_file():
            return path
    raise HarnessError("No mupen64plus_next or parallel_n64 core found; set RLTEST_CORE")


def run_scenario(emu, path):
    scenario = json.loads(Path(path).read_text())
    for index, action in enumerate(scenario["actions"]):
        op = action["op"]
        try:
            if op == "step":
                emu.step(action.get("frames", 1))
            elif op == "press":
                emu.press(*action["buttons"], controller=action.get("controller", 1),
                          frames=action.get("frames", 2), after=action.get("after", 2))
            elif op == "hold":
                emu.hold(*action["buttons"], controller=action.get("controller", 1))
                emu.step(action.get("frames", 1))
            elif op == "release":
                emu.release(*action.get("buttons", []), controller=action.get("controller", 1))
            elif op == "assert":
                actual = emu.read_u32(action["symbol"])
                if actual != action["equals"]:
                    raise HarnessError(
                        f"{action['symbol']} expected {action['equals']:#x}, got {actual:#x}"
                    )
            elif op == "state":
                emu.wait_state(action["value"], action.get("frames", 1800))
            elif op == "snapshot":
                emu.snapshot(action["path"])
            elif op == "screenshot":
                emu.screenshot(action["path"])
            elif op == "save":
                emu.save(action["path"])
            elif op == "load":
                emu.load(action["path"])
            elif op == "write32":
                emu.write_u32(action["address"], action["value"])
            else:
                raise HarnessError(f"Unknown scenario operation {op!r}")
        except Exception as exc:
            raise HarnessError(f"Scenario action {index} ({op}) failed: {exc}") from exc


def smoke(emu):
    emu.wait_state(0x02, frames=60 * 60)
    print(f"TITLE_SCREEN confirmed at frame {emu.frame}")
    emu.step(120)
    emu.press("START", frames=12, after=30)
    emu.wait_state(0x04, frames=60 * 20)
    emu.step(60)
    emu.press("N64_A", frames=12, after=45)
    print(f"MENU_SELECT confirmed at frame {emu.frame}")
    emu.screenshot(emu.output / "menu-select.png")


def battle_fixture(emu, attempts=80):
    emu.wait_state(0x02, frames=60 * 60)
    emu.step(120)
    emu.press("START", frames=12, after=30)
    emu.wait_state(0x04, frames=60 * 20)
    emu.step(60)
    emu.press("N64_A", frames=12, after=45)
    # Menu Select index 0 is the default. Left selects Battle Now; Down enters
    # its 1P/2P submenu, and three A confirmations reach Battle Now! (1P).
    emu.press("LEFT", frames=12, after=30)
    emu.press("DOWN", frames=12, after=30)
    for _ in range(3):
        emu.press("N64_A", frames=12, after=30)
    emu.wait_state(0x28, frames=60 * 20)
    for _ in range(attempts):
        mons = emu.battle_mons()
        if mons:
            path = emu.output / "battle-now.state"
            emu.save(path)
            emu.screenshot(emu.output / "battle-now.png")
            emu.snapshot(emu.output / "battle-now.rdram")
            print(f"Battle active at frame {emu.frame}; sides: {mons}")
            print(f"Local fixture saved to {path}")
            return
        emu.press("N64_A", frames=12, after=18)
    emu.snapshot(emu.output / "battle-now-incomplete.rdram")
    emu.screenshot(emu.output / "battle-now-incomplete.png")
    raise HarnessError(
        "Battle Now setup did not reach fragment-62 active mons. "
        "Inspect the latest screenshot/RAM snapshot, then adjust menu input."
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("scenario", choices=("smoke", "json", "battle-fixture"))
    parser.add_argument("--core", default=default_core())
    parser.add_argument("--rom", default=os.environ.get("RLTEST_ROM", ROOT / "build/pokestadium-us.z64"))
    parser.add_argument("--map", dest="map_path",
                        default=os.environ.get("RLTEST_MAP", ROOT / "build/pokestadium-us.map"))
    parser.add_argument("--output", default=os.environ.get("RLTEST_OUTPUT", ROOT / ".local/rltest"))
    parser.add_argument("--script", help="JSON action script, required for scenario=json")
    parser.add_argument("--timeout", type=int, default=110)
    args = parser.parse_args()
    if args.scenario == "json" and not args.script:
        parser.error("--script required with scenario=json")
    with Frontend(args.core, args.rom, args.map_path, args.output, timeout=args.timeout) as emu:
        print(f"Loaded {emu.rom_path.name} with {emu.library} ({args.core})")
        if args.scenario == "smoke":
            smoke(emu)
        elif args.scenario == "json":
            run_scenario(emu, args.script)
        elif args.scenario == "battle-fixture":
            battle_fixture(emu)
        print(f"Scenario passed at frame {emu.frame}")


if __name__ == "__main__":
    main()
