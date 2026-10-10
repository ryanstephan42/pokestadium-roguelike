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
  identify the Mewtwo finale encounter. The real Stadium US VS Mewtwo roster
  contains **one Mewtwo** in both rounds, not a fabricated six-mon team. The built-in
  four-mon pool is fixture data only, not final Stadium roster content.
- `rl_enemy_level` applies act base + 2 per row + ascension level bonus,
  +3 for elites and +5 for bosses, capped at 100. Elite/boss DVs are max;
  ordinary DVs and stat experience rise with depth. Ordinary DV nibbles round
  upward to odd values so the derived HP DV cannot reset on an even ascension
  step; every stat is nondecreasing with ascension for a fixed source mon.
  Scaled stats use the shared
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

### Canonical Stadium encounters

`rl_content.h` defines stable logical roster IDs and `RlEncounter` descriptors,
independent of generated numeric set IDs. `rl_encounter_select(run, &descriptor)`
selects from seed, act, row, lane and ascension using a local RNG; it does not
advance `run.rng`. Repeated previews and save/reload therefore select exactly
the same trainer without adding encounter fields to the v3 record.

| Act | Normal / elite | Final-row bosses |
|---|---|---|
| 0 | Poke Cup Poke/Great/Ultra with depth; Master for elites | Last trainer in one of the eight Gym Leader Castle gyms |
| 1 | Prime Cup Poke/Great/Ultra; Master for elites | Even lanes: gym leader; odd lanes: random Elite Four member |
| 2 | Prime Cup by depth; Elite Four members for elites | One of the seven real Rival variants |
| 3 | None | VS Mewtwo, trainer 0 |

Ascensions 8–10 use the corresponding Round 2 source sets; earlier ascensions
use Round 1. `rl_load_encounter(const RunState *, RlMon *, int capacity,
unsigned char *ai)` in `rl_rosters.h` loads and scales the **complete** native
signature roster in source order, returns its count (normally six, finale one),
and preserves native AI. Supply capacity six. Invalid requests return -1 with
native logging; do not proceed to battle on failure. The caller chooses three
combatants under ordinary rules or up to six with `rl_ascension(...).full_team`;
the loader does not discard reserve mons. It rebuilds stats, heals HP, clears
status/fainting, and restores PP for the new enemy encounter only.
`rl_enemy_scale` exposes the same in-place scaling for canonical host records.
Native source rosters/rentals are generated locally; identical rental arrays
share storage while all public set IDs and descriptors remain unchanged.

Winning battles award guaranteed money:
`(100 + 75*act + 15*row) * encounter_multiplier`, with multiplier 1/2/3
for normal/elite/boss, then ascension money reduction and Amulet Coin.
`rl_battle_money` queries that value. The battle-result API awards it exactly
once, before the separate pick-one reward; scenes must not award it again.
XP is `8000*(act+1) + 1000*row`, multiplied by 2 for elites or 3 for bosses,
then Lucky Egg and active/bench participation modifiers. Money awards saturate
at the unsigned 32-bit maximum rather than wrapping.

### Meta/unlock boundary

