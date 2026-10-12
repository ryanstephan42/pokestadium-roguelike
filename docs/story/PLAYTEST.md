# Isolated story playtest handoff

The user's requested playtest is available locally at
`/home/r/pokestadium-story-playtest/`. Run `~/pokestadium-story-playtest/launch.sh`.
Its `README.md` contains keyboard/controller mappings, campaign creation,
save/Continue rules, issue-report instructions and known limitations.

The launcher verifies the copied ROM/core hashes, then starts RetroArch with
private configuration, core options, save/Pak, state, history and log paths.
It does not install a test checkpoint or modify global RetroArch settings or
existing user saves. The save directory starts empty. Keep the package at its
current absolute path.

At title press Start, acknowledge any Game Pak prompt, then press N64 R on
the top-level mode menu (`R: STORY CAMPAIGN`). Select **New campaign**, set
version/healing/starter/seed and choose **Create**. Later choose **Continue
campaign**. Keyboard equivalents are Enter for Start, W for R, X for A,
Z for B, arrows for navigation and I/L/K/J for C-Up/Right/Down/Left.

Defeat ends a campaign; reset/quit after battle launch also loads terminal
on Continue, preserving prebattle progression with no retry. Wait for game
saving to finish and close RetroArch normally so its private save container
is flushed. Use the game save menu, not emulator savestates or rewind.

Build `4f5ed90`, native v49:

| Artifact | SHA-256 |
| --- | --- |
| ROM | `df0b16146bf8f56e2b01a52cde7befc5fded8c2f2a0e8c41d03f9b40751695aa` |
| Mupen64Plus-Next core | `1e5674de118b8cb3621a16402ecdd3d509591700c35e98943e8bb32906342263` |
| Native seal | `e25cc766205cbfd30c8e7e847cae60236744404330a0bf784a5ac136c5358941` |
| Proposal | `adeece472833869f0c69500281fdbf05227a3d98781371f8a31e2c6638e2b673` |

The forced-turn audit and joined HOST/sanitizer regressions cover verified
rollback, repeated forced frames and missing/tampered rollback refusal.
Vanilla `make` still prints `build/pokestadium-us.z64: OK`. The full integration
gate is explicitly deferred for this playtest handoff and remains pre-merge.
This package is not overall campaign acceptance.

Run24 naturally transformed Ditto into Onix at frame 2,365 and continued through
Bide with service failure flag zero. Viewed final snapshot shows both Onix
models, Ditto 27/33 and enemy 31/36. It used 16,088 persisted frames before the
480.014-second watchdog, with exact process-tree reap and no orphans. The
scenario is **FAIL** at the watchdog; Transform occurred but postbattle
restoration remains **UNTESTED**. Report:
`build/story/sequential-4f5ed90-standing-run24-transform.report.json`,
SHA `e9b319b24ec50bc7eb3ea72229fc72aef673939e665ef1bfc4ca4a3a44dbcb12`.

Native win and exact carry reload passed on the preceding image. Accepted
learning/evolution and natural terminal loss/reload are not yet natively
verified; the user's own play replaces further automated loss attempts.
Authentic Red/Blue teams use Stadium mechanics/presentation; move/model
animation pairing is not exhaustively verified. A PC, shops and items are
outside scope. Long animations and remaining integration issues
should be reported with version, seed, preset, trainer, party/levels, screen,
last action, expected/actual result and whether saving/Continue was involved.
Keep ROMs, saves and game screenshots out of git.
