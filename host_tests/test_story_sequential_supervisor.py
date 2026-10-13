import json
import io
import os
import pathlib
import subprocess
import sys
import tempfile
import unittest

from tools.rltest.frontend import HarnessError
from tools.rltest.story_sequential_supervisor import (
    RunBudget, counted_step, bind_frontend_budget, CONTROL_CAPS, scenario_caps, supervise)

ROOT = pathlib.Path(__file__).resolve().parents[1]


class ControlPartitionTests(unittest.TestCase):
    def test_separate_scenario_cannot_reset_cap_for_fresh_reload(self):
        for name in ("win", "carry-reload", "loss-terminal-reload", "transform"):
            budget = RunBudget(0, 480, caps=scenario_caps(name))
            budget.reserve(name, 17999, 1)
            budget.reserve(name, 1, 2)
            with self.assertRaises(HarnessError):
                budget.reserve(name, 1, 3)
            with self.assertRaises(HarnessError):
                budget.reserve("control", 1, 3)
            with self.assertRaises(HarnessError):
                RunBudget(0, 480, caps=scenario_caps(name)).reserve(name, 1, 480)
        budget = RunBudget(0, 480, caps=scenario_caps("win", True))
        budget.reserve("control", 1500, 1)
        budget.reserve("win", 16500, 2)
        self.assertEqual(budget.frames, 18000)
        with self.assertRaises(HarnessError):
            supervise([], None, seconds=481, caps=scenario_caps("win"))
        with self.assertRaises(HarnessError):
            supervise([], None, seconds=480)

    def test_control_is_nontransferable_within_same_aggregate(self):
        budget = RunBudget(0, 180, caps=CONTROL_CAPS)
        budget.reserve("control", 1500, 1)
        with self.assertRaises(HarnessError):
            budget.reserve("control", 1, 2)
        budget.reserve("win", 5500, 3)
        with self.assertRaises(HarnessError):
            budget.reserve("win", 1, 4)
        budget.reserve("carry-reload", 2000, 5)
        budget.reserve("loss-terminal-reload", 5000, 6)
        budget.reserve("transform", 4000, 7)
        self.assertEqual(budget.frames, 18000)

SUPERVISE = """
import sys
from tools.rltest.story_sequential_supervisor import supervise
from tools.rltest.frontend import HarnessError
try:
    budget = supervise([sys.executable, '-c', sys.argv[1]], sys.stderr, seconds=float(sys.argv[2]))
    print(budget.frames)
except HarnessError as error:
    print(str(error), file=sys.stderr)
    sys.exit(2)
"""


