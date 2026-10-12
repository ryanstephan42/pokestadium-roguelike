"""Launch only a separately approved, exclusive sequential reservation.

This command is not authorization. --help performs no native operation.
"""

import argparse
import json
import pathlib
import sys

from tools.rltest.frontend import HarnessError
from tools.rltest.story_sequential_supervisor import CAPS, supervise
from tools.rltest.story_sequential_worker import verify_candidate, write_json, digest


def collect(directory, budget, elapsed, reaped):
    scenarios = {}
    for name in CAPS:
        path = directory / name / "report.json"
        if path.is_file():
            row = json.loads(path.read_text())
        else:
            attempted = (directory / (name + ".attempted.json")).is_file()
            row = dict(status="fail" if attempted else "untested",
                       last_verified_boundary="no scenario observables; see stderr")
        if name == "loss-terminal-reload" and row["status"] == "awaiting-terminal-reload":
            cold = path.parent / "terminal-reload" / "report.json"
            terminal = json.loads(cold.read_text()) if cold.is_file() else dict(status="untested")
            row["terminal_reload"] = terminal
            row["status"] = "pass" if terminal["status"] == "pass" else "fail"
        scenarios[name] = row
    hashes = {}
    if directory.is_dir():
        import hashlib
        hashes = {str(path.relative_to(directory)): hashlib.sha256(path.read_bytes()).hexdigest()
                  for path in directory.rglob("*") if path.is_file()}
    observed = 0
    exact_frames = True
    for row in scenarios.values():
        if row["status"] != "untested":
            if "local_frames" not in row:
                exact_frames = False
            observed += row.get("local_frames", 0)
        if "terminal_reload" in row:
            cold = row["terminal_reload"]
            if "local_frames" not in cold:
                exact_frames = False
            observed += cold.get("local_frames", 0)
    return dict(scope="native scenario report; not gate or overall heap acceptance",
                scenarios=scenarios, frames_reserved=budget.frames,
                frames_observed=observed if exact_frames else None,
                scenario_frames=budget.per_scenario, seconds=elapsed,
                started_monotonic=budget.started, deadline_monotonic=budget.deadline,
                exact_tree_reaped=reaped, no_orphans=reaped,
                actual_carry_sha256=scenarios["win"].get("actual_carry_sha256"),
                artifact_sha256=hashes)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("seal", "package", "binding", "core", "out"):
        parser.add_argument("--" + name, type=pathlib.Path, required=True)
    parser.add_argument("--exclusive-approved-reservation", action="store_true", required=True)
    parser.add_argument("--seal-sha256", required=True)
    parser.add_argument("--core-sha256", required=True)
    args = parser.parse_args()
    verify_candidate(args.seal, args.package, args.binding, args.seal_sha256)
    if digest(args.core.read_bytes()) != args.core_sha256:
        raise HarnessError("Core differs from approved external pin")
    if args.out.exists() or args.out.with_suffix(".report.json").exists():
        raise HarnessError("Reservation outputs already exist; retry refused")
    args.out.parent.mkdir(parents=True, exist_ok=True)
    command = [sys.executable, "-m", "tools.rltest.story_sequential_worker"]
    for name in ("seal", "package", "binding", "core", "out"):
        command += ["--" + name, str(getattr(args, name).resolve())]
    command += ["--seal-sha256", args.seal_sha256]
    command += ["--core-sha256", args.core_sha256]

    def report(budget, elapsed, reaped):
        write_json(args.out.with_suffix(".report.json"), collect(args.out, budget, elapsed, reaped))

    with args.out.with_suffix(".stderr.log").open("x") as log:
        supervise(command, log, on_exit=report)


if __name__ == "__main__":
    main()
