# W9 native relic hooks

## Acceptance status

At `fbc416f` (after merging master with PRs #10–#12), vanilla `make` prints
`build/pokestadium-us.z64: OK`, `make rl` passes its CRC check, and
`scripts/rl-gate.sh` ends with `GATE PASSED`. The gate includes W7
`test_play_run`: three human-controlled native wins at frame 49495 with seed
`0x13579bdf`.

Native custom-battle acceptance used `python3 -m tools.rltest.test_native_relics
--scenario all` on the same ROM. All 16 scenarios passed: boulder, thunder,
marsh, cascade, volcano, ghost, cursed-power, cursed-multihit, cursed-fixed,
scope-lens, focus, leftovers, pp-pouch, pp-pouch-capped and cursed-tick in one
run, plus soul rerun after its scenario was made deterministic (harness-only
change). Each check reads native trace values from the hooked engine
execution and the settled persisted result; no battle state is forced.
For example, the Soul run blocked Toxic at round indices 0 and 1, the Toxic at
round 2 missed natively, and native toxic damage then ramped 22/44/66/88/110 HP
until faint.

Remaining caveat: human-controlled battles may count non-action phases as
completed rounds, such as a faint replacement with `unk_34 == 3`. That would
add Leftovers/Cursed ticks and shorten the Soul window. Custom CPU battles
showed one action pair per round. Switch, charge-move and replacement cadence
still needs dedicated dynamic coverage.

## Integration contract

`src/roguelike/rl_battle.h` extends `RlBattleRules` from 8 to 12 bytes:
the original eight bytes retain their offsets; `RlU32 relics` is at offset 8.
The run-loop adapter must set `rules.relics = run->relics`. Ordinary custom
battles use zero. `held_relic` does not substitute for this run-wide mask.

At PP Pouch acquisition, call `rl_battle_apply_pp_pouch(run)` after setting
the relic bit. It returns 1 if applied, 0 if absent/already consumed, or -1
with a native diagnostic for invalid input. Surface -1 in the scene instead
of treating it as a successful pickup. It never runs inside `rl_start_battle`.

Both call sites are wired in `src/fragments/rl/rl_play.c` (agreed with W7):
the mask is set once after rules initialization, and the Pouch helper runs only
after a successful PP Pouch reward apply. A negative return becomes a scene error.
Native post-battle copying still settles live HP/status into original records
before overlay release. W7 then maps selected indices back into the run and
calls the existing `rl_run_on_battle_result` for XP and Rainbow healing.

## Native surfaces

| Relic | Implementation and intentional scope |
| --- | --- |
| Boulder | Defense effective/base stats +10%, nearest integer, capped at native 999. |
| Cascade | Water move damage +15% after native random/type/accuracy calculation, before commit. |
| Thunder | Speed effective/base stats +10%, before paralysis; ordering and stage recalculation use those values. |
| Rainbow | Existing pure post-win party healing, after native result persistence. Not an overlay callback. |
| Soul | Reject opponent-inflicted major status during round indices 0 and 1. Existing statuses and voluntary Rest remain native. |
| Marsh | Special effective/base stats +10%; critical bypass uses the boosted base too. |
| Volcano | Fire move damage +15%. |
| Earth | Existing reward-choice count helper; no native damage effect. |
| Amulet | Existing reward-money helper; no native damage effect. |
| Exp. Share | Existing post-win run XP for unselected living party members. |
| Lucky Egg | Existing post-win run XP multiplier. |
| Leftovers | Living active player heals maxHP/16, minimum 1, once at completed-round phase 4. |
| Focus | One direct opponent hit per battle is limited to leave 1 HP; not Substitute, recoil, confusion, residual, or self-KO protection. |
| Scope Lens | Double native critical threshold, capped at 255, before the existing native random draw. |
| Silph Scope | Enemy reveal belongs to the selection scene; W9 does not fake a native combat effect. |
| PP Pouch | One native PP Up per current move on pickup, without restoring current PP; see replacement below. |
| Old Amber | Existing one-shot recruit API; the scene must supply the intended recruit. No native damage effect. |
| Master Ball | Existing one-shot boss recruit API; requires a scene reward/selection surface. |
| Ghost | Native Ghost moves +15%; native immunity/type rules still apply. |
| Cursed Power | Direct damage +10%, additive with type relics; active player loses 1 HP each completed round after Leftovers. Lethal loss routes through native faint/replacement/result processing. |

Native type bytes differ from the pure enum: Fire is `0x14`, Water `0x15`,
Ghost `8`. Damage hooks exclude `D_84390288` simulation mode, confusion
(`unk_48 == 0x14`) and miss/recoil (`unk_48 == 0x10`) paths. Fixed-damage
moves and Counter are included in direct-damage bonuses; Focus also handles
lethal direct fixed damage. Multi-hit moves reuse the unscaled roll on each hit,
rather than repeatedly multiplying the preceding scaled hit.

The RL build keeps the matching assembly for `func_84370B7C`, the native
switch-in stat initialization. An earlier revision forced the unverified
NON_MATCHING C body. That changed native battles even with no relics: W7's
`test_play_run` lead was never damaged, reproducibly across input timing
perturbations, while master passed. Its two callers now use the guarded
`rl_func_84370B7C`, which runs native initialization first and then applies
relic boosts to effective and unmodified Defense/Speed/Special. It writes only
values a relic changed. When Speed changed, it re-applies the native paralysis
quarter. This applies on initial copy and on every switch-in. No modified
stats are written back into persistent stat records.

## Actual-round boundary

`func_84320108` opens a round when both actions have been selected.
`func_843202A0` opens the corresponding switch-containing round.
The controller dispatcher `func_84328734` finishes an open round only when
`unk_38 == 4`, after the action/residual/faint progression has returned to
the next selection phase. A resident latch closes it before applying effects,
so repeated controller frames cannot create extra healing/status turns.
`func_8437345C` and `func_8437176C` are deliberately not turn-end hooks.

An action that ends the battle does not reach another phase 4; no terminal
Leftovers/Cursed tick is added at teardown. The end hook records the settled
native result, invalidates the cached player-side pointer, and clears ownership.
The fainted active mon is never revived by Leftovers. Cursed lethal loss sets
native phase 5 with side 0 as the fainted defender, letting the existing
animation, replacement, and party-exhaustion logic determine the result.

**Faint replacement is not a turn.** After a KO, `func_8430F350` forces the
surviving side to a committed no-op command (`unk_10 = 3`, `unk_04 = 1`).
The fainted side (`unk_654.unk_2D == 0x10`) chooses a switch, and
`func_8430F8B8` then dispatches `func_843202A0` exactly as for a real switch
turn. The same path covers the opening send-out. A human-controlled W7 trace
showed the result: rounds with no actions alternated with action rounds.
`rl_native_relics_open_round(enemy)` therefore records every commit (trace kind 8:
player/enemy command, player/enemy switch state, counted flag, active index).
It opens a turn only when neither side is in replacement state. A voluntary
switch (`unk_2D == 0xF`) is a turn. If a turn opens before the previous one
closed, trace kind 9 records the anomaly instead of merging silently.

`python3 -m tools.rltest.test_relic_cadence` drives human-controlled custom
battles (`player_cpu = 0`) with controller input only:

- `enemy-replacement` (Leftovers, Cursed and Soul): turn 0 KOs Magikarp, and
  Chansey's replacement send must be uncounted. Chansey's first Thunder Wave is
  then turn 1, which Soul must block. Thunder Wave must land after expiry.
  Every tick must equal exactly `min(hp + max/16, max) - 1`, chained with no HP
  change between ticks. The first two turns must each spend one PP. There is no
  terminal tick, and the persisted HP must equal the last settled tick.
- `player-switch` (Cursed): turn 0's tick KOs slot 0 through the
  `func_84328734` phase-5 route, which sets `unk_2C = 1`, `unk_38 = 5`, and
  clears `unk_20`/`unk_14`. The human replacement (slot 1) must be uncounted
  and must never tick. The voluntary switch to slot 2 is a counted turn and
  ticks once, and the turn-2 KO must produce a normal native win.
- Before every command, both scenarios idle 900 frames in the menu and require
  an unchanged trace and live HP.

Status: implemented and host-tested. Emulator proof is pending the shared
emulator queue. Charging moves and canceled second actions remain uncovered.

## PP Pouch replacement

The original pure definition's "+20% maximum PP" is not exactly representable
for arbitrary existing PP-Up levels. Native remaining PP uses six bits and
maximum PP is derived from the move table plus a two-bit PP-Up count; there is
no independent maximum field in the battle record. Native PP-Up increments
are `base/5`, except moves with base PP >=40 increment by 7, and the count
caps at three. Recomputing +20% from a persisted already-boosted maximum on
every launch would compound the bonus.

The explicit replacement is **one PP Up for each current party move on
pickup**, capped at three, with no refill. Future recruits/new moves do not
receive it. `consumed_relics` records the pouch pickup application and survives
suspend encoding along with the changed maximum and unchanged current PP.
The effect text and `rl_relic_pp_max` now use native single-PP-Up arithmetic.
The native helper derives current PP-Up count from the canonical move base
and stored maximum; the normal battle conversion encodes the resulting count.

## Evidence interfaces

`python3 -m tools.rltest.test_native_relics` launches supported scenarios through
the custom-battle API. It writes only launch configuration, never live battle
HP, RNG, scratch damage, turn counters, or native result flags. Select individual
scenarios with its CLI to keep emulator runs bounded.

Request 3 uses `gRlRelicTestParty`, `gRlRelicTestEnemy`, their count globals,
`gRlRelicTestRules`, and `gRlRelicTestMask`. Pokémon stats are prepared through
native conversion. Pouch tests apply acquisition twice before launch so an
incorrect repeated application is rejected.

`gRlRelicTrace` is resident, with count/dropped followed by 256 eight-word
events: kind, completed-round index, and six values. It reports stat inputs/
outputs (kind 1), actual damage commit inputs/outputs (2), completed-round HP/
PP and second-action state (3), status rejection (4), Focus use (5), native
critical threshold/draw/result (6), and settled end result (7).
The trace is diagnostic observation of hooked engine execution, not a source
of gameplay state. A dropped-event count makes overflow explicit.

`make -C host_tests native-relic-test` checks native 32-bit ABI, ownership
gating, type translation, stat caps, Focus consumption, critical threshold,
round latching, Soul expiry, lethal Cursed HP, and teardown invalidation.
Core tests additionally check native PP increments and suspend-state
preservation. These are host checks, not emulator acceptance.
