"""HOST fake-core composition. UI/result seams are labelled, not native proof."""

import copy
import ctypes
import io
import json
import os
import pathlib
import subprocess
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

from host_tests.test_story_sequential_pak import template
from tools.rltest.frontend import Frontend, HarnessError
from tools.rltest.run_story_sequential import collect
from tools.rltest.story_sequential_supervisor import RunBudget
from tools.rltest import story_sequential_worker as worker

ROOT = pathlib.Path(__file__).resolve().parents[1]


class FakeCore:
    """Real save-buffer and frame leases; no ROM is loaded or executed."""

    def __init__(self, core, rom, map_path, out, *, options, timeout, frame_lease, prepare_save):
        self.options, self.frame_lease = options, frame_lease
        self.frame = 0
        self._sc_budget_bound = True
        self.held = [set(), set()]
        self.analog = [[0] * 4, [0] * 4]
        self.buffer = ctypes.create_string_buffer(bytes(template()), 32768)
        self.core = types.SimpleNamespace(
            retro_get_memory_size=lambda kind: 32768,
            retro_get_memory_data=lambda kind: ctypes.addressof(self.buffer))
        self.lock = (out.parent.parent if out.name == "terminal-reload" else out.parent) / "core-live"
        self.fd = os.open(self.lock, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
        try:
            prepare_save(self)
            self.step(1)  # Same implicit constructor frame as the real Frontend.
        except HarnessError:
            self.close()
            raise
        self.phase, self.exp = 0, 100

    def step(self, frames=1):
        self.frame_lease(frames)
        self.frame += frames

    def read_u32(self, name):
        return 0x800000 if name == "osMemSize" else 4

    def close(self):
        os.close(self.fd)
        self.lock.unlink()

    def row(self):
        state = bytearray(648)
        state[20] = self.phase
        state[32:36] = self.exp.to_bytes(4, "big")
        return dict(state_raw=state.hex(), party=[dict(species=4, exp=self.exp)],
                    phase=self.phase)


class FakeScenario:
    """Substitutes native UI/combat while execute_scenario uses actual Pak IO,
    Continue comparisons, save isolation, reports and cold-process orchestration.
    Actual menu/observer behavior has separate direct fixtures below/alongside.
    """

    def __init__(self, emu, abi, manifest, directory, name, calibration):
        self.emu, self.directory, self.name = emu, directory, name
        self.boundary = "fake-core-initialized"
        self.transitions, self.acknowledgements = [], []
        self.ledger = types.SimpleNamespace(result=1 if name == "win" else 0,
                                           events=[dict(enemy=0)])
        self.transform = {"fake-observation": True} if name == "transform" else None
        self.retirement = {"fake-observation": True}
        self.screen = 2

    def wait(self, predicate, description):
        self.emu.step(1)
        self.boundary = description

    def press(self, name):
        self.emu.step(2)

    def enter_continue(self):
        self.emu.step(1)
        self.emu.exp = self.emu.buffer[1290][0] or 100
        self.emu.phase = 4 if self.emu.buffer[1295] == b"\1" else 0
        self.screen = 17 if self.emu.phase == 4 else 2
        return self.emu.row()

    def ui(self):
        return dict(screen=self.screen, pending_save=0)

    def launch(self):
        self.emu.step(1)
        self.emu.phase = 1
        self.emu.buffer[1295] = b"\1"
        (self.directory / "interrupted.note").write_bytes(
            worker.story_note(self.emu.buffer.raw))

    def finish_battle(self):
        self.emu.step(3)
        self.emu.phase = 0 if self.name == "win" else 4
        if self.name == "win":
            self.emu.exp = 120
            self.emu.buffer[1290] = bytes([120])
            self.emu.buffer[1295] = b"\0"

    def select(self, screen, id_=None):
        self.emu.step(2)
        self.screen = 0


def fake_inputs(args):
    note = b"SC01" + bytes(1532)
    package = args.out.parent / "fake-package"
    package.mkdir(exist_ok=True)
    for name in ("normal-route.note", "transform-route.note"):
        (package / name).write_bytes(note)
    args.package = package
    args.core = args.out.parent / "fake-core"
    args.core.write_bytes(b"HOST fake only")
    args.core_sha256 = worker.digest(args.core.read_bytes())
    return (dict(configuration={"BUILD_DIR": "build/story/sequential-805a0dc-build-v9",
                                "ROM": "not-loaded-by-fake"}),
            dict(files={name: worker.digest(note) for name in
                        ("normal-route.note", "transform-route.note")}),
            dict(abi={"state": {"phase": 20}}, binding={"run": 0x80001000}))


def fake_child(args):
    inputs = fake_inputs(args)
    with patch.object(worker, "verify_candidate", return_value=inputs), \
         patch.object(worker, "Scenario", FakeScenario), \
         patch.object(worker, "capture", side_effect=lambda emu, *rest: emu.row()), \
         patch.object(worker, "calibrate", return_value={"HOST": "fake-only"}):
        worker.execute_scenario(args.scenario, args, factory=FakeCore)


class SequentialWorkerTests(unittest.TestCase):
    def test_external_seal_pin_is_required_before_core_construction(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "seal.json"
            path.write_text("{}")
            with self.assertRaisesRegex(HarnessError, "approved external pin"):
                worker.verify_candidate(path, path.parent, path, "0" * 64)
            with self.assertRaisesRegex(HarnessError, "approved external pin"):
                worker.verify_candidate(path, path.parent, path, "")

    def test_real_frontend_constructor_frame_is_leased_before_run(self):
        events = []

        def init(emu, *args, **kwargs):
            emu.frame = 0
            emu.step(1)

        def step(emu, n):
            events.append(("core-run", n))
            emu.frame += n

        with patch.object(Frontend, "__init__", init), patch.object(Frontend, "step", step):
            emu = worker.LeasedFrontend(frame_lease=lambda n: events.append(("lease", n)),
                                       prepare_save=lambda emu: events.append(("setup", emu.frame)))
            self.assertEqual(events, [("setup", 0), ("lease", 1), ("core-run", 1)])
            self.assertEqual(emu.frame, 1)

            def refuse(n):
                raise HarnessError("refused")
            with self.assertRaises(HarnessError):
                worker.LeasedFrontend(frame_lease=refuse, prepare_save=lambda emu: None)
            self.assertEqual(len(events), 3)

    def test_reload_and_postboot_save_writes_refused(self):
        for frame, reload in ((0, True), (1, False)):
            with patch.object(worker.ctypes, "memmove") as write:
                with self.assertRaisesRegex(HarnessError, "write refused"):
                    worker.install_setup(types.SimpleNamespace(frame=frame), b"checkpoint",
                                         reload=reload)
                write.assert_not_called()

    def test_collect_transform_zero_is_untested(self):
        with tempfile.TemporaryDirectory() as directory:
            out = pathlib.Path(directory)
            (out / "transform").mkdir()
            worker.write_json(out / "transform/report.json", dict(status="pass", transform=0))
            report = collect(out, RunBudget(0, 180), 1, True)
            self.assertEqual(report["scenarios"]["transform"]["status"], "untested")

    def test_substituted_reload_bytes_refused_before_write(self):
        with patch.object(worker.ctypes, "memmove") as write:
            with self.assertRaisesRegex(HarnessError, "substituted bytes"):
                worker.load_persisted_save(types.SimpleNamespace(frame=0), b"checkpoint",
                                           worker.digest(b"game-written"))
            write.assert_not_called()

    def test_fake_core_cold_reloads_real_worker_and_supervisor(self):
        with tempfile.TemporaryDirectory() as directory:
            output = pathlib.Path(directory) / "reservation"
            command = [sys.executable, "-m", "host_tests.test_story_sequential_worker",
                       "--fake-parent", str(output)]
            supervisor = """
import json, sys
from tools.rltest.story_sequential_supervisor import supervise
from tools.rltest.run_story_sequential import collect
from pathlib import Path
budget = supervise(sys.argv[2:], sys.stderr, seconds=5)
print(json.dumps(collect(Path(sys.argv[1]), budget, 0, True)))
"""
            result = subprocess.run([sys.executable, "-c", supervisor, str(output), *command],
                                    cwd=ROOT, capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)
            report = json.loads(result.stdout)
            self.assertEqual([row["status"] for row in report["scenarios"].values()],
                             ["pass"] * 4)
            self.assertGreater(report["scenario_frames"]["win"], 0)
            self.assertGreater(report["scenario_frames"]["carry-reload"], 0)
            self.assertGreater(report["scenario_frames"]["loss-terminal-reload"], 0)
            self.assertTrue(report["no_orphans"])
            self.assertFalse((output / "core-live").exists())
            self.assertTrue((output / "loss-terminal-reload/terminal-reload/report.json").is_file())
            self.assertEqual(report["actual_carry_sha256"], worker.digest(
                worker.story_note((output / "win/save-ram.bin").read_bytes())))
            cold = json.loads((output / "carry-reload/committed-state.json").read_text()) \
                if (output / "carry-reload/committed-state.json").exists() else None
            self.assertIsNone(cold, "Reload must inspect, not write a new checkpoint")
            for name in ("carry-reload", "loss-terminal-reload/terminal-reload"):
                cold_report = json.loads((output / name / "report.json").read_text())
                self.assertEqual(cold_report["game_written_save_before_reload_sha256"],
                                 cold_report["loaded_save_sha256"])
                self.assertEqual(cold_report["loaded_save_sha256"],
                                 cold_report["game_written_save_after_reload_sha256"])

    def test_cold_reload_mismatch_refuses_without_success_report(self):
        with tempfile.TemporaryDirectory() as directory:
            out = pathlib.Path(directory) / "reservation"
            out.mkdir()
            args = types.SimpleNamespace(out=out, scenario="win", terminal_reload=False,
                                         seal=out / "unused", binding=out / "unused", seal_sha256="fake")
            inputs = fake_inputs(args)
            streams = dict(stream_in=io.StringIO("OK\n" * 100), stream_out=io.StringIO())
            with patch.object(worker, "verify_candidate", return_value=inputs), \
                 patch.object(worker, "Scenario", FakeScenario), \
                 patch.object(worker, "capture", side_effect=lambda emu, *rest: emu.row()), \
                 patch.object(worker, "calibrate", return_value={}):
                worker.execute_scenario("win", args, factory=FakeCore, **streams)
                committed = out / "win/committed-state.json"
                value = json.loads(committed.read_text())
                value["party"][0]["exp"] += 1
                committed.write_text(json.dumps(value))
                with self.assertRaisesRegex(HarnessError, "carry reload mismatch"):
                    worker.execute_scenario("carry-reload", args, factory=FakeCore, **streams)
            report = json.loads((out / "carry-reload/report.json").read_text())
            self.assertEqual(report["status"], "fail")
            self.assertFalse((out / "core-live").exists())

    def test_collect_marks_failed_attempt_and_never_passes_missing_terminal(self):
        with tempfile.TemporaryDirectory() as directory:
            out = pathlib.Path(directory)
            worker.write_json(out / "win.attempted.json", {})
            loss = out / "loss-terminal-reload"
            loss.mkdir()
            worker.write_json(loss / "report.json", dict(status="awaiting-terminal-reload"))
            report = collect(out, RunBudget(0, 180), 1, True)
            self.assertEqual(report["scenarios"]["win"]["status"], "fail")
            self.assertEqual(report["scenarios"]["carry-reload"]["status"], "untested")
            self.assertEqual(report["scenarios"]["loss-terminal-reload"]["status"], "fail")
            self.assertIsNone(report["actual_carry_sha256"])

    def test_calibration_uses_observed_bits_and_refuses_missing_c_button(self):
        class Inputs:
            held = [set(), set()]
            analog = [[0] * 4, [0] * 4]
            def step(self, n):
                pass
            def read_u32(self, name):
                return 0x80001000
            def read_u16(self, address):
                ids = {0: 0x8000, 1: 0x4000, 10: 0x20, 11: 0x10,
                       4: 0x800, 5: 0x400, 3: 0x1000}
                if self.held[0]:
                    return ids.get(next(iter(self.held[0])), 0)
                return {(0, 32767): 8, (32767, 0): 1,
                        (0, -32768): 4, (-32768, 0): 2}.get(tuple(self.analog[0][2:]), 0)
            def stick(self, x, y, right):
                self.analog[0][2:] = [x, y]
        core = Inputs()
        calibration = worker.calibrate(core)
        self.assertEqual(calibration["C_LEFT"], dict(kind="right-stick", xy=[-32768, 0]))
        self.assertFalse(any(core.held))
        with patch.object(worker, "candidate_bindings",
                          return_value=[dict(kind="joypad", id=0)]):
            with self.assertRaisesRegex(HarnessError, "incomplete"):
                worker.calibrate(core)
        self.assertFalse(any(core.held))

    def test_real_ui_reader_bound_menu_and_wrong_phase(self):
        abi = {"control": {"ui_pointer": 8}, "ui": {
            "size": 256, "list": 0, "ids": 32, "labels": 48,
            "screen": 12, "operation": 16, "pending_save": 20},
            "list": {"count": 0, "cursor": 4}}
        data = bytearray(256)
        data[0:4] = (2).to_bytes(4, "big")
        data[4:8] = (1).to_bytes(4, "big")
        data[12:16] = (3).to_bytes(4, "big")
        data[32:36] = b"\0\x01\0\x3a"
        data[48:53] = b"First"
        data[120:125] = b"Brock"

        class MenuCore:
            stage = 1
            def read_u32(self, name):
                return self.stage if name == "gScCampaignRun" else (
                    0x80004000 if name == "gScCampaignObserverControl" else 0x80400000)
            def read(self, address, size):
                return bytes(data)
        emu = MenuCore()
        view = worker.menu_view(emu, abi)
        self.assertEqual(view["ids"], [1, 58])
        self.assertEqual(view["labels"], ["First", "Brock"])
        emu.stage = 4
        with self.assertRaisesRegex(HarnessError, "outside active"):
            worker.menu_view(emu, abi)

    def test_actual_ui_selection_acknowledges_before_confirming(self):
        scenario = object.__new__(worker.Scenario)
        menu = dict(screen=3, cursor=0, ids=[1, 58], labels=["first", "Brock"])
        scenario.emu = types.SimpleNamespace(read_u32=lambda _: 1)
        scenario.ui = lambda: copy.deepcopy(menu)
        presses = []
        def press(name):
            presses.append(name)
            if name == "DOWN":
                menu["cursor"] += 1
        scenario.press = press
        scenario.wait = lambda predicate, description: self.assertTrue(predicate(), description)
        scenario.select(3, id_=58)
        self.assertEqual(presses, ["DOWN", "N64_A"])
        with self.assertRaises(HarnessError):
            scenario.select(3, id_=99)

    def test_actual_move_policy_and_native_party_mapping_with_transient_ack(self):
        scenario = object.__new__(worker.Scenario)
        scenario.mapping = [2, 0, 1]
        scenario.scenario = "win"
        scenario.abi = {"native_run_address": 0x80001000}
        scenario.emu = object()
        scenario.acknowledgements = []
        menu = dict(action=2, mode=2, locked=False, active=2, count=3,
                    hp=[5, 5, 5], moves=[10, 45, 0, 52], pp=[30, 40, 0, 25],
                    disabled=-1)
        presses = []
        def press(button):
            presses.append(button)
            # ACK is seen during the held frame, then gone after release.
            # The worker must retain it instead of polling only after press().
            scenario.native_ack = 17
        scenario.press = press
        scenario.wait = lambda predicate, description: self.assertTrue(predicate(), description)
        with patch.object(worker, "read_menu", return_value=menu):
            scenario.battle_action()
        self.assertEqual(presses, ["C_RIGHT"])  # native1 = story0, not native0.
        self.assertEqual(scenario.acknowledgements[-1]["slot"], 1)
        menu.update(action=1, mode=1, active=1)
        with patch.object(worker, "read_menu", return_value=menu):
            scenario.battle_action()
        self.assertEqual(presses[-1], "C_LEFT")  # actual Ember slot3.
        self.assertEqual(scenario.acknowledgements[-1]["frame"], 17)
        scenario.scenario = "loss-terminal-reload"
        with patch.object(worker, "read_menu", return_value=menu):
            scenario.battle_action()
        self.assertEqual(presses[-1], "C_RIGHT")  # Growl, not Ember.

    def test_retained_dp_snapshot_and_joined_cleanup_required_before_battle(self):
        from tools.rltest.bind_story_sequential import bind_lifecycle_words
        native = {
            "lifecycle": dict(memory_live=0, memory_storage=4, lifetime_live=8,
                              display_acquired=12, display_scope=16, retirement_acquired=20,
                              retirement_pending=24, backing_retired=28, snapshot_valid=32,
                              fallback_completed=36, snapshot=40, spans=132),
            "snapshot": dict(size=92, frames=0, fallback=48, vi_current=52, vi_next=56,
                             crash_frame=60, waiting=64, requests=68, completions=72,
                             events=76, rsp_state=80, rdp_state=84, task_completions=88),
            "frame": dict(size=16, rsp=1, rdp=2, work=4, task=8, image=12),
            "span": dict(size=8, pixels=0, bytes=4),
        }
        keys = [(group, key) for group, values in native.items() for key in values]
        words = [native[group][key] for group, key in keys]
        self.assertEqual(bind_lifecycle_words(keys, words, 924), native)
        for key in ("memory_storage", "snapshot"):
            changed = words.copy()
            changed[keys.index(("lifecycle", key))] = 924
            with self.assertRaises(HarnessError):
                bind_lifecycle_words(keys, changed, 924)
        changed = words.copy()
        changed[keys.index(("frame", "image"))] = 8
        with self.assertRaises(HarnessError):
            bind_lifecycle_words(keys, changed, 924)
        raw = bytearray(924)
        def put(offset, value):
            raw[offset:offset + 4] = value.to_bytes(4, "big")
        for offset in (28, 32, 36):
            put(offset, 1)
        for offset, value in ((88, 0x80001000), (92, 0x80002000), (96, 0x80002000),
                              (100, 0x80003000), (104, 1), (132, 0x80400000),
                              (136, 614400), (140, 0x80500000), (144, 614400)):
            put(offset, value)
        class RetiredCore:
            mapped = False
            def read(self, address, size):
                if address == "gFragments":
                    return bytes(size - 1) + bytes([int(self.mapped)])
                return b"\0\0\0\2\2\x80\0\1\x80\0\x20\0" + bytes(4)
        core = RetiredCore()
        self.assertEqual(worker.retirement_evidence(core, raw, native)["flags"]["backing_retired"], 1)
        for offset in (0, 24, 44, 108, 116, 128):
            altered = bytearray(raw)
            altered[offset:offset + 4] = (1).to_bytes(4, "big")
            with self.assertRaises(HarnessError):
                worker.retirement_evidence(core, altered, native)
        core.mapped = True
        with self.assertRaisesRegex(HarnessError, "still mapped"):
            worker.retirement_evidence(core, raw, native)


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--fake-parent":
        out = pathlib.Path(sys.argv[2])
        args = types.SimpleNamespace(out=out)
        worker.run_children(args, command_factory=lambda args: [
            sys.executable, "-m", "host_tests.test_story_sequential_worker",
            "--fake-child", str(args.out)])
    elif len(sys.argv) > 1 and sys.argv[1] == "--fake-child":
        out = pathlib.Path(sys.argv[2])
        name = sys.argv[sys.argv.index("--scenario") + 1]
        args = types.SimpleNamespace(out=out, scenario=name,
                                     terminal_reload="--terminal-reload" in sys.argv,
                                     seal=out / "unused", binding=out / "unused", seal_sha256="fake")
        fake_child(args)
    else:
        unittest.main()
