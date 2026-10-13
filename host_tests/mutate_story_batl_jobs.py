#!/usr/bin/env python3
"""Mutation run for the story battle job/publication admission.

Each mutant is one exact, single-occurrence source replacement applied to a private copy of src/ and
host_tests/ (the checkout is never modified). A mutant is KILLED when run_story_batl_jobs.sh or
run_story_batl_scope.sh fails (CHECK abort, sanitizer report, unexpected stop) or times out (a
hang). Usage: host_tests/mutate_story_batl_jobs.py <fresh-output-dir> [mutant-name ...]
"""
import os
import signal
import shutil
import subprocess
import sys
import time
from pathlib import Path

JOBS = "src/story/sc_batl_jobs.c"
SCOPE = "src/story/sc_batl_scope.c"
LOADER = "src/19840.c"
GEO = "src/geo_layout.c"

# name, file, exact old text, replacement, expectation note
MUTANTS = [
    ("result0-always-cached", JOBS,
     "if (v->token != 0 && v->token == v->live && v->live_model == v->request && v->live_tint == v->job_tint) {",
     "if (1) {", ""),
    ("cache-model-check", JOBS, " && v->live_model == v->request", "", ""),
    ("cache-token-live", JOBS, "v->token != 0 && v->token == v->live &&", "v->token != 0 &&", ""),
    ("cache-tint-check", JOBS, " && v->live_tint == v->job_tint", "", ""),
    ("load-buffer-bound", JOBS, "v->buffer == 0 || v->buffer != v->expected ||", "v->buffer == 0 ||", ""),
    ("root-owner", JOBS, "SC_JOB_ROOT_BYTES) && owner == entry);", "SC_JOB_ROOT_BYTES));", ""),
    ("roots-extent", JOBS, "sc_job_in(buffer, SC_JOB_MODEL_BYTES, roots, nroots * 4u)", "1", ""),
    ("typeE-extent-before-owner", SCOPE,
     "            (root[0] == 0xE && !sc_job_in(v.buffer, SC_JOB_MODEL_BYTES, (u32)root, SC_JOB_ROOT_BYTES)) ||\n",
     "", "reintroduces the b0df9e8 read-before-extent order"),
    ("model-ticket", SCOPE, "    if (loader.live &&\n        !sc_job_ticket_take(", "    if (0 &&\n        !sc_job_ticket_take(",
     "jobs cases 31/32"),
    ("ticket-index", JOBS, " && t->index == index", "", "jobs case 31"),
    ("ticket-header", JOBS, " && t->header == header", "", "jobs case 32"),
    ("fx-payload-unchecked", JOBS, "        if (!sc_job_in(buffer, SC_JOB_FX_BYTES, payload, 4)) {",
     "        if (0) {", "jobs cases 35-37"),
    ("fx-payload-zero-bytes", JOBS, "sc_job_in(buffer, SC_JOB_FX_BYTES, payload, 4)",
     "sc_job_in(buffer, SC_JOB_FX_BYTES, payload, 0)", "jobs case 35"),
    ("ticket-second-load", JOBS, "    if (t->state != 0) {\n        t->state = 2;\n        return;\n    }\n", "", "jobs case 33"),
    ("model-admit-bypass", SCOPE, "if (job->unk_00 == 1) return model_done(worker, job);",
     "if (job->unk_00 == 1) return 1;", ""),
    ("size0", JOBS, "    if (v->size == 0) {\n        return SC_JOB_FAILED;\n    }\n", "",
     "connected-path equivalent only: sc_batl_job_admit's sole caller is func_80019B48, whose ROGUELIKE "
     "size-0 prologue (STORY_SERVICE_CANDIDATE requires ROGUELIKE via the Makefile chain) stops first; "
     "the PURE function alone is not equivalent"),
    ("results-terminator", JOBS, "    return results[n] == 0;\n}", "    return 1;\n}", ""),
    ("partial-results", JOBS, "        if (!sc_job_fx_list_ok(buffer, results[i])) {",
     "        if (results[i] != 0 && !sc_job_fx_list_ok(buffer, results[i])) {", ""),
    ("fx-list-next-extent", JOBS,
     "        at += 8;\n        if (!sc_job_in(buffer, SC_JOB_FX_BYTES, at, 8)) {\n            return 0;\n        }\n",
     "        at += 8;\n", ""),
    ("fx-slot-range", JOBS, "        if (id < 0 || id >= SC_FX_SLOTS) {\n            return 0;\n        }\n        at += 8;",
     "        at += 8;", ""),
    ("fx-zero-slot-spin", JOBS, "            return *(const signed char*)at == 0;", "            return 1;", ""),
    ("fx-generation", JOBS, "t->generation[id] == t->sent[owner] && ", "", ""),
    ("fx-completed", JOBS, " && t->completed[owner] == t->sent[owner]", "",
     "equivalent over the ScFxTable mutators: sc_fx_note records generation=sent[owner] only inside a "
     "publication that sc_fx_publish_begin opened with completed==sent; afterwards sent only increments "
     "(sc_fx_send, which refuses at 0xFFFFFFFF), breaking generation==sent, and completed only becomes "
     "sent (sc_fx_complete refuses when already equal), so generation==sent implies completed==sent"),
    ("fx-kind", JOBS, "if (t->kind[id] != (graph ? SC_FX_GRAPH : SC_FX_DATA)) {", "if (0) {", ""),
    ("fx-send-other-pending", JOBS, "if (w < 0 || w > 1 || other_pending ||", "if (w < 0 || w > 1 ||",
     "jobs case 30: both sides arm before either sends"),
    ("fx-complete-bookkeeping", SCOPE, "return !loader.live || sc_fx_complete(&fx, w);", "return 1;", ""),
    ("publish-without-complete", SCOPE, "return sc_fx_publish_begin(&fx, fx_pending());", "return 1;", ""),
    ("shared-list-overlap", SCOPE, "int sc_batl_fx_request(void) { return !loader.live || !fx_pending(); }",
     "int sc_batl_fx_request(void) { return 1; }", ""),
    ("refusal-notify", LOADER,
     "    if (job->queue != NULL) {\n        osSendMesg(job->queue, job, OS_MESG_NOBLOCK);\n    }\n", "", ""),
    ("refusal-image-clear", LOADER, "    sRlLoaderJob = NULL;\n    rl_geo_image_set(NULL);\n",
     "    sRlLoaderJob = NULL;\n", "jobs case 29 asserts the image scope is inactive after refusal"),
    ("refuse-without-notify", GEO, "    rl_loader_refuse_notify();\n    osStopThread(NULL);",
     "    osStopThread(NULL);", "loader stops without completing the job: caller would wait forever"),
    ("recolor-admit-bypass", GEO, "    if (rl_geo_image_active() &&\n        !rl_geo_recolor_ok(",
     "    if (0 &&\n        !rl_geo_recolor_ok(", ""),
]

