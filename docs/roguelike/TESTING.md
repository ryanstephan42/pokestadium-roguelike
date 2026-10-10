# Roguelike test harness

`tools/rltest` is a synchronous Python/ctypes libretro frontend for booting the
US ROM headlessly. It prefers RetroArch's user core directory and then
`/usr/lib/libretro`, with Mupen64Plus-Next before Parallel-N64. Both need a
software renderer; the frontend requests Angrylion and explicitly rejects a
core that asks for a hardware video context.

## Requirements and quick checks

The worktree must contain the local, ignored ROM and normal build map. Setup a
new worktree with `scripts/rl-worktree-setup.sh`, then run:

```sh
tools/rltest/run_smoke.sh
python3 -m unittest tools.rltest.tests.test_helpers
python3 -m tools.rltest.run battle-fixture
python3 -m tools.rltest.test_play_run
```

The smoke runner builds nothing. It steps past the intro, asserts
`gCurrentGameState == STATE_TITLE_SCREEN` (2), allows the title prompt to
settle, presses START, passes the Game Pak Check with N64 A, and asserts Menu
Select (4). On success it writes the menu PNG under `.local/rltest/`. Set
`RLTEST_CORE`, `RLTEST_ROM`, `RLTEST_MAP`,
and `RLTEST_OUTPUT` to override its defaults. The core run has a wall-clock
timeout (110 seconds by default); pass `--timeout` to adjust it.

`make rl` uses `build_rl/` for its ROM and map. Pass `--rom` and `--map` to
`tools.rltest.run` to exercise that build explicitly. The API accepts GNU ld
map files and resolves loaded overlay symbols through the runtime `gFragments`
table, so fragment 62's symbols can be read at their linked addresses without
guessing its current RDRAM address.

## Python API

```python
from tools.rltest.frontend import Frontend

with Frontend(core, rom, map_path, output=".local/rltest") as emu:
    emu.step(60)
    emu.press("N64_A", frames=3, after=2)
    emu.hold("Z", controller=2)
    emu.step(10)
    emu.release(controller=2)
    state = emu.read_u32("gCurrentGameState")
    emu.write_u32("gCurrentGameState", state)
    emu.snapshot(".local/rltest/before.rdram")
    emu.screenshot(".local/rltest/frame.png")
    emu.save(".local/rltest/test.state")
```

`press` sends a timed press and restores the buttons previously held on that
controller; `hold` and `release` provide persistent controller state. Button
names A/B/Y refer to libretro joypad IDs, not N64 face-button legends. On the
installed Mupen core, `N64_A` (RetroPad B/id 0) is the Stadium confirm and
`N64_B` (RetroPad Y/id 1) is the N64 B action.
`read`/`write` accept symbol names or RDRAM addresses and normalize the
little-endian host's word-swapped N64 RDRAM to/from canonical N64 byte order.
`read_u8/u16/u32` and `write_u16/u32` are big-endian. Set `ram_order="big"` for
a core configured to expose canonical memory bytes. Savestate metadata binds
each fixture to the exact ROM, core library, options, and RAM byte order.

`battle_mons()` returns `None` until fragment 62 has two initialized active
mons; then each side includes species, level, moves, status, live HP/max HP,
and packed PP values. `wait_battle()` waits for that condition. This supports
W2 HP/PP checks and W3's `STATE_RL_BATTLE` test dispatcher without relying only
on the top-level game state.

## RAM snapshots

Take snapshots around an input sequence and compare them:

```python
emu.snapshot(".local/rltest/before.rdram")
emu.press("A", frames=3)
emu.snapshot(".local/rltest/after.rdram")
```

```sh
python3 -m tools.rltest.ramdiff \
  .local/rltest/before.rdram .local/rltest/after.rdram
```

Snapshots are raw canonical 8 MiB RDRAM images with adjacent JSON metadata
containing the frame, map path, and loaded overlay ranges. The diff groups
adjacent changed bytes and labels the range using the nearest map symbol or
loaded overlay symbol.

## Battle scenarios and generated files