class SequentialSupervisorTests(unittest.TestCase):
    def test_fresh_frontend_boot_press_wait_delegate_to_shared_stream(self):
        class Frontend:
            frame = 0
            def step(self, frames=1):
                self.frame += frames
            def press(self):
                self.step(1)
                self.step(1)
        output = io.StringIO()
        acknowledgement = io.StringIO("OK\n" * 4)
        scenario = ["win"]
        emu = Frontend()
        bind_frontend_budget(emu, acknowledgement, output, lambda: scenario[0])
        emu.step(3)  # Boot.
        emu.press()
        scenario[0] = "carry-reload"
        fresh = Frontend()
        bind_frontend_budget(fresh, acknowledgement, output, lambda: scenario[0])
        fresh.step(2)  # Fresh boot has the same stream/aggregate.
        events = [json.loads(line) for line in output.getvalue().splitlines()]
        self.assertEqual(sum(event["frames"] for event in events), 7)
        self.assertEqual(events[-1], {"scenario": "carry-reload", "frames": 2})
        with self.assertRaises(HarnessError):
            bind_frontend_budget(emu, acknowledgement, output, lambda: scenario[0])

    def test_frontend_step_not_called_until_reserved_including_boot(self):
        class Frontend:
            def step(self, frames):
                calls.append(frames)
        calls = []
        output = io.StringIO()
        counted_step(io.StringIO("OK\n"), output, "win", Frontend(), 2)
        self.assertEqual(calls, [2])
        self.assertEqual(json.loads(output.getvalue()), {"scenario": "win", "frames": 2})
        with self.assertRaises(HarnessError):
            counted_step(io.StringIO(""), output, "win", Frontend(), 5)
        self.assertEqual(calls, [2])

    def invoke(self, worker, seconds=1):
        return subprocess.run([sys.executable, "-c", SUPERVISE, worker, str(seconds)],
                              cwd=ROOT, capture_output=True, text=True, timeout=5)

    def test_budget_continuous_across_reloads_and_scenario_caps(self):
        budget = RunBudget(10, 190)
        budget.reserve("win", 7000, 11)
        budget.reserve("carry-reload", 2000, 12)
        budget.reserve("loss-terminal-reload", 5000, 13)
        budget.reserve("transform", 4000, 14)
        self.assertEqual(budget.frames, 18000)
        for scenario in ("transform", "win"):
            with self.assertRaises(HarnessError):
                budget.reserve(scenario, 1, 15)
        with self.assertRaises(HarnessError):
            RunBudget(10, 190).reserve("win", 7001, 11)
        with self.assertRaises(HarnessError):
            RunBudget(10, 190).reserve("win", 1, 190)

    def test_worker_waits_before_step_shared_budget_survives_fresh_load(self):
        worker = """
import json, sys
for scenario, count in [('win', 3), ('carry-reload', 2), ('loss-terminal-reload', 4)]:
    print(json.dumps(dict(scenario=scenario, frames=count)), flush=True)
    assert sys.stdin.readline() == 'OK\\n'
print('{"done":true}', flush=True)
assert sys.stdin.readline() == 'OK\\n'
"""
        result = self.invoke(worker)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), "9")

    def test_separate_carry_completes_without_unrelated_scenarios(self):
        code = """
import sys
from tools.rltest.story_sequential_supervisor import supervise, scenario_caps
worker = r'''
import sys
print('{"scenario":"carry-reload","frames":2}', flush=True)
assert sys.stdin.readline() == 'OK\\n'
print('{"done":true}', flush=True)
assert sys.stdin.readline() == 'OK\\n'
'''
budget = supervise([sys.executable, '-c', worker], sys.stderr, seconds=1,
                   caps=scenario_caps('carry-reload'))
print(budget.frames)
"""
        result = subprocess.run([sys.executable, "-c", code], cwd=ROOT,
                                capture_output=True, text=True, timeout=5)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), "2")

    def test_boot_watchdog_before_any_frame_and_exact_descendant_reap(self):
        with tempfile.TemporaryDirectory() as directory:
            pidfile = pathlib.Path(directory) / "pid"
            worker = f"""
import subprocess, sys, time
child = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(30)'])
open({str(pidfile)!r}, 'w').write(str(child.pid))
time.sleep(30)
"""
            result = self.invoke(worker, .3)
            self.assertEqual(result.returncode, 2, result.stderr)
            self.assertIn("watchdog", result.stderr)
            child = int(pidfile.read_text())
            self.assertFalse(pathlib.Path(f"/proc/{child}").exists(), "Descendant was not reaped")

    def test_refusal_and_normal_exit_also_kill_orphaned_descendant(self):
        for tail in ('print(\'{"scenario":"win","frames":7001}\', flush=True)',
                     "raise SystemExit(0)"):
            with tempfile.TemporaryDirectory() as directory:
                pidfile = pathlib.Path(directory) / "pid"
                worker = f"""
import subprocess, sys
child = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(30)'],
                         stdout=subprocess.DEVNULL)
open({str(pidfile)!r}, 'w').write(str(child.pid))
{tail}
"""
                result = self.invoke(worker)
                self.assertEqual(result.returncode, 2, result.stderr)
                self.assertFalse(pathlib.Path(f"/proc/{int(pidfile.read_text())}").exists())

    def test_detached_descendant_is_also_killed_and_reaped(self):
        with tempfile.TemporaryDirectory() as directory:
            pidfile = pathlib.Path(directory) / "pid"
            worker = f"""
import subprocess, sys, time
child = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(30)'],
                         start_new_session=True)
open({str(pidfile)!r}, 'w').write(str(child.pid))
time.sleep(30)
"""
            result = self.invoke(worker, .3)
            self.assertEqual(result.returncode, 2, result.stderr)
            self.assertIn("watchdog", result.stderr)
            self.assertFalse(pathlib.Path(f"/proc/{int(pidfile.read_text())}").exists())

    def test_missing_done_protocol_is_failure(self):
        result = self.invoke("print('junk', flush=True)")
        self.assertEqual(result.returncode, 2)
        self.assertIn("Malformed", result.stderr)


if __name__ == "__main__":
    unittest.main()
