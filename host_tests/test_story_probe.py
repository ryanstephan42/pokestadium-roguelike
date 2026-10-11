import struct
import ctypes
import copy
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch
from pathlib import Path

from tools.rltest.frontend import HarnessError
from tools.rltest.story_probe import (
    FIELDS, finish_command, foreign_note, map_value, probe_deadline, story_owned_changes,
    stack_written_margin,
    memory_evidence,
    validate_memory,
    verify_mailbox_abi,
    require_checkpoint_party,
    decode_party,
    BRIDGE_VIEW_FIELDS,
    read_bridge,
    require_bridge,
)
from tools.rltest.test_custom_battle import Mon


class StoryHarnessTests(unittest.TestCase):
    def test_bridge_snapshot_abi_and_rejections(self):
        mon = dict(species=1, level=6, exp=235, dvs=0x8889, hp=17, max_hp=22,
                   fainted=0, status=0x40, moves=[33, 45, 0, 0], pp=[7, 40, 0, 0],
                   max_pp=[35, 40, 0, 0], current_stats=[22, 11, 11, 11, 13],
                   stat_exp=[0] * 5, base_stats=[45, 49, 49, 45, 65], growth=3)
        evidence = dict(enabled=1, action=0, boundary=1, error=0)
        for prefix in ("initial", "carry"):
            for kind in ("party", "live"):
                view = dict.fromkeys(BRIDGE_VIEW_FIELDS, 0)
                view.update(species=1, level=6, exp=235 if kind == "party" else 0, dvs=0x8889,
                            hp=17 if prefix == "initial" else 16, max_hp=22,
                            status=0x40 if prefix == "initial" else 0x48)
                view.update({f"moves{i}": move for i, move in enumerate(mon["moves"])})
                view.update({f"pp{i}": pp for i, pp in enumerate(
                    mon["pp"] if prefix == "initial" else [6, 40, 0, 0])})
                view.update({f"stats{i}": stat for i, stat in enumerate(
                    [22, 11, 11, 2, 13] if kind == "live" else mon["current_stats"])})
                if kind == "live":
                    view.update({f"base{i}": stat for i, stat in enumerate([11, 11, 11, 13])})
                    view.update({f"stage{i}": 7 for i in range(6)})
                evidence[f"{prefix}_{kind}"] = view
        for kind in ("party", "live"):
            evidence[f"current_{kind}"] = copy.deepcopy(evidence[f"initial_{kind}"])
        require_bridge(evidence, [mon])
        values = [1, 0, 1, 0]
        for name in ("initial_party", "initial_live", "carry_party", "carry_live",
                     "current_party", "current_live"):
            values.extend(evidence[name][field] for field in BRIDGE_VIEW_FIELDS)
        emu = Mock()
        emu.read_u32.return_value = len(values) * 4
        emu.read.return_value = struct.pack(f">{len(values)}I", *values)
        self.assertEqual(read_bridge(emu), evidence)
        emu.read_u32.return_value -= 4
        with self.assertRaisesRegex(HarnessError, "bridge ABI mismatch"):
            read_bridge(emu)
        for field, value in (("hp", 22), ("pp0", 35), ("status", 0), ("stats3", 11), ("stage0", 8)):
            wrong = copy.deepcopy(evidence)
            wrong["initial_live"][field] = value
            with self.assertRaises(HarnessError):
                require_bridge(wrong, [mon])
        evidence["boundary"] = 2
        for kind in ("party", "live"):
            evidence[f"current_{kind}"] = copy.deepcopy(evidence[f"carry_{kind}"])
        carry = {**mon, "hp": 16, "pp": [6, 40, 0, 0], "status": 0x48}
        require_bridge(evidence, [carry], carry=True)
        for kind in ("party", "live"):
            wrong = copy.deepcopy(evidence)
            wrong[f"current_{kind}"]["pp0"] = 35
            with self.assertRaisesRegex(HarnessError, "Fresh native bridge drift"):
                require_bridge(wrong, [carry], carry=True)
        evidence["error"] = 1
        with self.assertRaisesRegex(HarnessError, "boundary failed"):
            require_bridge(evidence, [carry], carry=True)

    def test_mailbox_and_execution_hold(self):
        self.assertEqual(len(FIELDS), 53)
        self.assertEqual(len(set(FIELDS)), len(FIELDS))
        result = subprocess.run([sys.executable, "tools/rltest/story_probe.py"],
                                capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("No emulator launched: --execute is required", result.stderr)

    def test_stack_written_margin(self):
        self.assertEqual(stack_written_margin(b"\xA5" * 64 + b"\x01\xA5"), 64)
        self.assertEqual(stack_written_margin(b"\xA5" * 64), 64)
        self.assertEqual(stack_written_margin(b"\0" + b"\xA5" * 64), 0)

    def test_checkpoint_party_discriminates_pp_status(self):
        party = [dict(species=1, hp=17, max_hp=22, level=6, exp=235, dvs=0x8889,
                      fainted=0, status=4, pp=[7, 40, 0, 0], moves=[33, 45, 0, 0],
                      max_pp=[35, 40, 0, 0])]
        require_checkpoint_party(party)
        party[0]["pp"][0] = 35
        with self.assertRaisesRegex(HarnessError, "discriminating checkpoint"):
            require_checkpoint_party(party)
        party[0]["pp"][0] = 7
        party[0]["status"] = 0
        with self.assertRaisesRegex(HarnessError, "discriminating checkpoint"):
            require_checkpoint_party(party)

    def test_decodes_resident_party_not_mailbox_or_padding(self):
        mon = Mon(species=1, level=6, exp=235, dvs=0x8889, cur_hp=17, max_hp=22, status=4)
        mon.moves[:] = [33, 45, 0, 0]
        mon.pp[:] = [7, 40, 0, 0]
        mon.max_pp[:] = [35, 40, 0, 0]
        emu = Mock()
        emu.read_u32.side_effect = lambda symbol: {
            "gScProbeMonSize": ctypes.sizeof(Mon), "gScProbePartyOffset": 4,
            "gScProbePartyCountOffset": 0,
        }[symbol]
        raw = b"\1\0\0\0" + bytes(mon)
        emu.read.return_value = raw
        result = dict(state_address=0x80100000, state_size=len(raw), hp=999)
        captured, party = decode_party(emu, result)
        self.assertEqual(captured, raw)
        emu.read.assert_called_once_with(result["state_address"], len(raw))
        require_checkpoint_party(party)
        changed = bytearray(raw)
        defined = set()
        for name, field in Mon._fields_:
            defined.update(range(getattr(Mon, name).offset,
                                 getattr(Mon, name).offset + ctypes.sizeof(field)))
        for byte in set(range(ctypes.sizeof(Mon))) - defined:
            changed[4 + byte] = 0xA5
        emu.read.return_value = bytes(changed)
        self.assertEqual(decode_party(emu, result)[1], party)
        changed[4 + Mon.pp.offset] = 35
        emu.read.return_value = bytes(changed)
        with self.assertRaisesRegex(HarnessError, "discriminating checkpoint"):
            require_checkpoint_party(decode_party(emu, result)[1])
        changed[4 + Mon.pp.offset] = 7
        changed[4 + Mon.status.offset] = 0
        emu.read.return_value = bytes(changed)
        with self.assertRaisesRegex(HarnessError, "discriminating checkpoint"):
            require_checkpoint_party(decode_party(emu, result)[1])
        emu.read.return_value = b"\7" + raw[1:]
        with self.assertRaisesRegex(HarnessError, "bounds mismatch"):
            decode_party(emu, result)

    def test_stale_target_mailbox_rejected(self):
        emu = Mock()
        emu.read_u32.return_value = len(FIELDS) * 4
        emu.symbols.address.side_effect = [0x80110000, 0x80110000 + 47 * 4]
        with self.assertRaisesRegex(HarnessError, "Stale target mailbox ABI"):
            verify_mailbox_abi(emu)
        emu.read_u32.return_value = 47 * 4
        emu.symbols.address.side_effect = [0x80110000, 0x80110000 + len(FIELDS) * 4]
        with self.assertRaisesRegex(HarnessError, "Stale compiled mailbox ABI"):
            verify_mailbox_abi(emu)
        emu.read_u32.return_value = len(FIELDS) * 4
        emu.symbols.address.side_effect = [0x80110000, 0x80110000 + len(FIELDS) * 4]
        verify_mailbox_abi(emu)

    def test_relic_harnesses_reject_story_before_startup(self):
        from tools.rltest import test_native_relics, test_relic_cadence
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "probe.map"
            path.write_text("0x80001000 gScProbeCompiledSize\n0x80002000 gRlRelicTrace\n")
            for module in (test_native_relics, test_relic_cadence):
                with patch.object(module, "Frontend") as frontend:
                    with self.assertRaisesRegex(HarnessError, "STORY_NATIVE_PROBE"):
                        module.run_one("unused", "unused", "unused", path, temporary, 1)
                    frontend.assert_not_called()
            path.write_text("0x80002000 gRlRelicTrace\n")
            test_native_relics.require_relic_trace_build(path)

    def test_actual_fragment_map_and_allocation_diagnostics(self):
        emu = Mock()
        emu.story_stack_before = b"\xA5" * 96 + b"\0" * (0x18E0 - 96)
        emu.symbols.address.side_effect = {
            "D_80080000": 0x80080000, "D_800818E0": 0x800818E0,
            "gFragments": 0x80090000,
            "gScPoolZero": 0x80091000, "gScPoolFailure": 0x80091040,
        }.__getitem__
        emu.read.side_effect = [b"\xA5" * 80 + b"\0" * (0x18E0 - 80), b"\0" * 8,
                               bytes(40), bytes(40)]
        emu.read_u32.side_effect = [320, 4, 0, 128]
        result = dict(map_before=0, map_before_size=0, map_loaded=0x80200000,
                      map_loaded_size=173456, cold_size=173456, map_after=0, map_after_size=0)
        evidence = memory_evidence(emu, result)
        self.assertEqual(evidence["stack_written_margin"], 80)
        self.assertEqual(evidence["stack_precommand_margin"], 96)
        self.assertEqual(evidence["stack_new_bottom_writes"], 16)
        validate_memory(evidence, result)
        emu.read.side_effect = [b"\xA5" * 80 + b"\0" * (0x18E0 - 80),
                               struct.pack(">II", 0, 173456), bytes(40), bytes(40)]
        emu.read_u32.side_effect = [320, 4, 0, 128]
        with self.assertRaisesRegex(HarnessError, "fragment map survived"):
            validate_memory(memory_evidence(emu, result), result)

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
