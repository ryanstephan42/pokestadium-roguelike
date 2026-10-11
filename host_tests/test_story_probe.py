import struct
import ctypes
import copy
import json
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
    require_ko_progress,
    require_ember_progress,
    require_ember_entry,
    read_progress,
    require_resume_native,
    require_learning_storage,
    require_probe_profile,
    read_native_machine,
    synthetic_selector_input,
    finish_synthetic_command,
    wait_natural_probe,
)
from tools.rltest.test_custom_battle import Mon


class StoryHarnessTests(unittest.TestCase):
    def test_ember_reload_failed_load_captures_empty_resident_without_native_read(self):
        from tools.rltest import story_probe
        raw = bytes(644)
        result = dict(active=0, state_address=0x80001000, state_size=644, status=4)
        emu = Mock()
        emu.read_u32.side_effect = lambda name: {
            "gScProbeMonSize": 68, "gScProbePartyOffset": 168,
            "gScProbePartyCountOffset": 21}[name]
        emu.read.return_value = raw
        with tempfile.TemporaryDirectory() as temporary, \
                patch.object(story_probe, "read_probe", return_value=result), \
                patch.object(story_probe, "memory_evidence", return_value={}), \
                patch.object(story_probe, "read_progress", return_value={}), \
                patch.object(story_probe, "read_bridge", return_value={}), \
                patch.object(story_probe, "save_ram", return_value=(0, b"failed-save")), \
                patch.object(story_probe, "read_native_machine") as machine:
            directory = Path(temporary)
            captured = story_probe.capture_progress(
                emu, directory, "ember_reload-failure", allow_empty=True)
            self.assertEqual(captured[2:4], (raw, []))
            self.assertEqual((directory / "ember_reload-failure-save.bin").read_bytes(), b"failed-save")
            self.assertEqual((directory / "ember_reload-failure-state.bin").read_bytes(), raw)
            machine.assert_not_called()
            with self.assertRaises(HarnessError):
                decode_party(emu, result)

    def test_ember_reload_expected_immutable_source(self):
        from tools.rltest import story_probe
        source = Path(".local/rltest/story-ember-e2e-f90d18a")
        if not source.exists():
            self.skipTest("Accepted native evidence is local and intentionally not committed")
        with tempfile.TemporaryDirectory() as temporary:
            expected = story_probe.ember_reload_expected(source, Path(temporary))
            self.assertEqual(expected["party"][0]["moves"], [10, 45, 0, 52])
            self.assertEqual(len(expected["raw"]), 644)
            self.assertEqual(len(expected["save"]), 296960)
            note = Path(temporary) / "ember_reload-source-note.bin"
            state = Path(temporary) / "ember_reload-expected-state.bin"
            for codec in ("test_story_ember", "test_story_ember_san"):
                command = [str(Path("build/story") / codec), "--validate-reload", str(note), str(state)]
                self.assertEqual(subprocess.run(command, capture_output=True).returncode, 0)
                original = note.read_bytes()
                changed = bytearray(original)
                for offset in (8, 768 + 8):
                    changed[offset] ^= 1
                note.write_bytes(changed)
                self.assertNotEqual(subprocess.run(command, capture_output=True).returncode, 0)
                note.write_bytes(original)
                raw = state.read_bytes()
                changed = bytearray(raw)
                changed[7] ^= 1
                state.write_bytes(changed)
                self.assertNotEqual(subprocess.run(command, capture_output=True).returncode, 0)
                state.write_bytes(raw)
            with self.assertRaises(HarnessError):
                story_probe.ember_reload_expected(source, Path(temporary))

    def test_ember_reload_exact_state_replay_write_and_resource_refusals(self):
        from tools.rltest import story_probe
        result = dict(command=0, status=0, phase=3, interrupted=0, completed_crc=123,
                      active=0, ticks=0, ko_count=0, logic_steps=0, presentation_steps=0,
                      pause_ticks=0, resume=0, force_ko=0, cold_calls=1, cold_busy=0,
                      cold_address=0, cold_before=1234, cold_after=1234)
        progress = dict(ko_captures=0, resumes=0, terminal=0, error=0, injected=0,
                        award=0, learn_move=0)
        expected = dict(result=dict(completed_crc=123), raw=b"full-state", party=[dict(exp=437)],
                        save=b"unchanged-save")
        story_probe.require_ember_reload(result, progress, expected["raw"], expected["party"],
                                         expected["save"], expected["save"], expected)
        for key, value in (("interrupted", 1), ("phase", 1), ("completed_crc", 124),
                           ("ticks", 1), ("ko_count", 1), ("cold_calls", 2), ("cold_after", 1)):
            wrong = {**result, key: value}
            with self.assertRaises(HarnessError):
                story_probe.require_ember_reload(wrong, progress, expected["raw"], expected["party"],
                                                 expected["save"], expected["save"], expected)
        for raw, party, save in ((b"changed-offer", expected["party"], expected["save"]),
                                 (expected["raw"], [dict(exp=536)], expected["save"]),
                                 (expected["raw"], expected["party"], b"changed-save")):
            with self.assertRaises(HarnessError):
                story_probe.require_ember_reload(result, progress, raw, party,
                                                 expected["save"], save, expected)
        with self.assertRaises(HarnessError):
            story_probe.require_ember_reload(result, {**progress, "resumes": 1},
                                             expected["raw"], expected["party"],
                                             expected["save"], expected["save"], expected)

    def test_ember_reload_only_no_cleanup_and_failure_capture(self):
        from tools.rltest import story_probe
        emu = Mock(frame=0)
        emu.read_u32.return_value = 0
        emu.symbols.address.side_effect = lambda name: 0 if name == "D_80080000" else 0x18E0
        emu.read.return_value = bytes(0x18E0)
        expected = dict(save=b"save")
        result = dict(command=0, status=0)
        with tempfile.TemporaryDirectory() as temporary, \
                patch.object(story_probe, "start_command") as start, \
                patch.object(story_probe, "read_probe", return_value=result), \
                patch.object(story_probe, "capture_progress",
                             return_value=(result, {}, b"raw", [], {})) as capture, \
                patch.object(story_probe, "save_ram", return_value=(0, b"save")), \
                patch.object(story_probe, "require_ember_reload"), \
                patch.object(story_probe, "validate_memory"), \
                patch.object(story_probe, "read_native_machine") as machine:
            directory = Path(temporary)
            self.assertEqual(story_probe.ember_reload_only(emu, directory, b"save", expected), result)
            start.assert_called_once_with(emu, 1)
            self.assertEqual(emu.wait.call_args.kwargs["frames"], 6000)
            emu.step.assert_not_called()
            emu.wait_state.assert_not_called()
            machine.assert_not_called()
            for error in (HarnessError("load timeout"), HarnessError("load error")):
                emu.wait.side_effect = error
                with self.assertRaises(HarnessError):
                    story_probe.ember_reload_only(emu, directory, b"save", expected)
                self.assertEqual(capture.call_args.args[2], "ember_reload-failure")
                emu.step.assert_not_called()

    def test_ember_reload_wait_exact_cap_early_refusal_and_history(self):
        from tools.rltest import story_probe
        from tools.rltest.frontend import Frontend
        progress = dict(ko_captures=0, resumes=0, terminal=0, error=0, injected=0,
                        award=0, learn_move=0)
        result = dict(command=1, status=0xFFFFFFFF, active=0, ticks=0, ko_count=0,
                      logic_steps=0, presentation_steps=0, pause_ticks=0, resume=0, force_ko=0)
        for case in ("exact_cap", "timeout", "load_error", "replay", "injection", "native_launch"):
            with self.subTest(case=case), tempfile.TemporaryDirectory() as temporary:
                emu = Mock(frame=100)
                emu.symbols.address.side_effect = lambda name: 0 if name == "D_80080000" else 0x18E0
                emu.read.return_value = bytes(0x18E0)
                emu.wait.side_effect = lambda predicate, **kw: Frontend.wait(emu, predicate, **kw)
                emu.step.side_effect = lambda frames=1: setattr(emu, "frame", emu.frame + frames)
                emu.read_u32.side_effect = lambda symbol: int(
                    case == "native_launch" and symbol == "gRlBattleActive" and emu.frame == 113)
                def probe(instance):
                    value = result.copy()
                    if case == "exact_cap" and instance.frame == 6112:
                        value.update(command=0, status=0)
                    if instance.frame == 113:
                        if case == "load_error":
                            value["status"] = 4
                        elif case == "replay":
                            value["ticks"] = 1
                    return value
                def progression(instance):
                    return {**progress, "injected": int(case == "injection" and instance.frame == 113)}
                with patch.object(story_probe, "start_command", side_effect=lambda *a: emu.step(12)), \
                        patch.object(story_probe, "read_probe", side_effect=probe), \
                        patch.object(story_probe, "read_progress", side_effect=progression), \
                        patch.object(story_probe, "capture_progress",
                                     return_value=(result, progress, b"raw", [], {})) as capture, \
                        patch.object(story_probe, "save_ram", return_value=(0, b"save")), \
                        patch.object(story_probe, "require_ember_reload"), \
                        patch.object(story_probe, "validate_memory"), \
                        patch.object(story_probe, "read_native_machine") as native:
                    directory = Path(temporary)
                    if case == "exact_cap":
                        story_probe.ember_reload_only(emu, directory, b"save", dict(save=b"save"))
                        self.assertEqual(emu.frame, 6112)
                        self.assertEqual(capture.call_args.args[2], "ember_reload-loaded")
                    else:
                        with self.assertRaises(HarnessError):
                            story_probe.ember_reload_only(emu, directory, b"save", dict(save=b"save"))
                        self.assertEqual(emu.frame, 6112 if case == "timeout" else 113)
                        self.assertEqual(capture.call_args.args[2], "ember_reload-failure")
                    rows = (directory / "ember_reload-load-history.jsonl").read_text().splitlines()
                    self.assertEqual(json.loads(rows[-1])["frame"], emu.frame)
                    frames = json.loads((directory / "ember_reload-frames.json").read_text())
                    self.assertEqual(frames["command_launch_end_frame"], 112)
                    self.assertEqual(frames["operation_frames"], emu.frame - 100)
                    emu.press.assert_not_called()
                    emu.write.assert_not_called()
                    emu.write_u32.assert_not_called()
                    native.assert_not_called()

    def test_ember_reload_preflight_refuses_hash_mutation_before_codec(self):
        from tools.rltest import story_probe
        source = Path(".local/rltest/story-ember-e2e-f90d18a")
        if not source.exists():
            self.skipTest("Accepted native evidence is local and intentionally not committed")
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            modified = root / "source"
            modified.mkdir()
            output = root / "output"
            output.mkdir()
            for name in ("save-ram.bin", "completed-party.json", "completed-result.json",
                         "ko_ember_e2e-terminal-state.bin", "evidence.sha256", "grant-provenance.json"):
                (modified / name).write_bytes((source / name).read_bytes())
            for name in ("save-ram.bin", "completed-party.json", "completed-result.json",
                         "ko_ember_e2e-terminal-state.bin", "evidence.sha256", "grant-provenance.json"):
                original = (modified / name).read_bytes()
                (modified / name).write_bytes(original + b"altered")
                with patch.object(story_probe.subprocess, "run") as codec, \
                        self.assertRaises(HarnessError):
                    story_probe.ember_reload_expected(modified, output)
                codec.assert_not_called()
                self.assertEqual(list(output.iterdir()), [])
                (modified / name).write_bytes(original)

    def test_ember_budget_finish_has_no_hidden_steps(self):
        from tools.rltest import story_probe
        emu = Mock(frame=18060)
        budget = story_probe.EmberCombatBudget(0)
        budget.exclude_hold(9000, 9060)
        with patch.object(story_probe, "read_probe", return_value=dict(command=0)) as read:
            self.assertEqual(story_probe.finish_ember_budget_command(emu, budget), dict(command=0))
            read.assert_called_once_with(emu)
            emu.release.assert_called_once_with("L")
            emu.step.assert_not_called()
            emu.wait.assert_not_called()
            emu.wait_state.assert_not_called()
            read.return_value = dict(command=3)
            with self.assertRaises(HarnessError):
                story_probe.finish_ember_budget_command(emu, budget)
            emu.wait_state.assert_not_called()

    def test_ember_shared_budget_exact_cap_and_hold_exclusion(self):
        from tools.rltest import story_probe
        from tools.rltest.frontend import Frontend
        emu = Mock(frame=100)
        budget = story_probe.EmberCombatBudget(emu.frame)
        emu.wait.side_effect = lambda predicate, **kwargs: Frontend.wait(emu, predicate, **kwargs)
        emu.step.side_effect = lambda frames=1: setattr(emu, "frame", emu.frame + frames)
        def probe(instance):
            if instance.frame <= 7100:
                return dict(command=3, phase=2 if instance.frame == 7100 else 1,
                            active=0, force_ko=0, status=0xFFFFFFFF)
            return dict(command=0 if instance.frame == 18160 else 3, phase=1,
                        active=0, force_ko=0, status=0xFFFFFFFF, resume=1)
        with tempfile.TemporaryDirectory() as temporary, \
                patch.object(story_probe, "read_probe", side_effect=probe), \
                patch.object(story_probe, "read_progress", return_value=dict(error=0, injected=0)), \
                patch.object(story_probe, "read_native_machine") as native, \
                patch.object(story_probe, "synthetic_selector_input") as selector:
            directory = Path(temporary)
            wait_natural_probe(emu, directory, True, "ko_ember_e2e", budget)
            self.assertEqual(emu.frame, 7100)
            self.assertEqual(budget.evidence(emu.frame)["combat_remaining"], 11000)
            start = emu.frame
            emu.step(60)
            budget.exclude_hold(start, emu.frame)
            wait_natural_probe(emu, directory, False, "ko_ember_e2e", budget)
            self.assertEqual(emu.frame, 18160)
            self.assertEqual(budget.evidence(emu.frame)["combat_used"], 18000)
            rows = (directory / "ko_ember_e2e-postresume-machine-history.jsonl").read_text().splitlines()
            self.assertEqual(len(rows), 11001)
            self.assertEqual(json.loads(rows[-1])["budget"]["combat_remaining"], 0)
            self.assertEqual(json.loads(rows[-1])["budget"]["render_hold_used"], 60)
            native.assert_not_called()
            selector.assert_not_called()
            emu.set_input.assert_not_called()
            emu.write_u32.assert_not_called()
        with self.assertRaises(HarnessError):
            budget.exclude_hold(18160, 18220)

    def test_ember_shared_budget_timeout_remaining_and_early_refusals(self):
        from tools.rltest import story_probe
        from tools.rltest.frontend import Frontend
        for overrides, progress, expected_frame in (
                ({}, dict(error=0, injected=0), 18000),
                (dict(command=0), dict(error=0, injected=0), 17998),
                (dict(status=0x101), dict(error=0, injected=0), 17998),
                ({}, dict(error=1, injected=0), 17998),
                ({}, dict(error=0, injected=1), 17998)):
            emu = Mock(frame=17998)
            budget = story_probe.EmberCombatBudget(0)
            emu.wait.side_effect = lambda predicate, **kwargs: Frontend.wait(emu, predicate, **kwargs)
            emu.step.side_effect = lambda frames=1: setattr(emu, "frame", emu.frame + frames)
            probe = dict(command=3, phase=1, active=0, force_ko=0, status=0xFFFFFFFF)
            probe.update(overrides)
            with tempfile.TemporaryDirectory() as temporary, \
                    patch.object(story_probe, "read_probe", return_value=probe), \
                    patch.object(story_probe, "read_progress", return_value=progress):
                directory = Path(temporary)
                with self.assertRaises(HarnessError):
                    wait_natural_probe(emu, directory, True, "ko_ember_e2e", budget)
                self.assertEqual(emu.frame, expected_frame)
                rows = (directory / "ko_ember_e2e-prelearning-machine-history.jsonl").read_text().splitlines()
                self.assertEqual(json.loads(rows[-1])["frame"], expected_frame)
                self.assertTrue((directory / "ko_ember_e2e-prelearning-budget.json").exists())
                emu.set_input.assert_not_called()
                emu.write_u32.assert_not_called()
        with self.assertRaises(HarnessError):
            story_probe.EmberCombatBudget(0).exclude_hold(0, 61)

    def test_ember_entry_only_stops_without_combat_or_resume(self):
        from tools.rltest import story_probe
        emu = Mock()
        result = dict(active=1, ticks=1, force_ko=0, resume=0, cold_calls=1,
                      cold_busy=0, cold_address=0, cold_before=123, cold_after=123)
        progress = dict(error=0, injected=0, ko_captures=0, resumes=0, terminal=0)
        with tempfile.TemporaryDirectory() as temporary, \
                patch.object(story_probe, "start_command") as start, \
                patch.object(story_probe, "wait_probe") as wait, \
                patch.object(story_probe, "capture_progress",
                             return_value=(result, progress, b"raw", [dict()], dict())) as capture, \
                patch.object(story_probe, "save_ram", return_value=(0, b"save")), \
                patch.object(story_probe, "require_ember_entry"), \
                patch.object(story_probe, "validate_memory"), \
                patch.object(story_probe, "read_native_machine", return_value=dict()), \
                patch.object(story_probe, "require_ember_native_entry"), \
                patch.object(story_probe, "story_owned_changes"), \
                patch.object(story_probe, "find_pak", return_value=0), \
                patch.object(story_probe, "wait_natural_probe") as natural, \
                patch.object(story_probe, "set_field") as write:
            directory = Path(temporary)
            self.assertEqual(story_probe.ember_entry_only(emu, directory, b"before"), result)
            start.assert_called_once_with(emu, 3)
            self.assertEqual(wait.call_args.args[2], 1800)
            capture.assert_called_once_with(emu, directory, "ember_entry-entry")
            self.assertEqual((directory / "ember_entry-prebattle-save.bin").read_bytes(), b"save")
            emu.step.assert_not_called()
            natural.assert_not_called()
            write.assert_not_called()
            capture.reset_mock()
            progress["error"] = 1
            with self.assertRaises(HarnessError):
                story_probe.ember_entry_only(emu, directory, b"before")
            self.assertEqual(capture.call_args.args[2], "ember_entry-failure")
            natural.assert_not_called()
            emu.step.assert_not_called()

    def test_ember_native_entry_exact_fields(self):
        from tools.rltest import story_probe
        sides = []
        for species, level, dvs, hp, stats, moves, pp in (
                (4, 8, 0x4528, 24, [24, 14, 12, 16, 13], [10, 45, 0, 0], [35, 40, 0, 0]),
                (13, 9, 0x8889, 27, [27, 12, 11, 15, 10], [40, 81, 0, 0], [35, 40, 0, 0])):
            party, live = bytearray(0x54), bytearray(0x94)
            party[0] = live[0x0B] = species
            party[0x24] = live[0x26] = level
            struct.pack_into(">H", party, 0x1E, dvs)
            struct.pack_into(">H", live, 0x24, dvs)
            for i, value in enumerate(stats):
                struct.pack_into(">H", party, 0x26 + 2 * i, value)
                struct.pack_into(">H", live, 0x28 + 2 * i, value)
            sides.append(dict(flags=3, count=1, active=0, hp=hp, party_hp=[hp],
                              moves=moves, pp=pp, party_pp=pp, status=0, stages=[7] * 6,
                              team_raw=(bytes(0x1C) + party).hex(),
                              control_raw=(bytes(0x38) + live).hex()))
        story_probe.require_ember_native_entry(dict(sides=sides))
        for key, value in (("hp", 23), ("flags", 1), ("status", 4), ("count", 2)):
            wrong = copy.deepcopy(sides)
            wrong[0][key] = value
            with self.assertRaises(HarnessError):
                story_probe.require_ember_native_entry(dict(sides=wrong))

    def test_ember_exact_ko_resume_and_terminal_rejections(self):
        before = dict(species=4, level=8, exp=338, dvs=0x4528, hp=17, max_hp=24,
                      status=0x40, fainted=0, moves=[10, 45, 0, 0], pp=[31, 39, 0, 0],
                      max_pp=[35, 40, 0, 0], stat_exp=[129, 113, 130, 138, 90],
                      current_stats=[24, 14, 12, 16, 13])
        after = {**before, "level": 9, "exp": 437, "hp": 19, "max_hp": 26,
                 "stat_exp": [169, 148, 160, 188, 110], "current_stats": [26, 16, 13, 17, 14]}
        resumed = {**after, "moves": [10, 45, 0, 52], "pp": [31, 39, 0, 25],
                   "max_pp": [35, 40, 0, 25]}
        evidence = dict(error=0, ko_captures=1, injected=0, award=99, learn_move=52,
                        resumes=1, terminal=1, ko_stages=[7, 6, 5, 7, 7, 7],
                        resume_stages=[7, 6, 5, 7, 7, 7], before_ko=before,
                        after_ko=after, after_resume=resumed,
                        terminal_native=copy.deepcopy(resumed), terminal_resident=resumed)
        require_ember_progress(evidence, terminal=True)
        for key, value in (("injected", 1), ("award", 100), ("ko_captures", 2),
                           ("learn_move", 73), ("resumes", 2), ("terminal", 0)):
            wrong = copy.deepcopy(evidence)
            wrong[key] = value
            with self.assertRaises(HarnessError):
                require_ember_progress(wrong, terminal=True)
        for name, key, value in (("after_ko", "hp", 18), ("after_ko", "status", 0),
                                 ("before_ko", "dvs", 0x8889),
                                 ("after_resume", "pp", [31, 39, 0, 10]),
                                 ("terminal_native", "exp", 536)):
            wrong = copy.deepcopy(evidence)
            wrong[name][key] = value
            with self.assertRaises(HarnessError):
                require_ember_progress(wrong, terminal=True)

    def test_ember_entry_requires_exact_graph_checkpoint(self):
        mon = dict(species=4, dvs=0x4528, level=8, exp=338, hp=24, max_hp=24,
                   status=0, fainted=0, moves=[10, 45, 0, 0], pp=[35, 40, 0, 0],
                   max_pp=[35, 40, 0, 0], stat_exp=[129, 113, 130, 138, 90],
                   current_stats=[24, 14, 12, 16, 13])
        raw = bytearray(644)
        struct.pack_into(">4I", raw, 0, 42, 476557059, 0, 1)
        raw[16:20] = bytes([0, 0, 0, 4])
        struct.pack_into(">H", raw, 36, 351)
        for index in (57, 349):
            raw[38 + index // 8] |= 1 << (index % 8)
        result = dict(phase=1, ko_count=0)
        require_ember_entry(result, raw, [mon])
        for offset in (0, 16, 22, 36, 38 + 350 // 8, 102):
            wrong = bytearray(raw)
            wrong[offset] ^= 1
            with self.assertRaises(HarnessError):
                require_ember_entry(result, wrong, [mon])
        with self.assertRaises(HarnessError):
            require_ember_entry(result, raw, [{**mon, "exp": 419}])

    def test_ember_profile_is_mutually_exclusive_before_frontend(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "probe.map"
            path.write_text("gScProbeKoProfile\ngScProbeEmberProfile\n")
            require_probe_profile(path, "ko_ember")
            require_probe_profile(path, "ko_ember_e2e")
            require_probe_profile(path, "ember_reload")
            for phase in ("ko_natural", "ko_synthetic", "bridge_init", "create", "ko_reload"):
                with self.assertRaises(HarnessError):
                    require_probe_profile(path, phase)
            path.write_text("gScProbeKoProfile\n")
            with self.assertRaises(HarnessError):
                require_probe_profile(path, "ko_ember")
            with self.assertRaises(HarnessError):
                require_probe_profile(path, "ko_ember_e2e")
            with self.assertRaises(HarnessError):
                require_probe_profile(path, "ember_reload")

    def test_ember_passive_wait_preserves_budget_scope_and_no_writes(self):
        from tools.rltest import story_probe
        from tools.rltest.frontend import Frontend
        emu = Mock(frame=0)
        emu.wait.side_effect = lambda predicate, **kwargs: Frontend.wait(emu, predicate, **kwargs)
        probe = dict(command=3, status=0xFFFFFFFF, active=0, phase=1, force_ko=0)
        with tempfile.TemporaryDirectory() as temporary, \
                patch.object(story_probe, "read_probe", return_value=probe), \
                patch.object(story_probe, "read_progress", return_value=dict(injected=0, error=0)), \
                patch.object(story_probe, "read_native_machine") as native, \
                patch.object(story_probe, "synthetic_selector_input") as selector:
            directory = Path(temporary)
            for learning in (True, False):
                emu.frame = 0
                emu.step.side_effect = lambda frames=1: setattr(emu, "frame", emu.frame + frames)
                with self.assertRaises(HarnessError):
                    wait_natural_probe(emu, directory, learning, phase_name="ko_ember")
                self.assertEqual(emu.frame, 6000)
                label = "prelearning" if learning else "postresume"
                rows = (directory / f"ko_ember-{label}-machine-history.jsonl").read_text().splitlines()
                self.assertEqual(len(rows), 6001)
                last = json.loads(rows[-1])
                self.assertEqual(last["frame"], 6000)
                self.assertIn("not-native-wins", last["probe"]["coverage_scope"])
            native.assert_not_called()
            selector.assert_not_called()
            emu.set_input.assert_not_called()
            emu.write_u32.assert_not_called()

    def test_natural_waits_record_before_step_without_input_or_writes(self):
        from tools.rltest import story_probe
        from tools.rltest.frontend import Frontend
        emu = Mock(frame=0)
        emu.wait.side_effect = lambda predicate, **kwargs: Frontend.wait(emu, predicate, **kwargs)
        probe = dict(command=3, status=0xFFFFFFFF, active=1, phase=1, force_ko=0)
        progress = dict(injected=0, error=0)
        machine = dict(sides=[dict(flags=3, port=1, hp=22)], ai_timer_bits=0)
        with tempfile.TemporaryDirectory() as temporary, \
                patch.object(story_probe, "read_probe",
                             side_effect=[probe, {**probe, "phase": 2},
                                          {**probe, "phase": 2, "resume": 1}, probe,
                                          {**probe, "command": 0, "active": 0}]), \
                patch.object(story_probe, "read_progress", return_value=progress), \
                patch.object(story_probe, "read_native_machine",
                             return_value=machine) as native, \
                patch.object(story_probe, "synthetic_selector_input") as selector:
            directory = Path(temporary)

            def step(frames=1):
                name = "prelearning" if emu.frame == 0 else "postresume"
                rows = (directory / f"ko_natural-{name}-machine-history.jsonl").read_text().splitlines()
                self.assertEqual(json.loads(rows[-1])["frame"], emu.frame)
                emu.frame += frames

            emu.step.side_effect = step
            wait_natural_probe(emu, directory, learning=True)
            self.assertEqual(emu.frame, 1)
            wait_natural_probe(emu, directory, learning=False)
            self.assertEqual(emu.frame, 3)
            rows = [json.loads(row) for row in (
                directory / "ko_natural-postresume-machine-history.jsonl").read_text().splitlines()]
            self.assertEqual(len(rows), 3)
            self.assertEqual(rows[0]["probe"]["resume"], 1)
            self.assertIsNone(rows[-1]["machine"])
            self.assertEqual(native.call_count, 4)
            selector.assert_not_called()
        emu.hold.assert_not_called()
        emu.stick.assert_not_called()
        emu.release.assert_not_called()
        emu.write.assert_not_called()
        emu.write_u32.assert_not_called()
        self.assertTrue(all(call.kwargs["frames"] == 6000 for call in emu.wait.call_args_list))

    def test_natural_wait_refuses_loss_errors_and_injection_after_history(self):
        from tools.rltest import story_probe
        from tools.rltest.frontend import Frontend
        baseline = dict(command=3, status=0xFFFFFFFF, active=1, phase=1, force_ko=0)
        cases = [
            (True, {**baseline, "command": 0, "active": 0}, dict(injected=0, error=0),
             "ended without learning"),
            (False, {**baseline, "status": 257}, dict(injected=0, error=0), "failed"),
            (False, baseline, dict(injected=0, error=1), "failed"),
            (True, {**baseline, "force_ko": 1}, dict(injected=0, error=0), "synthetic"),
            (False, baseline, dict(injected=1, error=0), "synthetic"),
        ]
        for learning, probe, progress, message in cases:
            with self.subTest(message=message, learning=learning), \
                    tempfile.TemporaryDirectory() as temporary, \
                    patch.object(story_probe, "read_probe", return_value=probe), \
                    patch.object(story_probe, "read_progress", return_value=progress), \
                    patch.object(story_probe, "read_native_machine", return_value={}) as native:
                emu = Mock(frame=0)
                emu.wait.side_effect = lambda predicate, **kwargs: Frontend.wait(
                    emu, predicate, **kwargs)
                with self.assertRaisesRegex(HarnessError, message):
                    wait_natural_probe(emu, Path(temporary), learning)
                label = "prelearning" if learning else "postresume"
                row = json.loads((Path(temporary) /
                                  f"ko_natural-{label}-machine-history.jsonl").read_text())
                self.assertEqual(row["probe"], probe)
                self.assertEqual(row["progress"], progress)
                if not probe["active"]:
                    native.assert_not_called()
                emu.step.assert_not_called()
                emu.write.assert_not_called()
                emu.hold.assert_not_called()
                emu.stick.assert_not_called()

    def test_natural_wait_deadlines_unchanged_in_both_stages(self):
        from tools.rltest import story_probe
        from tools.rltest.frontend import Frontend
        probe = dict(command=3, status=0xFFFFFFFF, active=1, phase=1, force_ko=0)
        for learning in (True, False):
            with self.subTest(learning=learning), tempfile.TemporaryDirectory() as temporary, \
                    patch.object(story_probe, "read_probe", return_value=probe), \
                    patch.object(story_probe, "read_progress", return_value=dict(injected=0, error=0)), \
                    patch.object(story_probe, "read_native_machine", return_value={}):
                emu = Mock(frame=7)
                emu.wait.side_effect = lambda predicate, **kwargs: Frontend.wait(
                    emu, predicate, **kwargs)
                emu.step.side_effect = lambda frames=1: setattr(emu, "frame", emu.frame + frames)
                with self.assertRaisesRegex(HarnessError, "Timed out"):
                    wait_natural_probe(emu, Path(temporary), learning)
                self.assertEqual(emu.frame, 6007)
                label = "prelearning" if learning else "postresume"
                rows = (Path(temporary) /
                        f"ko_natural-{label}-machine-history.jsonl").read_text().splitlines()
                self.assertEqual(len(rows), 6001)
                self.assertEqual(json.loads(rows[-1])["frame"], 6007)
                emu.write.assert_not_called()
                emu.hold.assert_not_called()
                emu.stick.assert_not_called()

    def test_natural_completed_failure_capture_avoids_inactive_machine(self):
        from tools.rltest import story_probe
        result = dict(active=0, command=0, status=0, phase=3)
        with tempfile.TemporaryDirectory() as temporary, \
                patch.object(story_probe, "read_probe", return_value=result), \
                patch.object(story_probe, "memory_evidence", return_value={}), \
                patch.object(story_probe, "read_progress", return_value={"terminal": 1}), \
                patch.object(story_probe, "read_bridge", return_value={"carry_live": {}}), \
                patch.object(story_probe, "decode_party", return_value=(b"state", [{"hp": 0}])), \
                patch.object(story_probe, "save_ram", return_value=(0, b"persisted-save")), \
                patch.object(story_probe, "read_native_machine") as native:
            directory = Path(temporary)
            story_probe.capture_progress(Mock(), directory, "ko_natural-failure")
            self.assertEqual((directory / "ko_natural-failure-save.bin").read_bytes(),
                             b"persisted-save")
            self.assertEqual((directory / "ko_natural-failure-state.bin").read_bytes(), b"state")
            self.assertEqual(json.loads((directory / "ko_natural-failure-party.json").read_text()),
                             [{"hp": 0}])
            self.assertEqual(json.loads((directory / "ko_natural-failure-result.json").read_text()),
                             result)
            native.assert_not_called()

    def test_cpu_command_never_drives_human_input_and_logs_counters(self):
        from tools.rltest import story_probe
        machine = dict(mode=1, sequence=0, result=0,
                       sides=[dict(flags=3, port=1, hp=22, menu=0, choice=0)])
        emu = Mock(frame=0)
        emu.step.side_effect = lambda: setattr(emu, "frame", emu.frame + 1)
        probe = dict(command=3, status=0xFFFFFFFF, active=1, phase=1, resume=0,
                     ticks=1, logic_steps=1, presentation_steps=1, pause_ticks=0)
        with tempfile.TemporaryDirectory() as temporary, \
                patch.object(story_probe, "read_probe",
                             side_effect=[probe, {**probe, "logic_steps": 2}, {"command": 0}]), \
                patch.object(story_probe, "read_native_machine", return_value=machine), \
                patch.object(story_probe, "finish_command", return_value={"command": 0}):
            finish_synthetic_command(emu, Path(temporary))
            emu.hold.assert_not_called()
            history = (Path(temporary) / "ko_synthetic-machine-history.jsonl").read_text()
            self.assertNotIn('"input":', history)
            self.assertEqual(len(history.splitlines()), 2)
        emu.write.assert_not_called()

    def test_native_machine_fresh_enemy_party_and_bounds(self):
        machine_address, side0, side1 = 0x80100000, 0x80101000, 0x80103000
        team0, team1 = 0x800AE544, 0x800AE760
        machine = bytearray(0xE0)
        struct.pack_into(">I", machine, 0x1C, 1)
        control0, control1 = bytearray(0xCC), bytearray(0xCC)
        struct.pack_into(">H", control0, 0x44, 24)
        struct.pack_into(">I", control0, 0xC8, 0x80010000)
        struct.pack_into(">I", control1, 0xC8, 0x80010020)
        team = bytearray(0x21C)
        team[2] = 1
        struct.pack_into(">H", team, 0x1E, 24)
        foe = bytearray(team)
        struct.pack_into(">H", foe, 0x1E, 0)
        blocks = {machine_address: machine, side0 + 0x654: control0,
                  side1 + 0x654: control1, team0: team, team1: foe,
                  0x80010000: bytes(12), 0x80010020: bytes(12)}
        ai, port, pad = bytearray(0x20), bytearray(0x24), bytearray(6)
        struct.pack_into(">I", ai, 0, 0x3F800000)
        struct.pack_into(">I", ai, 0x18, 33)
        struct.pack_into(">I", port, 0x20, 0x80020100)
        struct.pack_into(">H", port, 6, 0x8000)
        struct.pack_into(">H", pad, 0, 0x8000)
        thread = bytearray(0x128)
        struct.pack_into(">I", thread, 284, 0x8437FC10)
        blocks.update({0x80200100: ai, 0x80020000: port, 0x80020100: pad,
                       0x80200300: thread})
        pointers = {0x80200000: machine_address, 0x80200010: side0,
                    0x80200014: side1, side0 + 0x724: team0, side1 + 0x724: team1}
        pointers.update({0x80200200: side0 + 0x68C, 0x80200204: side1 + 0x68C,
                         0x80200208: 0, 0x8020020C: side0, 0x80200210: side1})
        emu = Mock()
        emu.resolve.side_effect = {"D_84390240": 0x80200000,
                                   "D_84390010": 0x80200010, "D_84390124": 0x80200104,
                                   "gControllers": 0x80020000,
                                   "D_843C5238": 0x80200200, "D_843C523C": 0x80200204,
                                   "D_84390288": 0x80200208, "D_84390204": 0x8020020C,
                                   "D_84390200": 0x80200210, "pThreads": 0x80200300}.__getitem__
        emu.read_u32.side_effect = pointers.__getitem__
        emu.read.side_effect = lambda address, size: bytes(blocks[address][:size])
        initial = read_native_machine(emu)
        self.assertEqual(initial["sides"][1]["living"], 0)
        self.assertEqual(initial["port0"]["down"], 0x8000)
        self.assertEqual(initial["port0"]["pad_button"], 0x8000)
        self.assertEqual(initial["sides"][0]["buttons_down"], 0)
        self.assertEqual(initial["ai_timer_bits"], 0x3F800000)
        self.assertEqual(initial["ai_choice"], 33)
        self.assertEqual(initial["saved_thread_context"]["pc"], 0x8437FC10)
        struct.pack_into(">I", ai, 0x1C, 1)
        team[0:2] = b"\3\1"
        struct.pack_into(">H", foe, 0x1E, 3)
        struct.pack_into(">H", control1, 0x44, 3)
        current = read_native_machine(emu)
        self.assertEqual(current["sides"][1]["party_hp"], [3])
        self.assertEqual(current["sides"][1]["hp"], 3)
        self.assertEqual(current["ai_pending"], 1)
        self.assertEqual(current["sides"][0]["flags"], 3)
        self.assertEqual(current["sides"][0]["port"], 1)
        self.assertEqual(initial["sides"][1]["party_hp"], [0])
        pointers[side1 + 0x724] = 0x80400000
        with self.assertRaisesRegex(HarnessError, "cap4"):
            read_native_machine(emu)
        pointers[side1 + 0x724] = team1
        struct.pack_into(">I", control1, 8, 1)
        with self.assertRaisesRegex(HarnessError, "party bounds"):
            read_native_machine(emu)

    def test_synthetic_selector_only_drives_unqueued_fight_and_move(self):
        probe = dict(active=1, phase=1, resume=0)
        machine = dict(mode=1, sequence=0, result=0,
                       sides=[dict(flags=1, port=0, hp=24, menu=0, choice=0, moves=[33, 45, 0, 73],
                                   pp=[35, 40, 0, 10])])
        self.assertEqual(synthetic_selector_input(probe, machine), "fight")
        machine["sides"][0].update(menu=1, choice=1)
        self.assertEqual(synthetic_selector_input(probe, machine), "move0")
        for changed_probe in ({**probe, "phase": 2}, {**probe, "resume": 1},
                              {**probe, "active": 0}):
            self.assertIsNone(synthetic_selector_input(changed_probe, machine))
        for key, value in (("mode", 2), ("sequence", 1), ("result", 1)):
            self.assertIsNone(synthetic_selector_input(probe, {**machine, key: value}))
        for changed_side in (dict(hp=0, menu=1, choice=1),
                             dict(hp=24, menu=3, choice=1),
                             dict(hp=24, menu=0, choice=2),
                             dict(hp=24, menu=1, choice=0)):
            self.assertIsNone(synthetic_selector_input(
                probe, {**machine, "sides": [{"flags": 1, "port": 0, **changed_side}]}))
        for flags, port in ((3, 1), (3, 0), (1, 1)):
            side = {**machine["sides"][0], "flags": flags, "port": port}
            self.assertIsNone(synthetic_selector_input(probe, {**machine, "sides": [side]}))
        machine["sides"][0]["pp"][0] = 0
        with self.assertRaisesRegex(HarnessError, "usable native move"):
            synthetic_selector_input(probe, machine)

    def test_synthetic_command_preserves_deadline_and_releases_input(self):
        from tools.rltest import story_probe
        emu = Mock(frame=0)
        emu.step.side_effect = lambda: setattr(emu, "frame", emu.frame + 1)
        probe = dict(command=3, status=0xFFFFFFFF, active=1, phase=1, resume=0,
                     ticks=1, logic_steps=1, presentation_steps=1, pause_ticks=0)
        machine = dict(mode=1, step=0, actor=0, sequence=0, action=0, result=0,
                       sides=[dict(flags=1, port=0, hp=24, menu=0, choice=0)])
        with tempfile.TemporaryDirectory() as temporary, \
                patch.object(story_probe, "read_probe", return_value=probe), \
                patch.object(story_probe, "read_native_machine", return_value=machine):
            with self.assertRaisesRegex(HarnessError, "Timed out"):
                finish_synthetic_command(emu, Path(temporary))
            self.assertEqual(emu.frame, 6000)
            history = (Path(temporary) / "ko_synthetic-machine-history.jsonl").read_text()
            self.assertIn('"machine":', history)
            self.assertIn('"input": "fight"', history)
        emu.release.assert_called_with("N64_A")
        emu.stick.assert_called_with(right=True)
        emu.write.assert_not_called()
        emu.write_u32.assert_not_called()

    def test_synthetic_driver_waits_for_resume_and_releases_at_queue(self):
        from tools.rltest import story_probe
        emu = Mock(frame=0)
        emu.step.side_effect = lambda: setattr(emu, "frame", emu.frame + 1)
        probe = dict(command=3, status=0xFFFFFFFF, active=1, phase=1, resume=0,
                     ticks=1, logic_steps=1, presentation_steps=1, pause_ticks=0)
        machine = dict(mode=1, step=0, actor=0, sequence=0, action=0, result=0,
                       sides=[dict(flags=1, port=0, hp=24, menu=0, choice=0)])
        pending = {**probe, "phase": 2, "resume": 1}
        queued = {**machine, "sides": [dict(flags=1, port=0, hp=24, menu=3, choice=1)]}
        with tempfile.TemporaryDirectory() as temporary, \
                patch.object(story_probe, "read_probe",
                             side_effect=[pending, probe, probe, {"command": 0}]), \
                patch.object(story_probe, "read_native_machine",
                             side_effect=[machine, machine, queued]), \
                patch.object(story_probe, "finish_command", return_value={"command": 0}):
            self.assertEqual(finish_synthetic_command(emu, Path(temporary)), {"command": 0})
            emu.hold.assert_called_once_with("N64_A")
            calls = [call[0] for call in emu.mock_calls]
            self.assertEqual(calls[:5], ["step", "hold", "step", "release", "step"])
        emu.write.assert_not_called()
        emu.write_u32.assert_not_called()

    def test_ko_profile_rejects_bridge_before_frontend(self):
        from tools.rltest import story_probe
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "probe.map"
            path.write_text("0x80001000 gScProbeKoProfile\n")
            for phase in ("bridge_init", "bridge_carry"):
                with patch.object(story_probe, "Frontend") as frontend:
                    with self.assertRaisesRegex(HarnessError, "STORY_KO_PROBE"):
                        story_probe.phase(Mock(map=path, phase=phase, out=temporary))
                    frontend.assert_not_called()
            require_probe_profile(path, "ko_synthetic")
            path.write_text("0x80001000 gScProbeCompiledSize\n")
            require_probe_profile(path, "bridge_init")

    def test_learning_storage_rejects_pause_and_hold_writes(self):
        prebattle = bytes(1536)
        changed = b"\1" + prebattle[1:]
        require_learning_storage(prebattle, prebattle, prebattle)
        with self.assertRaisesRegex(HarnessError, "Learning pause changed"):
            require_learning_storage(prebattle, changed, changed)
        with self.assertRaisesRegex(HarnessError, "Learning hold changed"):
            require_learning_storage(prebattle, prebattle, changed)

    def test_ko_progression_and_terminal_carry_rejections(self):
        before = dict(species=1, level=6, exp=235, dvs=0x8889, hp=20, max_hp=22,
                      status=0, fainted=0, moves=[33, 45, 0, 0], pp=[34, 40, 0, 0],
                      max_pp=[35, 40, 0, 0], stat_exp=[0] * 5,
                      current_stats=[22, 11, 11, 11, 13])
        after = {**before, "level": 7, "exp": 304, "hp": 22, "max_hp": 24,
                 "stat_exp": [39, 52, 43, 65, 50], "current_stats": [24, 13, 13, 12, 15]}
        resumed = {**after, "moves": [33, 45, 0, 73], "pp": [34, 40, 0, 10],
                   "max_pp": [35, 40, 0, 10]}
        evidence = dict(error=0, ko_captures=1, injected=1, award=69, learn_move=73,
                        resumes=1, terminal=1, ko_stages=[8, 7, 7, 7, 7, 7],
                        resume_stages=[8, 7, 7, 7, 7, 7], before_ko=before,
                        after_ko=after, after_resume=resumed,
                        terminal_native=copy.deepcopy(resumed), terminal_resident=resumed)
        require_ko_progress(evidence, True, terminal=True)
        native = dict(carry_party={}, carry_live={})
        for kind, view in native.items():
            view.update({key: resumed[key] for key in
                         ("species", "level", "exp", "dvs", "hp", "max_hp", "status")})
            view.update({f"stats{i}": stat for i, stat in enumerate(resumed["current_stats"])})
            view.update({f"moves{i}": move for i, move in enumerate(resumed["moves"])})
            view.update({f"pp{i}": pp for i, pp in enumerate(resumed["pp"])})
            if kind == "carry_live":
                view["stats1"] = 19
                view.update({f"base{i}": stat for i, stat in enumerate([13, 13, 12, 15])})
                view.update({f"stage{i}": stage for i, stage in enumerate(evidence["resume_stages"])})
        require_resume_native(evidence, native)
        wrong_native = copy.deepcopy(native)
        wrong_native["carry_live"]["pp3"] = 0
        with self.assertRaisesRegex(HarnessError, "live moves"):
            require_resume_native(evidence, wrong_native)
        for key, value in (("injected", 0), ("ko_captures", 2), ("resumes", 2), ("award", 68)):
            wrong = copy.deepcopy(evidence)
            wrong[key] = value
            with self.assertRaises(HarnessError):
                require_ko_progress(wrong, True, terminal=True)
        for field in ("pp", "stat_exp", "current_stats"):
            wrong = copy.deepcopy(evidence)
            wrong["terminal_native"][field][0] += 1
            with self.assertRaisesRegex(HarnessError, "Terminal native"):
                require_ko_progress(wrong, True, terminal=True)
        evidence["injected"] = 0
        require_ko_progress(evidence, False, terminal=True)
        emu = Mock()
        offset = 19 * 4
        size = offset + 5 * ctypes.sizeof(Mon)
        emu.read_u32.side_effect = [size, offset]
        emu.read.return_value = bytes(size)
        self.assertEqual(read_progress(emu)["after_ko"]["exp"], 0)
        emu.read_u32.side_effect = [size - 4]
        with self.assertRaisesRegex(HarnessError, "progression ABI mismatch"):
            read_progress(emu)

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
