"""One-slot Linux supervisor for a later authorized sequential worker.

Workers request {"scenario": name, "frames": n} on stdout BEFORE boot/run
frames and wait for an "OK" line on stdin. Every reload uses the same worker,
clock and counters. Progress messages are capped and watchdog-polled; other
diagnostics belong on stderr. This is not an emulator execution grant.
"""

import ctypes
import json
import os
import pathlib
import select
import signal
import subprocess
import time
from dataclasses import dataclass, field

from tools.rltest.frontend import HarnessError

CAPS = {"win": 7000, "carry-reload": 2000, "loss-terminal-reload": 5000,
        "transform": 4000}
CONTROL_CAPS = {"control": 1500, "win": 5500, "carry-reload": 2000,
                "loss-terminal-reload": 5000, "transform": 4000}

def scenario_caps(scenario, control=False):
    if scenario not in CAPS:
        raise HarnessError("Unknown separate scenario")
    return ({"control": 1500, scenario: 16500} if control else {scenario: 18000})


@dataclass
class RunBudget:
    started: float
    deadline: float
    frames: int = 0
    per_scenario: dict = field(default_factory=lambda: {name: 0 for name in CAPS})
    scenario_index: int = -1
    caps: dict = field(default_factory=lambda: CAPS.copy())

    def __post_init__(self):
        if self.caps == CONTROL_CAPS or any(
                self.caps == scenario_caps(name, control)
                for name in CAPS for control in (False, True)):
            self.per_scenario = {name: 0 for name in self.caps}
        elif self.caps != CAPS:
            raise HarnessError("Unreviewed sequential budget partition")

    def check(self, now):
        if now >= self.deadline:
            raise HarnessError("Sequential aggregate watchdog exhausted")

    def reserve(self, scenario, frames, now):
        self.check(now)
        if type(scenario) is not str or scenario not in self.caps or type(frames) is not int or not 1 <= frames <= 18000:
            raise HarnessError("Invalid pre-step frame request")
        index = list(self.caps).index(scenario)
        if index < self.scenario_index or index > self.scenario_index + 1:
            raise HarnessError("Sequential scenario reordered/skipped")
        if self.frames + frames > 18000 or self.per_scenario[scenario] + frames > self.caps[scenario]:
            raise HarnessError("Sequential aggregate/scenario frame cap exhausted")
        self.frames += frames
        self.per_scenario[scenario] += frames
        self.scenario_index = index


def enable_subreaper():
    if not hasattr(os, "pidfd_open") or not hasattr(signal, "pidfd_send_signal"):
        raise HarnessError("Sequential supervisor requires Linux pidfd support")
    if adopted_children():
        raise HarnessError("Run supervisor in a dedicated process with no existing children")
    libc = ctypes.CDLL(None, use_errno=True)
    if libc.prctl(36, 1, 0, 0, 0) != 0:
        raise HarnessError(f"Cannot establish descendant reaping: errno {ctypes.get_errno()}")


def adopted_children():
    children = set()
    for task in pathlib.Path(f"/proc/{os.getpid()}/task").iterdir():
        try:
            children.update(int(pid) for pid in (task / "children").read_text().split())
        except FileNotFoundError:
            continue  # A sibling supervisor thread ended during enumeration.
    return children


def kill_child(pid):
    try:
        descriptor = os.pidfd_open(pid)
    except ProcessLookupError:
        return
    try:
        signal.pidfd_send_signal(descriptor, signal.SIGKILL)
    except ProcessLookupError:
        pass
    finally:
        os.close(descriptor)


def stop_tree(process):
    """Kill the owned group, then pidfd-kill/reap every subreaper-adopted child.

    Detached/double-forked children are adopted too, not left as escaped
    process-group orphans. The dedicated supervisor began with no children.
    """
    try:
        os.killpg(process.pid, signal.SIGKILL)
    except ProcessLookupError:
        pass
    try:
        process.wait(timeout=2)
    except subprocess.TimeoutExpired as error:
        raise HarnessError("Owned sequential leader did not reap") from error
    deadline = time.monotonic() + 2
    while True:
        for pid in adopted_children():
            kill_child(pid)
        try:
            pid, _ = os.waitpid(-1, os.WNOHANG)
        except ChildProcessError:
            return
        if pid:
            continue
        if time.monotonic() >= deadline:
            raise HarnessError("Owned sequential process tree did not reap")
        time.sleep(.005)


