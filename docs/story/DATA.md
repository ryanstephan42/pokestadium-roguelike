# Red/Blue campaign data milestone

**This is not a playable story mode yet.** The importer and platform-independent
progression are implemented for review. The canonical traversal/retirement graph,
native per-KO callbacks, Controller Pak codec and UI remain unwired.
`Makefile` explicitly excludes `src/story/` from both ROM builds at this stage.
The generated catalog has `graph_reviewed = 0`; `sc_new` rejects it with
`SC_UNREVIEWED` rather than silently exposing every trainer in arbitrary order.

## Reproducing the source audit

```sh
python3 tools/story_data.py --fetch
make -C host_tests story-test
```

Only the first command permits network access. Public source is pinned to
[`pret/pokered` af519899719f0754965776faac0e836a3b906e6d](https://github.com/pret/pokered/tree/af519899719f0754965776faac0e836a3b906e6d).
The importer downloads an allowlisted subset of assembly data/map objects and
relevant scripts, not a repository clone, ROM, graphics, audio or dialogue
assets. Downloads live in ignored `.local/story-source/`; generated numeric
JSON/C tables live in ignored `build/story/`. Upstream assembly is not vendored.
The expected tree is anchored by committed SHA-256
`9a47f4f79fd8746ef3d87f4d3100743c2cbd4e54302b3114c64dc1f6e23913aa`,
independently checked against a fresh pinned GitHub API response. Its digest
covers root SHA and sorted `(path, mode, type, blob/tree SHA)` tuples (ASCII
compact JSON with sorted keys). URLs and entry order are not trusted identity.
Fetched and cached trees are authenticated before their blob hashes are used;
an editable root SHA string alone is not an authenticity check.
Cached content is checked against the authenticated Git blob SHA, with SHA-256
source provenance recorded in the JSON. Missing offline files, unsupported conditional
syntax, bad pointers, unknown species, duplicate IDs and hash mismatches are
explicit errors.

| Audited inventory, each English version | Count |
|---|---:|
| Ordinary trainer map objects | 332 |
| Scripted rival stages, one persistent starter branch | 8 |
| Fixed wild battle instances | 14 |
| Gift/revival choices | 6 |
| Non-recruitable ghost Marowak | 1 |
| Total distinct encounter IDs | 361 |
| Stored trainer party records, including alternatives and unused data | 391 |
| Party records unreferenced by the audited map/rival inventory | 39 |
| Canonical species / move PP entries | 151 / 165 |

**340 trainer encounter instances is an inventory, not a verified count of
trainers reachable on one campaign path.** Optional encounter expiration,
script prerequisites and mutually exclusive branches require graph review.
The JSON includes script event operands and source line numbers as evidence,
but does not pretend to infer control flow from them. All 39 unreferenced
party records are enumerated, including the three unused Professor Oak teams.
English Red/Blue have identical trainer/static/gift content in this bounded
scope. Their Game Corner and wild-table exclusives are not substituted into
canon trainer teams; those repeatable encounters are outside the initial scope.

## Source-derived team construction

- [`data/trainers/parties.asm`](https://github.com/pret/pokered/blob/af519899719f0754965776faac0e836a3b906e6d/data/trainers/parties.asm)
  supplies species, team order and common/per-mon levels.
- [`engine/pokemon/evos_moves.asm`](https://github.com/pret/pokered/blob/af519899719f0754965776faac0e836a3b906e6d/engine/pokemon/evos_moves.asm)
  supplies construction semantics: start with that species' base moves, process
  its own level learnset, skip duplicates, fill empty slots, shift when full.
  Evolved trainer species do not inherit simulated pre-evolution moves.
- [`engine/battle/read_trainer_party.asm`](https://github.com/pret/pokered/blob/af519899719f0754965776faac0e836a3b906e6d/engine/battle/read_trainer_party.asm)
  and [`data/trainers/special_moves.asm`](https://github.com/pret/pokered/blob/af519899719f0754965776faac0e836a3b906e6d/data/trainers/special_moves.asm)
  determine the gym, Elite Four and Champion slot-2 overrides. PP is populated
  after the final move assignment, not before.
- [`engine/pokemon/add_mon.asm`](https://github.com/pret/pokered/blob/af519899719f0754965776faac0e836a3b906e6d/engine/pokemon/add_mon.asm)
  and trainer constants specify DVs 9/8/8/8 and zero starting stat experience.
  `ScTemplate.dvs = 0x8889` uses the `RlMon` nibble convention, not raw GB bytes.
- The eight rival stages carry all three source variants. State locks one
  branch. Counter-starters apply to a selected canon starter or trio random
  mode; the other random pools choose a separate canon rival branch even if
  they happen to produce a canon starter.

Static inventory is two Snorlax 30, six Voltorb 40, two Electrode 43, three
legendary birds 50, and Mewtwo 70. Gifts are Magikarp 5, Eevee 25, one Dojo
choice 30, Lapras 15, one Mt. Moon fossil revival 30 and Old Amber revival 30.
Ghost Marowak 30 advances the story without a recruit/transfer reward.
Access/fossil-choice/expiration policy is deliberately deferred to graph review.

### Bounded retirement audit

The JSON now records eleven source-checked retirement rules, with exact trigger
and affected encounter IDs, event operands and pinned-source line numbers.
All eight gym leaders deactivate their remaining gym trainers (41 total);
the Dojo master deactivates the four other Blackbelts. Brock additionally
expires the optional first Route 22 rival. These 46 distinct retirements are
identical in Red and Blue. Source trainer-header counts (explicit battle-event
checks for Cinnabar's custom quiz script) and contiguous event indices must
match the expected deactivation directive or the audit fails. Silph Giovanni's
explicit hide list retires 30 additional trainers; the toggle constants and
object-state table are paired per floor and resolved to exact encounter IDs.
This does not retire the Silph rival or Lapras, which are not in that hide list.

Evidence: each corresponding `scripts/*Gym.asm` leader postbattle handler,
[`PewterGym.asm`](https://github.com/pret/pokered/blob/af519899719f0754965776faac0e836a3b906e6d/scripts/PewterGym.asm#L76-L79)
and
[`FightingDojo.asm`](https://github.com/pret/pokered/blob/af519899719f0754965776faac0e836a3b906e6d/scripts/FightingDojo.asm#L73).
Silph evidence:
[`SilphCo11F.asm`](https://github.com/pret/pokered/blob/af519899719f0754965776faac0e836a3b906e6d/scripts/SilphCo11F.asm#L73-L146),
`constants/toggle_constants.asm` and `data/maps/toggleable_objects.asm`.
These 76 retirements are independently bounded facts, **not a complete graph**.
Ship departure, rival prerequisites, static permissions, route access and
full branch/version traversals remain under review. The C emitter deliberately
does not install these partial rules or change `graph_reviewed = 0`.

## Isolated C89 progression

`src/story/sc_core.c` and `sc_progression.c` use `RlMon` as a boundary record,
not `RunState`, `RlMeta`, global game RNG, libultra, heap allocation or rogue
currency/relic hooks. Public functions return `ScStatus` values; native/UI
integration must display failures and must not advance a transaction on error.

The pure core provides deterministic starters/teams/source-slot rewards,
milestone/prior-encounter/retirement gates, six-slot confirmed replacement,
unrestricted donor move transfer, three healing presets, terminal loss,
per-opponent XP/stat experience, final-level move prompts, and cancellable
postbattle level/stone/trade evolution. Stone/trade permissions are explicit
inputs; the reviewed graph/UI must derive them from Mt. Moon/Celadon/Saffron
milestones, not arbitrary toggles. The state stores the selected encounter
variant to prevent reward rerolls or rival switching between launch and result.

Native integration must supply actual participation flags and synchronized
party HP before `sc_on_ko`. As in
[`engine/battle/experience.asm`](https://github.com/pret/pokered/blob/af519899719f0754965776faac0e836a3b906e6d/engine/battle/experience.asm),
divide base yield/stat-experience input by the flagged participant count before
level multiplication/division; skip fainted recipients; apply the trainer
1.5 multiplier afterward. There is no bench/traded-OT multiplier. Stat
experience accumulates normally, without copying donor stats as a reward.
Stats rebuild on level changes, not every stat-experience increment. Red/Blue
learns a move only at the final level reached by a level jump. The level-one
Medium Slow underflow glitch is intentionally normalized; all starts are level 5.
Evolution preserves moves, PP, experience, DVs, stat experience and missing HP,
and does not revive fainted Pokemon.

### Important native boundary discrepancy

[`home/move_mon.asm`, `CalcStat`](https://github.com/pret/pokered/blob/af519899719f0754965776faac0e836a3b906e6d/home/move_mon.asm)
derives HP DV with Attack/Defense/Speed/Special weights **8/4/2/1**.
The existing Stadium/roguelike helper uses the reversed weights for its native
layout. Consequently the existing native adapter cannot be assumed to preserve
canonical story HP when it recalculates stats. Story's independent stat helper
matches GB mechanics: Brock's Geodude 12 has HP 33 and Onix 14 has HP 36.
The later story bridge must explicitly preserve verified story stats in both
expanded and live native records without changing the rogue helper.

## Verification scope and remaining review

`story-test` checks source inventory/overrides/rivals/statics, parser errors and
cache tampering, every species at levels 1..100, all 65,536 stat-experience
values under eight asymmetric DV fixtures, 3,000 deterministic starter cases,
synthetic gate/retirement transactions, replacement cancellation, unrestricted
transfer, participant/rounding/learning behavior, healing, loss and evolution.
The C suite runs strict C89, ASan/UBSan and IDO 7.1 compilation. Generated data
also participates in strict C89 compilation.

The tested graph is explicitly a four-encounter synthetic fixture. **There is
no claim of native KO timing, durable save behavior, campaign balance or full
campaign reachability yet.** No emulator runs were made during the coordinator's
exclusive runtime slot. The full `scripts/rl-gate.sh` is still required before
the complete story feature can be declared finished; it must not be weakened
or described as passed based on host-only tests.
