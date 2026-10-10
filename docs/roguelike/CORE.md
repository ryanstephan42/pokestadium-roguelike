# Roguelike pure-C core

`src/roguelike/rl_*.c` is the platform-independent run model. The modules marked
`PURE` include only `rl_core.h`, `rl_types.h`, and standard C library facilities
(`string.h` where needed). They do not use libultra, game headers, heap
allocation, floats, or global game RNG. The public interface is C89-compatible
and can be built by host GCC or the decomp's IDO compiler.

## Battle record boundary

`RlMon` is the integration record passed between run logic and battle scenes.
Its `base_stats[5]` and `stat_exp[5]` indices are **HP, Attack, Defense, Speed,
Special**. Do not use the legacy `STAT_*` enum from `22630.h` to index these
arrays. The Pokémon base-stat table is species-indexed (including its dummy
species-zero entry); the adapter is responsible for table lookup and translates
the decomp's legacy field names into the combat order above.

`dvs` is the Gen 1 packed DV word; zero is a real DV value and must not trigger
a default. `moves`, current `pp`, and `max_pp` are separate unpacked byte arrays.
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
   advances the map or creates the next act; finishing act 3's boss marks `RL_WON`.
5. Loss of the entire party or a lost boss battle marks `RL_LOST`.

`rl_run_advance` requires at least one party member. `active_mask` passed to the
battle-result API uses pre-battle party indices (bits 0–5). XP is applied only
to active mons, or half XP to the bench with Exp. Share. Ascension 3+ uses
permadeath as configured in `rl_ascension`; `rl_reward_apply` applies the
ascension money reduction before the Amulet Coin bonus.

## Deterministic content APIs

- `rl_random` / `rl_random_bounded` advance only `RunState.rng`; they never call
  `guRandom` or touch game RNG state.
- `rl_map_generate` creates a seven-row, two-to-four-lane DAG. Every node is
  reachable from row zero, row six contains only bosses, elite nodes are
  excluded from rows zero and one, and at least one center is placed before
  the boss row. Edges are bitmasks by destination lane. Use `rl_map_valid` in
  diagnostics and host tests.
- `rl_enemy_generate` accepts an `RlPool` supplied by the caller. Pools from
  extracted trainer/rental tables can be passed without changing core logic.
  `fixed_order` preserves source order for signature teams; otherwise selections
  use the run RNG. The built-in four-mon pool is fixture data only, not final
  Stadium roster content. Boss scripts should pass their signature pool.
- `rl_enemy_level` applies act base + 2 per row + ascension level bonus,
  +3 for elites and +5 for bosses, capped at 100. Elite/boss DVs are max;
  ordinary DVs and stat experience rise with depth. Supplied records are copied;
  DVs/stat experience are not silently defaulted.
- `rl_xp_for_level` implements Gen 1 Fast, Medium Fast, Medium Slow and Slow
  growth curves. `rl_mon_add_xp` updates level/HP using Gen 1 stat math and
  preserves the mon's prior damage amount. Learnset/move replacement is left
  to a caller with game data.

## Party, rewards, events, relics

Party helpers cover recruiting/releasing/swapping/reviving, healing, and
between-node item use (`rl_item_use`). Rewards return three choices, or four
with Earth Badge. Shop inventory has act-scaled prices and includes consumables,
a relic offer and a TM offer. `rl_event_resolve` uses event and choice indices
matching `rl_events`; it mutates run data only and performs no UI or battle
transition. `rl_event_wild_options` previews three Safari Zone candidates from a
copy of the run RNG; resolving option 0–2 consumes the same draws and recruits
the displayed candidate. The twelve event entries include the eight launch-plan events and
four data-driven extensions. The placeholder pool is also used by tests;
W3/W10 extracted data should replace it at the integration boundary.

Relics are a 32-bit bitset indexed by `enum RlRelic`. Hooks expose stat and
typed-damage and money modifiers, end-of-turn and post-battle changes, reward count,
status immunity, Focus Band survival, critical bonus, PP maximum, and one-time
recruit consumption. The battle adapter decides precisely where to call these
hooks (stat rebuild, final damage, residual, battle end); no hook affects the
base game unless its caller opts in.

## Save wire format

`rl_save_format.h` exposes version-1 little-endian serialization and CRC-32
(IEEE polynomial `0xedb88320`). The 8-byte prefix contains `RLSV` magic,
version, suspend flag, and ascension. Fixed-width meta fields and six fixed
Hall-of-Fame records serialize to **361 bytes** including checksum, under the
512-byte meta budget. An optional fixed-layout run suspend record adds 511
bytes, for a maximum current record size of **872 bytes**. Buffer capacity is
provided by the caller; the serializer allocates nothing and rejects malformed,
wrong-version, truncated, oversized, or CRC-invalid records. Future layout
changes must increment the version and preserve explicit byte encoding rather
than dumping compiler structs.

## Host validation

Run `make -C host_tests test` for strict C89 compilation, seeded determinism,
10,000 generated map validity checks, scaling, rewards/shop, relic math, Gen 1
XP, party operations, and meta/suspend save round-trips and corruption
detection. The target repeats the suite under AddressSanitizer and
UndefinedBehaviorSanitizer. `scripts/rl-gate.sh` invokes this target when the
host test Makefile exists.
