# Separate sequential story reservations

This procedure implements the user's relayed test-procedure decision after
run13. It does not change campaign rules or grant broader access.

Run win, carry reload, loss plus terminal reload, and Transform in separate,
sequential reservations. Each uses exactly one pinned Mupen64Plus-Next core
instance at a time, up to 18,000 frames and 480 seconds measured before worker
creation through completion. Fresh loss/terminal processes share their
reservation's counter and deadline. Supervisor kill/reap is required on every
outcome; watchdog/cap exhaustion remains FAIL, not permission to resume it.
An optional 1,500-frame normal-ROM control is included only when frontend/core
setup changes, leaving 16,500 frames for that reservation's selected scenario.

The new sequential-only Squirtle checkpoint is prepared through the existing
pure campaign APIs along the same legal five-encounter route. Its learned
Bubble and all imported moves obey the packed-slots rule. Brock remains the
win target. Prior HOST victories are preparation, not native battle evidence.
The shared Charmander fixture and frozen c399 are not edited.

Install a sealed, hashed checkpoint only during scenario setup before frame
zero, equivalent to inserting a prepared private Controller Pak. A carry
reload instead requires the exact game-written save from a completed PASS
win. Pin the producer aggregate report SHA; verify same ROM/core and successful
reap, then verify its recorded hashes of the save container, committed state,
calibration and scenario report. Load those exact unmodified bytes before frame
zero. Assert producer, loaded and post-Continue container hashes are equal and
record all three. No checkpoint, edited bytes, RAM patch or save substitution
is permitted on reload. Loss/terminal reload uses the same exact persistence
model inside its own reservation.

All interaction after setup is ordinary calibrated button input. Preserve
the 8 MiB story layout, private saves, frozen c399, normal 4 MiB path and user
saves. No global frontend/core settings changes.

Reports must distinguish PASS, FAIL and UNTESTED, retain bounded incremental
boundary/witness/video evidence and artifact hashes, and include the candidate
seal/ROM/core pins, producer report pin, frames, wall time and cleanup result.
Transform without an actual observation is UNTESTED, never PASS.

```sh
python3 -m tools.rltest.run_story_sequential ... --only-scenario win
python3 -m tools.rltest.run_story_sequential ... --only-scenario carry-reload \
  --producer-run build/story/WIN-RUN --producer-report-sha256 REPORT-SHA
python3 -m tools.rltest.run_story_sequential ... --only-scenario loss-terminal-reload
python3 -m tools.rltest.run_story_sequential ... --only-scenario transform
```

Each command requires its newly sealed candidate, binding, package, core pin,
fresh output directory and explicit exclusive-reservation argument. This
document describes the standing grant; it is not itself an execution grant.

The subsequent user request opens a separate, isolated story playtest and
replaces the automated loss scenario with the user's own play. The coordinator
approved restoring unchanged native forced/status behavior only after verified
staged-choice rollback; missing or failed rollback remains fail-stop. Perform
one further bounded Transform verification before the package handoff. The
package uses its own RetroArch configuration, Controller Pak/save and state
directories, not global settings or existing user saves. Full `rl-gate.sh` is
explicitly deferred for this handoff and remains required before merging PR18.
