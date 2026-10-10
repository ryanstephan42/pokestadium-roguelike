# Roguelike pure-C core

`src/roguelike/rl_*.c` is the platform-independent run model. The modules marked
`PURE` use only `rl_core.h`, `rl_types.h`, and C89 language facilities. They do
not use libultra, game headers, libc memory functions, heap allocation, floats,
or global game RNG. The public interface is C89-compatible and can be built by
host GCC or the decomp's IDO compiler.

## Battle record boundary

`RlMon` is the integration record passed between run logic and battle scenes.
Its `base_stats[5]` and `stat_exp[5]` indices are **HP, Attack, Defense, Speed,
Special**. Do not use the legacy `STAT_*` enum from `22630.h` to index these
arrays. The Pokémon base-stat table is species-indexed (including its dummy
species-zero entry); the adapter is responsible for table lookup and translates
the decomp's legacy field names into the combat order above.

`dvs` is the Gen 1 packed DV word (Attack, Defense, Speed, Special nibbles in
ascending bit order); zero is a real DV value and must not trigger a default.
`moves`, current `pp`, and `max_pp` are separate unpacked byte arrays.
The battle adapter owns conversion to/from native packed PP-Up representation,
game-encoded nicknames, and concrete expanded/party records. `status` and
`fainted` remain separate: HP zero is authoritative for fainting, while status
holds non-faint conditions. `held_relic` is a slot identifier or `RL_NO_SLOT`.
Names supplied to this pure core are ASCII, NUL-terminated strings.

The compact Gen 1 record described in `src/22630.h` stores species, moves,
packed PP, DVs, stat experience, level, current HP, and nickname data. `RlMon`
keeps those fields in a wider, explicit, battle-friendly structure; it does not
alias or cast onto decomp structs. Keep conversion at the battle-entry/return
boundary so persistent HP/PP/status is not overwritten by the game's preparation
healing path.

W3's `rl_battle.h` adapter exposes `rl_prepare_mon(RlMon *, int full_heal)`:
it rebuilds species-derived native stats and PP limits, retaining HP/PP/status
and within-level EXP for persistent input when `full_heal` is false. Its battle
entry API returns updated mons with their original party indices, so a party
subset does not shift persistent run slots.

## Run lifecycle

1. `rl_run_new(&run, seed, ascension)` initializes a fresh draft state, the
   independent xorshift32 RNG, starting money, and first seven-row map.
2. Integrators draft/add starters with `rl_party_recruit`, then set
   `run.phase = RL_MAP`.
3. `rl_run_advance(&run, lane)` validates the selected outgoing map edge and
   moves to `RL_NODE`. Node scenes resolve centers, marts, and events directly;
   battles call `rl_run_on_battle_result`.
4. A won battle enters `RL_REWARD`; call `rl_rewards_generate` and
   `rl_reward_apply` for the player's selected offer. `rl_run_finish_node`
   advances the map or creates the next act. Acts 0–2 are followed by a finale
   at act 3: a single row, single lane, single Mewtwo boss encounter. Completing
   its winning reward phase marks `RL_WON`; it is not a fourth branching act.
5. Loss of the entire party or a lost boss battle marks `RL_LOST`.

`rl_run_advance` requires at least one party member. `active_mask` passed to the
battle-result API uses pre-battle party indices (bits 0–5). XP is applied only
to active mons, or half XP to the bench with Exp. Share. Ascension 3+ uses
permadeath as configured in `rl_ascension`; `rl_reward_apply` applies the
ascension money reduction before the Amulet Coin bonus.

## Deterministic content APIs

- `rl_random` / `rl_random_bounded` advance only `RunState.rng`; they never call
  `guRandom` or touch game RNG state.
- `rl_map_generate` creates a seven-row, two-to-four-lane DAG for acts 0–2. Every node is
  reachable from row zero, row six contains only bosses, elite nodes are
  excluded from rows zero and one, and at least one center is placed before
  the boss row. Edges are bitmasks by destination lane. Use `rl_map_valid` in
  diagnostics and host tests. Act 3 contains only `nodes[0][0]`, a boss with
  `target_species == 150`; all unused rows and lanes are zero. `RL_ROWS` is
  storage capacity, not the active row count: scenes must use
  `rl_map_rows(run.act)` (7 for acts 0–2, 1 for act 3). No struct layout change
  is required to represent the finale.
- `rl_enemy_generate` accepts an `RlPool` supplied by the caller. Pools from
  extracted trainer/rental tables can be passed without changing core logic.
  `fixed_order` preserves source order for signature teams; otherwise selections
  use the run RNG. In act 3, map boss nodes carry `target_species == 150` to
  identify the Mewtwo finale encounter; the integrator must supply its complete
  VS Mewtwo signature team, not a one-Pokémon Mewtwo record. The built-in
  four-mon pool is fixture data only, not final Stadium roster content.
