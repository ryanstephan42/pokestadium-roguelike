import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch
from pathlib import Path

from tools.rltest.frontend import HarnessError
from tools.rltest.story_probe import (
    FIELDS, finish_command, foreign_note, map_value, probe_deadline, story_owned_changes,
)


class StoryHarnessTests(unittest.TestCase):
    def test_mailbox_and_execution_hold(self):
        self.assertEqual(len(FIELDS), 47)
        self.assertEqual(len(set(FIELDS)), len(FIELDS))
        result = subprocess.run([sys.executable, "tools/rltest/story_probe.py"],
                                capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("No emulator launched: --execute is required", result.stderr)

    def test_layout_symbols(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "probe.map"
            path.write_text("  0x8011d510 gScProbe\n"
                            "  0x80140200 heap_BSS_START = .\n")
            self.assertEqual(map_value(path, "gScProbe"), 0x8011D510)
            self.assertEqual(map_value(path, "heap_BSS_START"), 0x80140200)
            with self.assertRaises(HarnessError):
                map_value(path, "missing")

    def test_trigger_held_until_command_finishes(self):
        emu = Mock()
        with patch("tools.rltest.story_probe.read_probe", return_value={"command": 0}):
            self.assertEqual(finish_command(emu), {"command": 0})
        calls = [call[0] for call in emu.mock_calls]
        self.assertEqual(calls, ["wait", "release", "wait_state", "step"])
        emu.release.assert_called_once_with("L")

    def test_watchdog_cancelled_on_error(self):
        with patch("tools.rltest.story_probe.threading.Timer") as timer:
            with self.assertRaisesRegex(HarnessError, "expected"):
                with probe_deadline(Mock(), Path(".")):
                    raise HarnessError("expected")
        timer.return_value.start.assert_called_once()
        timer.return_value.cancel.assert_called_once()

    def test_note_ownership(self):
        before = bytearray(32768)
        for page in range(5, 128):
            before[256 + page * 2:258 + page * 2] = b"\0\3"
        before = foreign_note(before, 0)
        self.assertEqual(before[768:774], b"NPSE01")
        after = bytearray(before)
        note = 800
        after[note:note + 6] = b"SC01RL"
        after[note + 6:note + 8] = struct.pack(">H", 6)
        for page in range(6, 12):
            after[256 + page * 2:258 + page * 2] = struct.pack(">H", page + 1 if page < 11 else 1)
            after[512 + page * 2:514 + page * 2] = after[256 + page * 2:258 + page * 2]
        after[6 * 256] ^= 1
        self.assertEqual(story_owned_changes(before, after, 0), list(range(6, 12)))
        damaged = bytearray(after)
        damaged[5 * 256] ^= 1
        with self.assertRaisesRegex(HarnessError, "unowned/RL01"):
            story_owned_changes(before, damaged, 0)
        damaged = bytearray(after)
        damaged[768] ^= 1
        with self.assertRaises(HarnessError):
            story_owned_changes(before, damaged, 0)
        damaged = bytearray(after)
        damaged[256 + 11 * 2:258 + 11 * 2] = struct.pack(">H", 6)
        with self.assertRaisesRegex(HarnessError, "Invalid story Pak chain"):
            story_owned_changes(before, damaged, 0)

    def test_full_fixture(self):
        data = bytearray(32768)
        for page in range(5, 128):
            data[256 + page * 2:258 + page * 2] = b"\0\3"
        full = foreign_note(data, 0, full=True)
        self.assertEqual(full[256 + 127 * 2:258 + 127 * 2], b"\0\1")
        self.assertFalse(any(full[256 + page * 2:258 + page * 2] == b"\0\3"
                             for page in range(5, 128)))


if __name__ == "__main__":
    unittest.main()