`rl_meta.h` operates on the current v4 `RlMeta` without touching storage or UI;
the v4 codec migrates existing v3 profiles. Only initialize a profile with
`rl_meta_init` after explicit new-profile creation, never as a fallback for a
load error. Pool IDs are
`RL_META_POOL_STARTERS` (Poke Cup), `RL_META_POOL_PRIME_CUP`, and
`RL_META_POOL_LITTLE_CUP` (Stadium's Petit Cup). Use
`rl_meta_select_ascension` before `rl_run_new` to enforce the saved maximum;
the low-level constructor's clamp alone does not enforce unlocks.

`rl_meta_permitted_pools/relics/events` return catalog-bounded saved masks.
`rl_meta_choose_event(meta, &run.rng, &event)` uniformly chooses an unlocked
event, reporting `RL_EMPTY` without mutation if none are available.
The base starter-species catalog contains first-stage species with base-stat
totals at most 320. `rl_meta_choose_species` filters a caller-provided legal
rental catalog to unlocked species and selects among distinct species with
inverse-BST weights. Terminal recording stores the unregistered species on the
final team as a pending profile choice; `rl_meta_register_species` or
`rl_meta_skip_species_registration` consumes it, so the choice survives the
terminal save clearing the suspend record. `rl_meta_purchase_option` spends
saved currency once per available option bit and is idempotent for already-owned
options. `rl_meta_option_available` and `rl_meta_option_name` exclude option IDs
whose content is not implemented; event-pack, contract, and cosmetic IDs are
reserved and cannot be purchased.

`rl_meta_content.h` supplies the first functional option catalogs. Four move
options select alternate four-move sets only from their matching canonical
rentals: Poke Cup, Prime Cup, Petit Cup, and Prime Cup Round 2.
`rl_load_meta_rental_catalog` in `rl_rosters.h` copies only species and moves
from the corresponding generated native rental set; pass those records to
`rl_meta_list_move_variants`. It requires the same species, removes the current
set and duplicates, and copies only move IDs. Stats, DVs, stat experience,
items, and PP Ups are never inherited. Four purchased starter themes (Forest,
Coast, Cavern, Sky) intersect curated National Dex lists with the caller's
canonical starter rentals and profile-unlocked species. `rl_meta_apply_kit`
applies either the existing 300-coin start or the purchased Potion Cache kit
(one Potion and 100 coins); both have a provisional value of 300 using the
Act-0 Potion shop price. It only accepts a fresh empty draft and cannot add
value to the existing start.

These option prices and the kit valuation are proposals, not user-approved
balance values. Runtime scenes must source loadout/species candidates from the
generated canonical rental tables, present only non-empty lists, and apply a
selected kit only when creating a new run.
`rl_rewards_generate_unlocked` and `rl_shop_generate_unlocked` take the
permitted relic mask; their legacy counterparts intentionally expose all
content for compatibility and fixtures. `rl_event_resolve_unlocked` also
accepts that mask, returning `RL_EMPTY` before mutation for locked fixed
Ghost/Cursed relic choices. A shop relic detail of `RL_NO_SLOT` is unavailable,
not a purchasable placeholder. Event availability itself is checked with the
meta event APIs before resolution.

New profiles expose the base starter pool and the first eight relics/events.
The first win unlocks Prime starters, the fifth unlocks Petit starters, and
each win adds one subsequent relic/event. Clearing the highest unlocked
ascension unlocks the next, capped at 10. Call `rl_meta_record_run` only once
after `RL_WON`/`RL_LOST`, then save its changed metadata with the existing
Controller Pak API. It tracks saturated runs/wins, best floor (up to 22),
terminal-only currency and pending eligible species, and the latest winning
team. The terminal-summary integrator caches this candidate metadata across
uncertain writes and reloads to avoid applying a run's payout twice. See
`SAVES.md` for proposed economy and price constants.

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

`rl_save_format.h` exposes version-4 little-endian serialization and CRC-32
(IEEE polynomial `0xedb88320`). The 8-byte prefix contains `RLSV` magic,
version, suspend flag, and ascension. Fixed-width profile fields and six fixed
Hall-of-Fame records serialize to **416 bytes** including checksum, under the
512-byte meta budget. An optional fixed-layout run suspend record adds 556
bytes, for a maximum current record size of **972 bytes** (992 bytes after
storage-page padding). The profile extension stores currency, option ownership,
registered species, and a pending terminal-team species choice. The suspend
record includes map encounter targets and stored TM move IDs; derived current
stats are recalculated from serialized base data on decode. Buffer capacity is
provided by the caller; the serializer allocates nothing and rejects malformed
species/PP/HP state, inconsistent derived HP, wrong-version, truncated,
oversized, or CRC-invalid records. Future layout changes must increment the
version and preserve explicit byte encoding rather than dumping compiler
structs.
The decoder migrates version-3 records (361 bytes meta-only or 917 bytes with a
suspend) while preserving existing profile unlock bits and active-run seed,
RNG, map, party, HP, PP, and status. New v4 fields default empty. Version 4
also rejects the former seven-row act-3 representation; the finale is encoded
in the first map cell with all unused cells zero. Round-trip regressions cover
consumed TMs, completed daycare returns, every preceding boss-row width, and
the finale's map/node/reward/won and lost states.

## Host validation

Run `make -C host_tests test` for strict C89 compilation, seeded determinism,
10,000 seeds across **all four acts**, each including v4 encode/decode, plus
complete 22-node runs at all ascensions, monotonic scaling, unlocked
rewards/shop/event choices, meta milestones, rewards/shop/TM detail, event
pool and RNG behavior, healing/revive rules, relic math, Gen 1 XP, party
operations, and meta/suspend save round-trips and corruption detection. The
target repeats the suite under AddressSanitizer and UndefinedBehaviorSanitizer.
`scripts/rl-gate.sh` invokes this target when the host test Makefile exists.

`python3 tools/rl_balance.py --runs 1000 --seed 0` prints CSV containing run
win rates, per-act reach/battle win rates and player/enemy mean levels for all
ascensions 0–10. `--sanitize` compiles the host simulator with ASan/UBSan.
It uses locally extracted real Stadium teams, canonical base stats/growth,
the core's actual map/scaling/progression/reward APIs, a six-option Poke Cup
rental draft at source levels, and deterministic battle RNG separate from
the run RNG. Its route policy prefers recruits until full, battles for XP,
and Centers when hurt; events use Oak's heal and marts buy affordable potions.

**This is a power-model estimate, not a native Stadium AI/move simulation.**
Battles compare current-HP-weighted Attack/Special/Speed, model damage and a
seeded win probability, and do not implement typing, moves, PP usage, status,
or all battle relic hooks. CSV zeros are genuine model outcomes, not fabricated
success rates. Use the curves to spot difficulty cliffs, then calibrate with
W8/W9 native full-run emulator playtests before claiming final balance.
ROM-derived fixtures/executables are temporary and cleaned automatically.