def supervise(command, log, seconds=180, clock=time.monotonic, on_exit=None, caps=None):
    separate = caps is not None and any(
        caps == scenario_caps(name, control) for name in CAPS for control in (False, True))
    if not 0 < seconds <= (480 if separate else 180):
        raise HarnessError("Watchdog exceeds approved scenario procedure")
    enable_subreaper()
    started = clock()  # Before Popen, worker/core load, boot or fresh reload.
    budget = RunBudget(started, started + seconds, caps=CAPS.copy() if caps is None else caps)
    process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                               stderr=log, start_new_session=True, bufsize=0)
    pending = b""
    completed = False
    try:
        while True:
            now = clock()
            budget.check(now)
            ready, _, _ = select.select([process.stdout], [], [], min(.05, budget.deadline - now))
            if not ready:
                continue
            data = os.read(process.stdout.fileno(), 4096)
            if not data:
                if pending:
                    raise HarnessError("Truncated sequential frame protocol")
                try:
                    result = process.wait(timeout=max(.001, budget.deadline - clock()))
                except subprocess.TimeoutExpired as error:
                    raise HarnessError("Sequential aggregate watchdog exhausted at worker exit") from error
                if result or not completed:
                    raise HarnessError(f"Sequential worker failed/unacknowledged completion: {result}")
                budget.check(clock())
                return budget
            pending += data
            if len(pending) > 4096:
                raise HarnessError("Oversized sequential frame protocol")
            while b"\n" in pending:
                line, pending = pending.split(b"\n", 1)
                try:
                    message = json.loads(line)
                except (ValueError, UnicodeDecodeError) as error:
                    raise HarnessError("Malformed sequential frame protocol") from error
                if completed or not isinstance(message, dict):
                    raise HarnessError("Unexpected sequential frame protocol")
                if set(message) == {"done"} and message["done"] is True:
                    required = (len(budget.caps) - 1 if separate else
                                list(budget.caps).index("loss-terminal-reload"))
                    if budget.scenario_index < required:
                        raise HarnessError("Missing required win/reload/loss scenarios")
                    completed = True
                    budget.check(clock())
                elif set(message) == {"scenario", "frames"}:
                    budget.reserve(message["scenario"], message["frames"], clock())
                else:
                    raise HarnessError("Unknown sequential frame protocol")
                try:
                    process.stdin.write(b"OK\n")
                    process.stdin.flush()
                except BrokenPipeError as error:
                    raise HarnessError("Sequential worker exited before frame acknowledgement") from error
    finally:
        reaped = False
        try:
            stop_tree(process)
            reaped = not adopted_children()
        finally:
            if on_exit is not None:
                on_exit(budget, clock() - started, reaped)


def reserve_frames(stream_in, stream_out, scenario, frames):
    stream_out.write(json.dumps(dict(scenario=scenario, frames=frames)) + "\n")
    stream_out.flush()
    if stream_in.readline() != "OK\n":
        raise HarnessError("Supervisor refused/closed frame lease")


def counted_step(stream_in, stream_out, scenario, emu, frames):
    """A standalone step seam used by HOST fixtures and explicit worker steps."""
    reserve_frames(stream_in, stream_out, scenario, frames)
    emu.step(frames)


def bind_frontend_budget(emu, stream_in, stream_out, scenario):
    """Bind each fresh frontend before its first frame, including boot.

    press/wait/step all delegate to this budgeted instance step. scenario is a
    callable returning the shared worker's current scenario. Creating another
    frontend never creates another supervisor or resets a counter.
    """
    if emu.frame != 0 or getattr(emu, "_sc_budget_bound", False):
        raise HarnessError("Frontend budget must bind exactly once before boot")
    original_step = emu.step
    def step(frames=1):
        reserve_frames(stream_in, stream_out, scenario(), frames)
        original_step(frames)
    emu.step = step
    emu._sc_budget_bound = True
