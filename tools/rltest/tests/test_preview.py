import os
import subprocess
import tempfile
import unittest
from pathlib import Path

from tools.rltest.preview_capture import metrics


ROOT = Path(__file__).resolve().parents[3]


class PreviewTests(unittest.TestCase):
    def test_frame_metrics(self):
        self.assertEqual(metrics(bytes(300))["black"], 1)
        self.assertEqual(metrics(bytes([255]) * 300)["white"], 1)
        self.assertEqual(metrics(bytes([255, 0, 0]) * 100)["colored"], 1)
        with self.assertRaises(ValueError):
            metrics(b"")

    def test_isolated_launch(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            bin_dir = tmp / "bin"
            bin_dir.mkdir()
            stub = bin_dir / "retroarch"
            stub.write_text(
                '#!/bin/sh\nprintf "%s\\n" "$XDG_CONFIG_HOME" "$SDL_VIDEODRIVER" '
                '"${WAYLAND_DISPLAY-unset}" "$@"\n')
            stub.chmod(0o755)
            rom = tmp / "test.z64"
            core = tmp / "mupen64plus_next_libretro.so"
            rom.touch()
            core.touch()
            profile = tmp / "profile"
            env = {
                **os.environ,
                "PATH": f"{bin_dir}:{os.environ['PATH']}",
                "WAYLAND_DISPLAY": "wayland-test",
                "DISPLAY": ":test",
                "RL_PREVIEW_ROM": str(rom),
                "RL_PREVIEW_CORE": str(core),
                "RL_PREVIEW_PROFILE": str(profile),
            }
            for key in ("RL_PREVIEW_RDP", "RL_PREVIEW_RSP",
                        "RL_PREVIEW_INPUT", "RL_PREVIEW_PAD"):
                env.pop(key, None)
            result = subprocess.run(
                [str(ROOT / "scripts/rl-preview.sh"), "--max-frames=1"],
                env=env, text=True, capture_output=True, check=True,
            )
            lines = result.stdout.splitlines()
            self.assertEqual(lines[:3], [str(profile / "xdg"), "x11", "unset"])
            self.assertIn(str(rom), lines)
            self.assertIn("--max-frames=1", lines)
            config = (profile / "retroarch.cfg").read_text()
            for setting in (
                'config_save_on_exit = "false"',
                'global_core_options = "true"',
                'auto_overrides_enable = "false"',
                'auto_remaps_enable = "false"',
                'input_autodetect_enable = "true"',
                'input_joypad_driver = "sdl2"',
                'video_context_driver = "glx"',
                f'core_options_path = "{profile}/core-options.cfg"',
                f'savefile_directory = "{profile}/saves"',
            ):
                self.assertIn(setting, config)
            self.assertNotIn(str(Path.home() / ".config"), config)
            self.assertIn('mupen64plus-rdp-plugin = "angrylion"',
                          (profile / "core-options.cfg").read_text())
            bindings = dict(
                line.split(" = ", 1) for line in config.splitlines()
                if line.startswith("input_player1_r_")
            )
            self.assertEqual(bindings, {
                "input_player1_r_y_minus_axis": '"+3"',
                "input_player1_r_y_plus_axis": '"-3"',
                "input_player1_r_y_minus": '"i"',
                "input_player1_r_y_plus": '"k"',
                "input_player1_r_x_minus": '"j"',
                "input_player1_r_x_plus": '"l"',
            })


if __name__ == "__main__":
    unittest.main()
