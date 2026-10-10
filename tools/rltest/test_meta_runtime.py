"""Real native loss records progression once; cold process reads the same Pak."""

import argparse
import ctypes
from pathlib import Path
import subprocess
import sys
import tempfile

from tools.rltest.frontend import Frontend
from tools.rltest.run import default_core
from tools.rltest.save_roundtrip import boot, native_command, save_ram
from tools.rltest.test_play_run import (
    SCENE_MAP, press, read_run, enter_roguelike_from_menu, reenter_roguelike, require,
    run_native_loss_only, start_new_run, wait_scene,
)


def load_profile(emu, from_exit=False):
    if from_exit:
        emu.press("N64_A", frames=6, after=120)
        emu.step(120)
    result = native_command(emu, 1)
    require(result[1] == 0, f"native meta load failed: {result}")
    mailbox = emu.resolve("gRlSaveProbe")
    masks = tuple(emu.read_u32(mailbox + offset) for offset in (36, 40, 44, 48))
    return result, masks


def phase(directory, reload_only):
    with Frontend(default_core(), Path("build_rl/pokestadium-rl.z64"),
                  Path("build_rl/pokestadium-rl.map"), output=directory,
                  options={"mupen64plus-pak1": "memory"}, timeout=1800) as emu:
        if reload_only:
            pointer, initial = save_ram(emu)
            data = (directory / "meta-save.bin").read_bytes()
            require(len(data) == len(initial), "save RAM changed size")
            ctypes.memmove(pointer, data, len(data))
            boot(emu)
        else:
            run_native_loss_only(emu)
        result, masks = load_profile(emu, from_exit=not reload_only)
        require(result[4:8] == (1, 0, 1, 0),
                f"terminal loss did not persist exactly once: {result}")
        require(masks == (1, 255, 255, 0),
                f"new profile was reset or granted locked content: {masks}")
        if reload_only:
            enter_roguelike_from_menu(emu)
            start_new_run(emu, 99, 0)
            for _ in range(3):
                press(emu, "DOWN")
            press(emu, "N64_A")
            wait_scene(emu, 8)
            emu.step(30)
            event = emu.read_u32("gRlSceneEvent")
            require(event < 8, f"new profile offered locked event {event}")
            emu.screenshot(directory / "locked-profile-event.png")
            press(emu, "START", after=80)
            emu.wait_state(4, frames=600)
            reenter_roguelike(emu)
            start_new_run(emu, 8, 0)
            run = read_run(emu)
            require(run.ascension == 0, "new profile selected locked ascension")
            # Exit through map suspension, then reload without recording a run.
            for _ in range(run.map.lanes[0]):
                press(emu, "DOWN")
            press(emu, "N64_A")
            emu.wait_state(4, frames=600)
            again, again_masks = load_profile(emu, from_exit=True)
            require(again[4:7] == (1, 0, 1) and again_masks == masks,
                    "re-entry counted the terminal loss twice or reset profile")
            print(f"META COLD RELOAD PASSED: one native loss, retained unlock masks, "
                  f"locked profile selected permitted event {event}")
        else:
            _, data = save_ram(emu)
            (directory / "meta-save.bin").write_bytes(data)
            print("META NATIVE LOSS PASSED: recorded once through gameplay")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--phase", choices=("loss", "reload"))
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.phase:
        phase(args.output, args.phase == "reload")
        return
    root = Path(".local/rltest")
    root.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="meta-runtime-", dir=root) as temporary:
        for part in ("loss", "reload"):
            subprocess.run(
                [sys.executable, "-m", "tools.rltest.test_meta_runtime",
                 "--phase", part, "--output", temporary], check=True)
    print("META RUNTIME PASSED")


if __name__ == "__main__":
    main()
