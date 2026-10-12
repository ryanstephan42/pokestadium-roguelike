#!/usr/bin/env bash
# Bounded hang fixture for mutate_story_batl_jobs.py: the fx-zero-slot-spin mutant hangs forever; with a
# short timeout the runner must report it KILLED and leave no process referencing the output directory.
set -euo pipefail
out=$(realpath -m "${1:?usage: $0 <fresh-dir>}")
[ ! -e "$out" ] || { echo "$out exists"; exit 1; }
MUTANT_TIMEOUT=${MUTANT_TIMEOUT:-20} python3 "$(dirname "$0")/mutate_story_batl_jobs.py" "$out" fx-zero-slot-spin
grep -q '^fx-zero-slot-spin *KILLED *jobs timeout (hang)' "$out/report.txt"
left=0
for d in /proc/[0-9]*; do
    if tr '\0' ' ' < "$d/cmdline" 2>/dev/null | grep -qF "$out/"; then
        echo "survivor ${d#/proc/}: $(tr '\0' ' ' < "$d/cmdline")"; left=1
    fi
done
[ "$left" = 0 ] && echo "REAP FIXTURE PASS"