SUITES = [("jobs", "host_tests/run_story_batl_jobs.sh"), ("scope", "host_tests/run_story_batl_scope.sh")]
LINKED = ["include", "lib", "build", "assets", ".local", "tools", "Makefile"]


def tree(root, out, name):
    t = out / "trees" / name
    t.mkdir(parents=True)
    shutil.copytree(root / "src", t / "src")
    shutil.copytree(root / "host_tests", t / "host_tests")
    for n in LINKED:
        if (root / n).exists():
            os.symlink(root / n, t / n)
    return t


TIMEOUT = int(os.environ.get("MUTANT_TIMEOUT", "300"))


def group_alive(pgid):
    try:
        os.killpg(pgid, 0)
        return True
    except ProcessLookupError:
        return False


def reap_group(p):
    """SIGKILL the runner's own process group (pgid == bash pid), reap the leader and wait until the
    group is empty (an unreaped zombie leader would still count as a member)."""
    pgid = p.pid
    if group_alive(pgid):
        os.killpg(pgid, signal.SIGKILL)
    p.wait()
    for _ in range(500):
        if not group_alive(pgid):
            return
        time.sleep(0.01)
    raise SystemExit("process group %d survived SIGKILL" % pgid)


def run(t, out, name):
    for suite, script in SUITES:
        log = out / ("%s-%s.log" % (name, suite))
        with open(log, "w") as f:
            # own session/process group so a hang is reaped with every descendant (test binary, tee)
            p = subprocess.Popen(["bash", script, str(out / "runs" / name / suite)], cwd=t, stdout=f,
                                 stderr=subprocess.STDOUT, start_new_session=True)
            try:
                rc = p.wait(timeout=TIMEOUT)
                hung = False
            except subprocess.TimeoutExpired:
                hung = True
            reap_group(p)
            if hung:
                return "KILLED", "%s timeout (hang)" % suite
        if rc != 0:
            text = log.read_text().splitlines()
            keys = ("CHECK FAILED", "runtime error", "ERROR:", "assertion failed", "error:")
            hits = [l.strip() for l in text if any(k in l for k in keys)]
            return "KILLED", "%s: %s" % (suite, hits[0][:160] if hits else "rc %d" % rc)
    return "SURVIVED", ""


def main():
    root = Path(__file__).resolve().parent.parent
    out = Path(sys.argv[1]).resolve()
    out.mkdir(parents=True)
    wanted = set(sys.argv[2:])
    rows = []
    status, why = run(tree(root, out, "baseline"), out, "baseline")
    rows.append(("baseline", status == "SURVIVED" and "PASS" or "FAIL", why, ""))
    if status != "SURVIVED":
        print("baseline failed: " + why)
        return 1
    for name, path, old, new, note in MUTANTS:
        if wanted and name not in wanted:
            continue
        t = tree(root, out, name)
        p = t / path
        s = p.read_text()
        if s.count(old) != 1:
            print("mutant %s: pattern occurs %d times in %s" % (name, s.count(old), path))
            return 1
        p.write_text(s.replace(old, new))
        status, why = run(t, out, name)
        rows.append((name, status, why, note))
        print("%-28s %-8s %s %s" % (name, status, why, note), flush=True)
    with open(out / "report.txt", "w") as f:
        for name, path, old, new, note in MUTANTS:
            if not wanted or name in wanted:
                f.write("MUTANT %s in %s\n--- old\n%s\n+++ new\n%s\n\n" % (name, path, old, new))
        f.write("RESULTS\n")
        for r in rows:
            f.write("%-28s %-8s %s %s\n" % r)
    killed = sum(1 for r in rows[1:] if r[1] == "KILLED")
    print("%d/%d killed" % (killed, len(rows) - 1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
