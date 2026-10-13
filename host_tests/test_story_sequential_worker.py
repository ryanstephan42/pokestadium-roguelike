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


class ChildExitTests(unittest.TestCase):
    def test_separate_run_starts_only_selected_scenario_and_terminal_reload(self):
        with tempfile.TemporaryDirectory() as directory:
            out = pathlib.Path(directory) / "run"
            args = types.SimpleNamespace(out=out, only_scenario="loss-terminal-reload")
            calls = []
            def run(command, **kwargs):
                calls.append(command)
                path = out / "loss-terminal-reload"
                path.mkdir(exist_ok=True)
                (path / "report.json").write_text("{}")
                return types.SimpleNamespace(returncode=0)
            with patch.object(worker.subprocess, "run", side_effect=run), \
                    patch.object(worker.sys, "stdin", io.StringIO("OK\n")), \
                    patch.object(worker.sys, "stdout", io.StringIO()):
                worker.run_children(args, command_factory=lambda _: ["fake-worker"])
            self.assertEqual(calls, [
                ["fake-worker", "--scenario", "loss-terminal-reload"],
                ["fake-worker", "--scenario", "loss-terminal-reload", "--terminal-reload"]])

    def test_producer_report_and_every_persisted_input_are_pinned(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            producer = root / "build/story/producer"
            (producer / "win").mkdir(parents=True)
            names = ("save-ram.bin", "committed-state.json", "calibration.json", "report.json")
            for name in names:
                (producer / "win" / name).write_bytes(b"game-written")
            report = dict(exact_tree_reaped=True, no_orphans=True,
                          core_sha256="core", candidate_rom_sha256="rom",
                          scenarios={"win": {"status": "pass"}},
                          artifact_sha256={"win/" + name: worker.digest(b"game-written")
                                           for name in names})
            report_path = producer.with_suffix(".report.json")
            report_path.write_text(json.dumps(report))
            args = types.SimpleNamespace(producer_run=producer, core_sha256="core",
                                         producer_report_sha256=worker.digest(report_path.read_bytes()))
            with patch.object(worker, "ROOT", root):
                self.assertEqual(worker.producer_inputs(args, "rom"), producer)
                (producer / "win/save-ram.bin").write_bytes(b"substituted")
                with self.assertRaisesRegex(HarnessError, "Producer artifact changed"):
                    worker.producer_inputs(args, "rom")
                with self.assertRaisesRegex(HarnessError, "ROM/core differs"):
                    worker.producer_inputs(args, "different-rom")
                args.producer_report_sha256 = "wrong-pin"
                with self.assertRaisesRegex(HarnessError, "report pin mismatch"):
                    worker.producer_inputs(args, "rom")

    def test_native_exit_is_retained_and_debug_output_is_redirected(self):
        with tempfile.TemporaryDirectory() as directory:
            out = pathlib.Path(directory) / "run"
            args = types.SimpleNamespace(out=out, debug_exit=True)
            with patch.object(worker.subprocess, "run",
                              return_value=types.SimpleNamespace(returncode=1)) as run:
                with self.assertRaisesRegex(HarnessError, "returncode=1"):
                    worker.run_children(args, command_factory=lambda _: ["fake-worker"])
            command = run.call_args.args[0]
            self.assertEqual(command[0], "gdb")
            self.assertIn("--protocol-fd", command)
            self.assertNotEqual(run.call_args.kwargs["stdout"], sys.stdout)
            self.assertIn("--nx", command)
            self.assertIn("set debuginfod enabled off", command)
            self.assertEqual(json.loads((out / "win.exit.json").read_text()),
                             dict(returncode=1, host_debugger=True))

    def test_debugger_success_without_scenario_report_is_not_a_pass(self):
        with tempfile.TemporaryDirectory() as directory:
            args = types.SimpleNamespace(out=pathlib.Path(directory) / "run",
                                         debug_exit=True)
            with patch.object(worker.subprocess, "run",
                              return_value=types.SimpleNamespace(returncode=0)):
                with self.assertRaisesRegex(HarnessError, "without final report"):
                    worker.run_children(args, command_factory=lambda _: ["fake-worker"])


class BootThreadTests(unittest.TestCase):
    def test_early_action_edge_does_not_wait_forever(self):
        frames = []
        scenario = object.__new__(worker.Scenario)
        scenario.emu = types.SimpleNamespace(step=lambda n: frames.append(n))
        scenario.abi = {"native_run_address": 0x80010000}
        scenario.mapping = [0]
        presses = []
        scenario.press = presses.append
        menu = dict(mode=0, action=0, locked=False, hp=[32], active=0)
        with patch.object(worker, "read_menu", return_value=menu):
            scenario.battle_action()
            scenario.battle_action()
        self.assertEqual(presses, ["N64_A", "N64_A"])
        self.assertEqual(frames, [1] * 120)
        frames.clear()
        presses.clear()
        with patch.object(worker, "read_menu", side_effect=[
                menu, menu, menu, {**menu, "mode": 1}]):
            scenario.battle_action()
        self.assertEqual(presses, ["N64_A"])
        self.assertEqual(frames, [1, 1])

    def test_native_observation_is_read_only_and_bounded(self):
        reads = []
        def read(symbol, size):
            reads.append(size)
            if symbol == "D_84390010":
                return bytes.fromhex("8001000080011000")
            return bytes(size)
        emu = types.SimpleNamespace(read=read, read_u32=lambda _: 0x80020000,
                                    read_u8=lambda _: 2)
        rows = worker.native_observation(emu)
        self.assertEqual([row["count"] for row in rows], [2, 2])
        self.assertEqual(len(rows[0]["party"]), 2 * 0x54 * 2)
        self.assertEqual(len(rows[0]["dialog"]), 0x8C * 2)
        self.assertLessEqual(max(reads), 6 * 0x54)
        emu.read_u8 = lambda _: 7
        with self.assertRaisesRegex(HarnessError, "count invalid"):
            worker.native_observation(emu)
        emu.read_u32 = lambda _: 0x84300000
        with self.assertRaisesRegex(HarnessError, "outside 8 MiB"):
            worker.native_observation(emu)

    def test_missed_move_edge_returns_to_counted_policy_without_false_ack(self):
        scenario = object.__new__(worker.Scenario)
        frames, presses = [], []
        scenario.emu = types.SimpleNamespace(step=frames.append)
        scenario.abi = {"native_run_address": 0x80010000}
        scenario.mapping = [0]
        scenario.scenario = "transform"
        scenario.acknowledgements = []
        scenario.press = presses.append
        menu = dict(mode=1, action=1, locked=False, hp=[33], active=0,
                    moves=[33, 111, 0, 0], pp=[4, 0, 0, 0], disabled=-1)
        with patch.object(worker, "read_menu", return_value=menu):
            scenario.battle_action()
            scenario.battle_action()
        self.assertEqual(presses, ["C_UP", "C_UP"])
        self.assertEqual(frames, [1] * 120)
        self.assertEqual(scenario.acknowledgements, [])
        self.assertIsNone(scenario.expected_native)
        frames.clear()
        def step(n):
            frames.append(n)
            scenario.native_ack = 18
        scenario.emu.step = step
        with patch.object(worker, "read_menu", return_value=menu):
            scenario.battle_action()
        self.assertEqual(frames, [1])
        self.assertEqual(scenario.acknowledgements,
                         [dict(frame=18, kind="move", slot=0, button="C_UP")])

    def test_native_error_dialog_uses_button_not_infinite_neutral_wait(self):
        scenario = object.__new__(worker.Scenario)
        scenario.emu = types.SimpleNamespace()
        scenario.abi = {"native_run_address": 0x80010000}
        presses = []
        scenario.press = presses.append
        for action, mode in ((1, 1), (2, 2)):
            with patch.object(worker, "read_menu",
                              return_value=dict(action=action, mode=mode, locked=True)):
                scenario.battle_action()
        self.assertEqual(presses, ["N64_A", "N64_A"])
        for action, mode in ((0, 0), (1, 3)):
            with patch.object(worker, "read_menu",
                              return_value=dict(action=action, mode=mode, locked=True)):
                scenario.battle_action()
        self.assertEqual(presses, ["N64_A", "N64_A"])

    def test_service_fail_stop_is_reported_without_waiting_for_frame_cap(self):
        scenario = object.__new__(worker.Scenario)
        raw = bytes(36) + (1).to_bytes(4, "big")
        scenario.abi = {"native_service": dict(address=0x80001000, size=40,
                                              failed_offset=36)}
        scenario.emu = types.SimpleNamespace(read_u32=lambda _: worker.STORY_STATE,
                                             read=lambda address, size: raw)
        snapshots = []
        scenario.progress = types.SimpleNamespace(details={},
                                                  persist=snapshots.append)
        with self.assertRaisesRegex(HarnessError, "callable service fail-stopped"):
            scenario.observe()
        self.assertEqual(scenario.progress.details["service_lifecycle"], raw.hex())
        self.assertEqual(snapshots, [scenario.emu])

    def test_first_transform_witness_survives_cap_without_claiming_completion(self):
        with tempfile.TemporaryDirectory() as directory:
            out = pathlib.Path(directory)
            path = out / "transform"
            path.mkdir()
            scenario = object.__new__(worker.Scenario)
            mons = [dict(species=132, moves=[33, 111, 0, 0]),
                    dict(species=74, moves=[33, 111, 0, 0])]
            scenario.emu = types.SimpleNamespace(
                frame=1500, read_u32=lambda _: worker.STORY_STATE,
                battle_mons=lambda: mons)
            scenario.abi = {"native_lifecycle": {}}
            scenario.directory, scenario.scenario = path, "transform"
            scenario.ledger = scenario.last = scenario.retirement = None
            scenario.transform = scenario.progress = scenario.expected_native = None
            scenario.interrupted = b"already-captured"
            scenario.transitions = []
            row = dict(stage=worker.BATTLE, ledger={"count": 0}, ui=None,
                       control_raw="")
            with patch.object(worker, "capture", return_value=row), \
                    patch.object(worker, "retirement_evidence", return_value={}):
                scenario.observe()
                scenario.emu.frame = 18000
                scenario.observe()
            self.assertEqual(json.loads((path / "transform-observed.json").read_text()),
                             dict(frame=1500, player=mons[0], enemy=mons[1]))
            worker.write_json(out / "transform.attempted.json", {})
            report = collect(out, RunBudget(0, 180), 1, True)
            self.assertEqual(report["scenarios"]["transform"]["status"], "fail")
            self.assertEqual(report["scenarios"]["transform"]["transform_observed"]["frame"],
                             1500)

    def test_saved_thread_register_offsets_and_bounded_stack(self):
        data = bytearray(0x130)
        data[0x10:0x12] = (8).to_bytes(2, "big")
        for offset, value in ((0x14, 6), (0x11C, 0x80005000),
                              (0xF4, 0x80001000), (0x104, 0x80007000)):
            data[offset:offset + 4] = value.to_bytes(4, "big")
        reads = []

        def read(address, size):
            reads.append((address, size))
            return bytes(data) if address == "pThreads" else bytes(size)

        row = worker.boot_thread(types.SimpleNamespace(read=read))
        self.assertEqual((row["state"], row["id"], row["pc"], row["ra"]),
                         (8, 6, 0x80005000, 0x80007000))
        self.assertEqual(len(row["stack_words"]), 16)
        self.assertEqual(len(reads), 17)
        data[0xF4:0xF8] = (0xFFFFFFFF).to_bytes(4, "big")
        self.assertNotIn("stack_words", worker.boot_thread(types.SimpleNamespace(read=read)))


class FakeCore:
    """Real save-buffer and frame leases; no ROM is loaded or executed."""

    def __init__(self, core, rom, map_path, out, *, options, timeout, frame_lease, prepare_save,
                 progress=None, boot_symbols=None, require_expansion=False):
        self.options, self.frame_lease = options, frame_lease
        self.progress = progress
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
        if self.progress:
            self.progress.persist(self)
        self.frame_lease(frames)
        self.frame += frames
        if self.progress:
            self.progress.completed = self.frame
            self.progress.persist(self)

    def read_u32(self, name):
        return 0x800000 if name == "osMemSize" else 4

    def hold(self, name):
        self.held[0].add(name)

    def release(self, name):
        self.held[0].discard(name)

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

    def boot_to_title(self):
        self.wait(lambda: True, "fake-title")

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

    def test_incremental_diagnostics_survive_denied_step_and_bound_video(self):
        with tempfile.TemporaryDirectory() as directory:
            out = pathlib.Path(directory)
            progress = worker.Progress(out)
            emu = types.SimpleNamespace(frame=1, framebuffer=(bytes(16), 2, 2, 8),
                                        pixel_format=1, read_u32=lambda symbol: 1)
            progress.completed = 1
            for i in range(12):
                progress.mark(f"boundary-{i}", emu)
            self.assertEqual(len(list(out.glob("video-*.png"))), 8)
            progress.witness = {"ledger": {"count": 1}}
            progress.mark("waiting-title", emu)
            with self.assertRaises(HarnessError):
                progress.persist(emu)
                raise HarnessError("Supervisor cap")
            value = json.loads((out / "progress.json").read_text())
            self.assertEqual(value["boundary"], "waiting-title")
            self.assertEqual(value["witness"]["ledger"]["count"], 1)
            worker.write_json(out / "win.attempted.json", {})
            (out / "win").mkdir()
            (out / "progress.json").replace(out / "win/progress.json")
            report = collect(out, RunBudget(0, 180), 1, True)
            self.assertEqual(report["scenarios"]["win"]["status"], "fail")
            self.assertIsNone(report["frames_observed"])
            self.assertEqual(report["frames_completed_persisted"], 1)

    def test_snapshot_retains_early_frames_and_repeated_boundary_is_throttled(self):
        with tempfile.TemporaryDirectory() as directory:
            progress = worker.Progress(pathlib.Path(directory))
            emu = types.SimpleNamespace(frame=1, framebuffer=(bytes(16), 2, 2, 8),
                                        pixel_format=1, read_u32=lambda symbol: 1)
            progress.mark("boot", emu)
            emu.frame = 2
            progress.mark("intro", emu)
            for frame in range(3, 499):
                emu.frame = frame
                progress.mark("boot", emu)
            self.assertEqual(progress.snapshots, 2)
            for frame in range(502, 6503, 500):
                emu.frame = frame
                progress.persist(emu)
            self.assertEqual(progress.details["videos"]["0"]["frame"], 1)
            self.assertEqual(progress.details["videos"]["1"]["frame"], 2)
            self.assertEqual(len(list(pathlib.Path(directory).glob("video-*.png"))), 8)

    def test_absolute_boot_symbol_resolves_from_binding_not_map(self):
        emu = worker.LeasedFrontend.__new__(worker.LeasedFrontend)
        emu.boot_symbols = {"osMemSize": 0x80000318}
        self.assertEqual(emu.resolve("osMemSize"), 0x80000318)

    def test_control_core_has_exact_1500_frames_and_fresh_private_output(self):
        with tempfile.TemporaryDirectory() as directory:
            out = pathlib.Path(directory) / "run"
            out.mkdir()
            core = out.parent / "fake-core"
            core.write_bytes(b"fake")
            args = types.SimpleNamespace(out=out, core=core, control=out.parent / "control.json",
                                         seal=out / "seal", package=out / "package",
                                         binding=out / "binding", seal_sha256="fake")
            control = {"rom": "fake-rom", "map": "fake-map", "boot_symbols": {}}
            with patch.object(worker, "verify_candidate", return_value=({}, {}, {})), \
                 patch.object(worker, "control_inputs", return_value=control), \
                 patch.object(worker, "reserve_frames") as reserve:
                worker.execute_control(args, io.StringIO(), factory=FakeCore)
            self.assertEqual(sum(call.args[-1] for call in reserve.call_args_list), 1500)
            report = json.loads((out / "control/report.json").read_text())
            self.assertEqual(report["local_frames"], 1500)
            self.assertFalse((out / "core-live").exists())

    def test_intro_Start_reaches_title_without_waiting_for_full_movie(self):
        class Intro:
            frame = 120
            held = [set(), set()]
            progress = None
            state = 1
            def step(self, count):
                self.frame += count
                if self.held[0]:
                    self.state = 2
            def hold(self, name):
                self.held[0].add(name)
            def release(self, name):
                self.held[0].clear()
            def read_u32(self, symbol):
                return self.state if symbol == "gCurrentGameState" else 0x80001000
        scenario = worker.Scenario.__new__(worker.Scenario)
        scenario.emu, scenario.progress = Intro(), None
        scenario.boot_to_title()
        self.assertEqual(scenario.emu.state, 2)
        self.assertEqual(scenario.emu.frame, 243)
        self.assertFalse(any(scenario.emu.held))

    def test_game_pak_check_acknowledged_before_campaign_entry(self):
        for already_ready in (False, True):
            values = {"gCurrentGameState": 4, "gScCampaignMenuReady": int(already_ready),
                      "gScCampaignRun": worker.ACTIVE}
            steps, presses = [], []
            scenario = worker.Scenario.__new__(worker.Scenario)
            scenario.emu = types.SimpleNamespace(read_u32=values.__getitem__, step=steps.append)
            scenario.progress, scenario.abi = None, {}

            def press(name):
                presses.append(name)
                if name == "N64_A":
                    values["gScCampaignMenuReady"] = 1
                elif name == "R":
                    values["gCurrentGameState"] = worker.STORY_STATE

            def wait(predicate, description):
                self.assertTrue(predicate(), description)

            scenario.press, scenario.wait = press, wait
            scenario.select = lambda *args, **kwargs: None
            scenario.ui = lambda: {"screen": 2}
            with patch.object(worker, "capture", return_value={"loaded": True}):
                self.assertEqual(scenario.enter_continue(), {"loaded": True})
            self.assertEqual(presses, ["R"] if already_ready else ["N64_A", "R"])
            self.assertEqual(steps, [] if already_ready else [60])

    def test_real_frontend_constructor_frame_is_leased_before_run(self):
        events = []

        def init(emu, *args, **kwargs):
            emu.frame = 0
            emu.step(1)

        def step(emu, n):
            events.append(("core-run", n))
            emu.frame += n

        with patch.object(Frontend, "__init__", init), patch.object(Frontend, "step", step), \
             patch.object(worker.LeasedFrontend, "verify_options"):
            emu = worker.LeasedFrontend(frame_lease=lambda n: events.append(("lease", n)),
                                       prepare_save=lambda emu: events.append(("setup", emu.frame)))
            self.assertEqual(events, [("setup", 0), ("lease", 1), ("core-run", 1)])
            self.assertEqual(emu.frame, 1)

            def refuse(n):
                raise HarnessError("refused")
            with self.assertRaises(HarnessError):
                worker.LeasedFrontend(frame_lease=refuse, prepare_save=lambda emu: None)
            self.assertEqual(len(events), 3)

    def test_registered_extra_memory_value_and_unknown_key_refusal(self):
        emu = worker.LeasedFrontend.__new__(worker.LeasedFrontend)
        emu.options = {"mupen64plus-pak1": "memory"}
        emu.registered_options = {}
        emu.requested_options = {"mupen64plus-pak1"}
        emu.progress = None
        emu.expansion_choice = None
        emu.register_option("mupen64plus-pak1", ["none", "memory"], "none",
                            ["None", "Memory"], "Controller Pak")
        emu.register_option("mupen64plus-DisableExtraMem", ["True", "False"], "True",
                            ["Enabled", "Disabled"], "Disable Expansion Pak")
        self.assertEqual(emu.options["mupen64plus-DisableExtraMem"], "False")
        emu.register_option(worker.ANGRYLION_THREADS_OPTION, ["1", "4"], "1",
                            ["One", "Four"], "Angrylion threads")
        self.assertTrue(emu._angrylion_threads_supported)
        emu.verify_options()
        emu.requested_options.add("invented")
        with self.assertRaisesRegex(HarnessError, "Unregistered"):
            emu.verify_options()

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
            worker.write_json(out / "transform/report.json",
                              dict(status="pass", transform=0, local_frames=7))
            report = collect(out, RunBudget(0, 180), 1, True)
            self.assertEqual(report["scenarios"]["transform"]["status"], "untested")
            self.assertEqual(report["frames_observed"], 7)

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
            start_frames = 0
            def step(self, n):
                if 3 in self.held[0]:
                    self.start_frames += n
            def read_u32(self, name):
                return 0x80001000
            def read_u16(self, address):
                ids = {0: 0x8000, 1: 0x4000, 10: 0x20, 11: 0x10,
                       4: 0x800, 5: 0x400, 3: 0x1000}
                if self.held[0]:
                    if 3 in self.held[0] and self.start_frames < 20:
                        return 0
                    return ids.get(next(iter(self.held[0])), 0)
                return {(0, 32767): 8, (32767, 0): 1,
                        (0, -32768): 4, (-32768, 0): 2}.get(tuple(self.analog[0][2:]), 0)
            def stick(self, x, y, right):
                self.analog[0][2:] = [x, y]
        core = Inputs()
        calibration = worker.calibrate(core)
        self.assertEqual(calibration["C_LEFT"], dict(kind="right-stick", xy=[-32768, 0]))
        self.assertEqual(core.start_frames, 20)
        self.assertEqual(worker.candidate_bindings()[-2:],
                         [dict(kind="joypad", id=0), dict(kind="joypad", id=3)])
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
                    hp=[5, 5, 5], moves=[33, 39, 145, 0], pp=[35, 40, 30, 0],
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
        self.assertEqual(presses[-1], "C_DOWN")  # Packed Bubble slot2.
        self.assertEqual(scenario.acknowledgements[-1]["frame"], 17)
        scenario.scenario = "loss-terminal-reload"
        with patch.object(worker, "read_menu", return_value=menu):
            scenario.battle_action()
        self.assertEqual(presses[-1], "C_RIGHT")  # Tail Whip, not Bubble.

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