`python3 -m tools.rltest.run battle-fixture` boots through Stadium's Poke Cup,
chooses a six-rental team containing Bulbasaur, Ivysaur and Squirtle, selects
those three for battle, and waits for initialized fragment-62 mons. It asserts
the live lead is the expected level-50 Bulbasaur with moves `[73, 92, 34, 75]`,
then saves a battle state, screenshot and RDRAM snapshot under
`.local/rltest/`. It also saves `battle-now-attack-menu.state` after entering
the move-selection screen for W2's live HP/PP verifier. A real Body Slam was
verified from this freshly booted state: slot-2 PP decremented once, both
live HP values changed, party HP converged after animation, and PP-Up bits
were preserved. Damage and hit outcomes depend on the game's time-based RNG.
If navigation or the lead assertion fails, the command exits non-zero rather
than claiming fixture success. ROM-derived states, snapshots, and screenshots
are local-only and never belong in Git.

After W0/W3 add their RL dispatcher test script, run its custom-battle test
against `build_rl/pokestadium-us.z64` and `build_rl/pokestadium-us.map`. It uses
`gRlBattleTestRequest` at a dispatcher boundary and can combine the frontend's
`battle_mons()` live-state inspection with the run's counters/results. Those
symbols are intentionally not available in the vanilla map.
The exact-team scenario uses a guarded CPU-vs-CPU fixture to exercise native
result capture and carryover without driving the playable battle menu.

The JSON runner supports `step`, `press`, `hold`, `release`, `state`, `assert`,
`write32`, `snapshot`, `screenshot`, `save`, and `load` operations. Each
generated ROM-derived artifact and metadata file remains in ignored
`.local/rltest/`.

## Playable run scenario

`python3 -m tools.rltest.test_play_run` boots `build_rl/pokestadium-rl.z64` and
drives the real L+A entry and run UI. It checks draft cancellation/re-entry,
the free reroll and a deterministic seed, selects three rentals, chooses a
trainer route, rejects an empty active team, and verifies B returns to the map
without advancing it. Guarded test fixtures boost the party for the win path
and start its lead at full HP in the first battle; all run-battle decisions
remain human-controlled. The scenario requires actual native HP damage, then
checks that the damaged lead's exact HP, moves, and PP are passed into the next
battle. It also covers reward selection, three chained wins, summary, exit and
roguelike re-entry. A separate UI-only regression fixture injects an all-fainted
run state to check the loss-summary and exit UI; this is not a native battle
loss.

The same scenario includes a distinct human-controlled native-loss test. Its
guarded pre-battle fixture starts the three selected active Pokemon at 1 HP and
gives them Growl, then lets native opponents faint them. The test reads the
adapter's actual battle result and requires those three KOs to produce
`RL_LOST`; it does not write HP or battle results after the fight. A seed-8 UI
suspend/resume check exercises the Controller Pak path. Host coverage performs
save roundtrips across 1,000 generated seeds after map preparation and each
battle/reward transition. Generated screenshots and savestates remain local
under `.local/rltest/play-run/`.

Run just the native-loss path while debugging battle input with:

```sh
python3 -m tools.rltest.test_play_run --native-loss-only
```

The complete scenario and `scripts/rl-gate.sh` were verified with
Mupen64Plus-Next's libretro core; the complete run reached frame 51937 and
reported three native wins plus the separate native loss summary. The
all-fainted UI-only fixture remains separate from that native-loss result.

Normal play exposes the complete canonical map and dispatches every node kind.
The first-three-row trainer route is now restricted to the guarded regression
test mode. Nonbattle scene controls and real-input node scenarios are documented
in [SCENES.md](SCENES.md). Recruit rewards come from the defeated trainer's
canonical roster; other rewards use the existing core generator.

For a visible test, build with `make rl` and launch the resulting
`build_rl/pokestadium-rl.z64` in RetroArch. At the title, press N64 Start and
pass Game Pak Check with N64 A. In Menu Select, hold L and press N64 A to open
Roguelike. N64 A confirms, N64 B backs out, the D-pad moves through lists, and
the C-buttons (right stick on the configured preview controller) navigate
battle moves.
