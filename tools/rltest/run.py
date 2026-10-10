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


def battle_fixture(emu):
    """Boot a Poke Cup rental battle and save live-battle fixtures."""
    emu.wait_state(0x02, frames=60 * 60)
    emu.step(120)
    emu.press("START", frames=12, after=30)
    emu.wait_state(0x04, frames=60 * 20)
    emu.step(60)
    emu.press("N64_A", frames=12, after=45)
    emu.wait_state(0x04, frames=60 * 20)
    emu.screenshot(emu.output / "battle-menu-select.png")

    # Enter Stadium, select Poke Cup, Poke Ball, then Battle.
    emu.press("N64_A", frames=12, after=180)
    emu.wait_state(0x10, frames=60 * 20)
    emu.press("N64_A", frames=12, after=240)
    emu.wait_state(0x20, frames=60 * 20)
    emu.press("N64_A", frames=12, after=150)
    emu.press("N64_A", frames=12, after=180)
    emu.press("N64_A", frames=12, after=180)
    emu.press("N64_A", frames=12, after=150)
    emu.screenshot(emu.output / "battle-rentals.png")

    # Rent Bulbasaur and Ivysaur, navigate to Squirtle, then take the next
    # three distinct rentals. Each rental uses a details screen and a Yes press.
    for _ in range(2):
        emu.press("N64_A", frames=12, after=150)
        emu.press("N64_A", frames=12, after=180)
    for button in ("LEFT", "LEFT", "DOWN", "DOWN"):
        emu.press(button, frames=12, after=120)
    for _ in range(4):
        emu.press("N64_A", frames=12, after=150)
        emu.press("N64_A", frames=12, after=180)
    emu.press("N64_A", frames=12, after=180)  # OK to Register
    emu.screenshot(emu.output / "battle-roster-registered.png")

    # Finish tournament presentation and enter the three-Pokemon pre-battle
    # selection. Native B selects Bulbasaur; the right stick selects Ivy/Squirtle.
    emu.press("START", frames=12, after=180)
    emu.step(240)
    emu.press("N64_A", frames=12, after=180)
    emu.step(120)
    emu.press("N64_B", frames=12, after=120)
    for x, y in ((-32767, 0), (0, -32767)):
        emu.stick(x, y, right=True)
        emu.step(12)
        emu.stick(right=True)
        emu.step(120)
    emu.screenshot(emu.output / "battle-team-selected.png")

    emu.press("START", frames=12, after=1200)
    emu.screenshot(emu.output / "battle-confirmation.png")
    emu.press("N64_A", frames=12, after=1200)
    mons = emu.wait_battle(frames=60 * 60)
    expected_moves = [73, 92, 34, 75]
    if mons[0]["species"] != 1 or mons[0]["moves"] != expected_moves:
        emu.snapshot(emu.output / "battle-now-incomplete.rdram")
        emu.screenshot(emu.output / "battle-now-incomplete.png")
        raise HarnessError(f"Unexpected lead Pokemon in fresh battle: {mons[0]}")

    path = emu.output / "battle-now.state"
    emu.save(path)
    emu.snapshot(emu.output / "battle-now.rdram")
    emu.screenshot(emu.output / "battle-now.png")
    print(f"Battle active at frame {emu.frame}; sides: {mons}")
    print(f"Local fixture saved to {path}")

    # W2's live HP/PP verifier expects the attack-selection menu as its entry.
    emu.press("N64_A", frames=12, after=120)
    emu.step(240)
    emu.save(emu.output / "battle-now-attack-menu.state")
    emu.snapshot(emu.output / "battle-now-attack-menu.rdram")
    emu.screenshot(emu.output / "battle-now-attack-menu.png")


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