- `rl_enemy_level` applies act base + 2 per row + ascension level bonus,
  +3 for elites and +5 for bosses, capped at 100. Elite/boss DVs are max;
  ordinary DVs and stat experience rise with depth. Scaled stats use the shared
  Gen 1 recalculation helper rather than linear HP scaling; level-ups preserve
  the Pokémon's absolute missing HP. Supplied records are copied and zero
  DVs/stat experience are not silently defaulted.
- `rl_mon_stat_value` follows native `GetPokemonDV` and `CalculateStatValue`
  in `src/22630.c:192–278`. The HP DV is Special's low bit in bit 3, Speed's in
  bit 2, Defense's in bit 1, Attack's in bit 0 (the legacy comments reverse
  these positions). Effort uses the smallest root at least the square root,
  capped at 255, divided by 4. Host regressions compare all 65,536 stat-exp
  values for each stat using eight asymmetric/one-bit/max/zero DV fixtures.
- `rl_xp_for_level` implements Gen 1 Fast, Medium Fast, Medium Slow and Slow
  growth curves. `rl_mon_add_xp` updates level/HP using Gen 1 stat math and
  preserves the mon's prior damage amount. Learnset/move replacement is left
  to a caller with game data.

## Party, rewards, events, relics

Party helpers cover recruiting/releasing/swapping/reviving, healing, and
between-node item use (`rl_item_use`). Potion, Super Potion, and Hyper Potion
restore fixed 20/50/200 HP respectively; Full Restore restores HP and status.
None can revive a fainted Pokémon—use Revive or a Center's explicit revive
choice. Percentage-based party/event/relic heals also leave fainted Pokémon
fainted. Rewards return three choices, or four with Earth Badge. Shop inventory
has act-scaled prices and includes consumables and a relic offer. A TM offer is
included only when the caller supplies a valid `RlMovePool`; its actual move ID
is stored in the run. Reward, shop, and Move Tutor TM IDs are carried forward
without pretending that arbitrary moves are legal. `rl_tm_teach` requires the
integrator to supply a species/move callback that confirms legality and returns
that move's correct maximum PP before teaching it.
Consumed TM slots are zeroed. Small shop output capacities select a window
from the entire eligible catalog rather than reducing which items can appear.

Fossil resolution requires a canonical pool entry for the chosen fossil species
(Kabuto 140 or Omanyte 138) and recalculates its stats at the event's level; it
does not relabel unrelated data. Silph Co. similarly recalculates the selected
canonical pool record at its new level. `rl_event_resolve` uses event and choice
indices matching `rl_events`; its final `RlMovePool` argument provides candidate
move IDs for Move Tutor and it mutates run data only, performing no UI or battle
transition. `rl_event_wild_options` returns up to three candidates, bounded by
both caller capacity and pool size. Resolving a displayed choice consumes the
same RNG draws and recruits that candidate; a failed full-party recruit does
not advance RNG. The twelve event entries include the eight launch-plan events
and four data-driven extensions. The placeholder pool is fixture data only;
W3/W10 extracted data should replace it at the integration boundary.

Relics are a 32-bit bitset indexed by `enum RlRelic`. Hooks expose stat and
typed-damage and money modifiers, end-of-turn and post-battle changes, reward count,
status immunity, Focus Band survival, critical bonus, PP maximum, and one-time
recruit consumption. The battle adapter decides precisely where to call these
hooks (stat rebuild, final damage, residual, battle end); no hook affects the
base game unless its caller opts in.

## Save wire format

`rl_save_format.h` exposes version-3 little-endian serialization and CRC-32
(IEEE polynomial `0xedb88320`). The 8-byte prefix contains `RLSV` magic,
version, suspend flag, and ascension. Fixed-width meta fields and six fixed
Hall-of-Fame records serialize to **361 bytes** including checksum, under the
512-byte meta budget. An optional fixed-layout run suspend record adds 556
bytes, for a maximum current record size of **917 bytes**. The suspend record
includes map encounter targets and stored TM move IDs; derived current stats
are recalculated from serialized base data on decode. Buffer capacity is
provided by the caller; the serializer allocates nothing and rejects malformed
species/PP/HP state, inconsistent derived HP, wrong-version, truncated,
oversized, or CRC-invalid records. Future layout changes must increment the
version and preserve explicit byte encoding rather than dumping compiler
structs.
Version 3 retains the byte layout and sizes but rejects the former seven-row
act-3 representation; the finale is encoded in the first map cell with all
unused cells zero. Round-trip regressions cover consumed TMs, completed daycare
returns, every preceding boss-row width, and the finale's map/node/reward/won
and lost states.

## Host validation

Run `make -C host_tests test` for strict C89 compilation, seeded determinism,
10,000 generated map validity checks, scaling, rewards/shop/TM detail, event
pool and RNG behavior, healing/revive rules, relic math, Gen 1 XP, party
operations, and meta/suspend save round-trips and corruption detection. The
target repeats the suite under AddressSanitizer and UndefinedBehaviorSanitizer.
`scripts/rl-gate.sh` invokes this target when the host test Makefile exists.
