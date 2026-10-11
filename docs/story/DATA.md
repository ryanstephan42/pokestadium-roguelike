# Red/Blue campaign data milestone

**This is not a playable story mode yet.** The importer and platform-independent
progression and the accepted region graph are installed in pure C. The isolated
save codec/journal and KO transaction contract have host proofs. Guarded native
record/Pak backends compile but are excluded from ROMs; live per-KO callbacks,
physical Controller Pak runtime proof and UI remain unwired.
`Makefile` explicitly excludes `src/story/` from both ROM builds at this stage.
The generated catalog has `graph_reviewed = 1` only after the importer checks
the exact accepted graph/provenance digest. Unreviewed catalogs still return
`SC_UNREVIEWED`; changed facts or policy fail generation instead of inheriting
review approval.

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
Access/fossil-choice/expiration policy is installed from the accepted regional graph.

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
Ship departure is also checked as a distinct event window: all 17 ship
encounters become inaccessible only after receiving HM01 and exiting through
dock destination warp 1. Winning the ship rival is not itself the retirement
trigger. The menu must warn before the departure action, allowing remaining
ship trainers to be visited first.
[`VermilionDock.asm`](https://github.com/pret/pokered/blob/af519899719f0754965776faac0e836a3b906e6d/scripts/VermilionDock.asm#L5-L11)
provides the checked conditional sequence.

All eight rival selectors are now checked against their actual script branch
jumps/team loads (including Route 22's starter table lookup), not merely the
presence of an `OPP_RIVAL` operand. Their source party index order is
STARTER2/STARTER3/STARTER1, consistently Squirtle/Bulbasaur/Charmander.
Rival prerequisites, static permissions, route access and
full branch/version traversals were accepted at exact review head
`973fbd4507e2f104a8c12575207afe6702182316` under the user-approved scope.

### Access facts and candidate traversals

Nineteen exact source sequences are now checked in both retail versions and
recorded with source line ranges: Cut/Cascade, Surf/Soul and Strength/Rainbow
badge eligibility; the corresponding HM acquisitions; Tower Silph Scope
identification; both finite Snorlax/Flute triggers; fossil choice exclusion;
Old Amber acquisition; seven badges opening Viridian Gym; and Hall of Fame
removing the Cerulean Cave guard; the lab's exact fossil item/species mapping
and level-30 handover; both Dojo gifts' mutually exclusive claim guards;
Bill's ticket/guard relocation and Fuji's Silph entrance guard relocation.
Virtual Console-only battle patches are
explicitly excluded, just as debug-only code is excluded.

Important references:
[`start_sub_menus.asm`](https://github.com/pret/pokered/blob/af519899719f0754965776faac0e836a3b906e6d/engine/menus/start_sub_menus.asm#L152-L180),
[`core.asm`, IsGhostBattle](https://github.com/pret/pokered/blob/af519899719f0754965776faac0e836a3b906e6d/engine/battle/core.asm#L3309-L3324),
[`item_effects.asm`, ItemUsePokeFlute](https://github.com/pret/pokered/blob/af519899719f0754965776faac0e836a3b906e6d/engine/items/item_effects.asm#L1671-L1702),
[`HallOfFame.asm`](https://github.com/pret/pokered/blob/af519899719f0754965776faac0e836a3b906e6d/scripts/HallOfFame.asm#L99-L101).
Battle core source has a separately bounded 256 KiB download allowance; all
other allowlisted source files retain the 128 KiB limit.

`tools/story_graph.py` describes the **reviewed region policy**. It gives all
361 inventory IDs explicit region/dependency clauses and
22 nonbattle milestone nodes. Full sweeps cover both versions, all three
locked rival branches and both fossil choices (12 traversals), without
duplicate encounters or dangling edges. Destructive milestones are postponed
until available optional encounters are consumed; minimal eastern/western
Fuchsia traversals instead exercise skipped encounters and retirement.
Twenty-four minimal Champion traversals cover every version/rival/fossil
choice through both coasts without requiring optional gym trainers or gifts.
Every retirement trigger and healing access/expiry clause is exercised in
both versions, including Standard reuse, Limited once and Brutal denial.
Both Fuchsia approaches remain usable, field permissions require the correct
badge, no HM move slot is required, and the selected fossil branch carries
through to its single revival offer. Captain-room HM01 receipt is a separately
named action that sets `EVENT_GOT_HM01`, not an implicit result of the rival win.
Ship departure additionally requires explicit dock destination warp 1; neither
the rival win alone nor a missing/wrong warp retires optional ship encounters.
Receiving HM01 does not require Misty's badge; using Cut does.
The candidate orders the five Nugget Bridge trainers before the Rocket and
the Route 25 branch, using source object positions and the documented bridge
sequence. Fossil variant 0 is explicitly Helix/Omanyte and variant 1
Dome/Kabuto; revival cannot independently switch that branch. Per-encounter
tests check all static/gift permission clauses and deny premature access
(Scope, Flute, Surf, Strength, Dojo/Silph clearance, fossils and postgame).

**Direct user scope decision, relayed by the coordinator:** "Yes—optional
regional trainers, with required rivals/gyms/story battles". Thus regional
trainers are optional unless required by an explicit storyline chain; this
does not reproduce terrain line-of-sight forcing or exact tile navigation.
Nugget Bridge's five trainers/Rocket, the fossil guard, Cerulean thief,
required rival stages, Hideout door guards/lift-key trainer, Tower ghost and
Fuji Rockets, Silph rival/boardroom guard and all eight gym leaders/League
remain explicit required chains. The early Route 22 rival alone is optional.
Midgame gym freedom and both Fuchsia approaches are preserved.
The thief gates the eastern story route, not Vermilion: the region-menu
southern underground route, Route 6/11, Bicycle voucher and Vermilion Center
open on reaching Cerulean. Bill's ticket remains required for the ship.

These tests prove the approved region-menu graph invariants, **not exact
original-game terrain reachability**. Free nonbattle travel/item actions
are the planned no-economy menu adaptation, not trainer wins or extra XP:
Bill/ticket, captain/HM01, fossil acquisition/revival, Celadon guard beverage,
Bicycle, Safari Surf/Gold Teeth/Strength, Card Key and Mansion Secret Key.
No shops, repeated item purchases or additional wild battles are introduced.
Route 19's two shore trainers remain accessible before Surf, while sea
trainers require Surf. Exact original tile-path proof is no longer an
acceptance requirement under the explicit user scope.
The bare candidate constructor does not grant approval. `reviewed_graph`
authenticates both version graphs plus all numerical/source audit data against
SHA256 `42abbcfced4b4e824b8495b0579b0b443d14715001e85d5700588b7a534eb42e`
(accepted head above), then emits the C graph. Factual, provenance or dependency
changes require a new review identity. C nodes preserve alternative all-of
clauses, explicit retirement triggers, fossil locking and dock warp checks.
Caller-supplied milestone grants cannot bypass installed canonical actions.

### Healing permission evidence

The importer identifies 15 distinct canon healing locations: twelve Pokemon
Centers (including Mt. Moon, Rock Tunnel and Indigo), home, Tower 5F's purified
zone and the Silph 9F nurse. Each has a checked nurse macro or `HealParty` call
and source line. No S.S. Anne healer is invented from later remakes.
The Silph nurse is **not permanently available**: her script tests
`EVENT_BEAT_SILPH_CO_GIOVANNI` and branches around `HealParty` after that win.
[`SilphCo9F.asm`](https://github.com/pret/pokered/blob/af519899719f0754965776faac0e836a3b906e6d/scripts/SilphCo9F.asm#L144-L161)
is the exact expiration evidence.

The host candidate attaches access clauses to these stable identities, applies
the locked Standard/Limited/Brutal rules, and prevents all healing during the
League sequence. Tests cover unavailable-before-access, once-per-location use,
Brutal denial, Silph retirement and League lock. These permissions are not yet
wired into native UI. Pure C healing uses the same installed permissions,
including calls through the original `sc_heal` entry point. Access uses unlocked regions rather than current
tile position: returning to an unlocked healing location is free, except for
the explicit League lock and Silph nurse expiry. Standard never consumes a
healing location; Limited consumes each identity once and Brutal denies all.
Vermilion Center no longer inherits the ship's Bill/ticket prerequisite.
Its earlier access is the explicit no-tile-path region abstraction through
Route 5/6's ungated underground connection, not a claim about collision tiles.
Silph nurse retains the Card Key door-access condition; her source expiry
after Giovanni remains independent of that condition.

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
The guarded `sc_native_probe` writes canonical stats/DVs directly into the
actual expanded native record layout without calling `rl_prepare_mon` or
Stadium stat calculation. Its 32-bit host probe confirms Geodude's HP 33 and
all stat fields survive this boundary. This is **not** proof that subsequent
live initialization/stage recalculation preserves them.

## Isolated saves and KO boundary

`sc_save.c` uses an explicit little-endian 736-byte version-1 record, not a
dump of `ScState` or compiler padding. The header binds the exact accepted
graph identity; CRC32 covers header and payload. A different graph identity
or unknown version is rejected without migration or mutation of outputs.
Validation includes canonical base/growth/PP and bounded Gen I stats, starter
rival consistency, completed dependency clauses, exact retirement bits,
locked fossil choice and an authentic pending donor offer.
Inactive party slots and non-reward offers must contain zero serialized
fields; non-reward `offered_slot` is `RL_NO_SLOT`. Resolved learning clears its
return phase, and inactive learning fields are rejected. These checks exclude
padding bytes and prevent unused state from surviving a save roundtrip.

`sc_storage.c` is a two-slot, 1536-byte isolated note backend behind checked
device callbacks. The native note identity is company `0x524c`, game `SC01`,
name `STADIUM STORY`, extension `V1`, never `STADIUM RL`/`RL01`. Creation is
explicit. The inactive slot is invalidated, its payload written/read back,
then its header committed/read back. The previous valid generation survives
torn writes. Device, CRC, version and data errors are distinct results;
`SC_SAVE_RECOVERED` explicitly reports fallback to the surviving record.
No EEPROM, cartridge, Transfer Pak or rogue meta data is touched.
`sc_pak_probe.c`, behind `STORY_NATIVE_PROBE`, implements physical discovery,
allocation and 32-byte I/O using the existing checked raw-Pak approach. It
reuses a parameterized read-only filesystem validator and the stock libultra
allocator, not rogue saves/meta. The original RL identity wrapper remains;
tests verify simultaneous disjoint RL01/SC01 notes and wrong-name/page-count
rejection. A note with the requested company/game but an incompatible name
or extension fails closed instead of being treated as a creatable absence.
No formatting or automatic filesystem repair occurs. Device identity is
checked before/after transfers; SI access is serialized. The guarded backend
has target IDO compilation, **not physical/emulator I/O proof** yet.

`sc_bridge_begin` persists the route state as an interrupted prebattle
checkpoint before admitting a battle. `sc_bridge_commit` publishes its win,
reward or terminal defeat only after durable storage succeeds. Storage errors
are returned through the required `storage_status` output; callers must display
them and block advancement. A failed commit leaves the in-memory precommit
state retryable, including its RNG and finite reward selection.

The pure bridge copies HP/status/PP snapshots, tracks participant flags per
enemy, and delegates exactly-once XP to `sc_on_ko`. `SC_LEARNING` blocks further
observations until the prompt resolves. No native pool pointer is retained.
`STORY_NATIVE_PROBE` guards the expanded-record stat and snapshot feasibility
adapter; it is excluded from both default ROM builds. A real native caller must map
compacted living-team indices back to party slots, use original party PP for
Transform, stop before next action/switch on learning, and refresh canonical
stats/HP/learned moves after each KO. Fragment-62 scheduling, live-stage handling,
resident storage and post-pool lifetime still need runtime proof before UI.

### Guarded runtime probe stage (not runtime acceptance)

`make story-probe` first checks vanilla, then produces the separate ignored
`build_story_probe/pokestadium-rl.z64` with `ROGUELIKE=1 STORY_PROBE=1`.
Neither `make` nor `make rl` enables it. There is no story menu/campaign UI.
Vanilla remains byte-matching. Checked native conversion returns are isolated
to the probe build; the default RL path retains its original void conversion.
The harness drives `gScProbe` through the existing debug battle launcher.
Commands are 1 load, 2 explicitly create/store a fresh isolated story,
3 store an interrupted checkpoint and launch the fixed lab probe, and
4 retry a failed completed-result write. Save results use `ScSaveStatus`;
core/native precondition failures use `0x100 + ScStatus`. An invalid live
observation logs an error and holds the battle instead of fabricating success.
The probe rejects a native draw as an unresolved result (`0x100 + SC_PENDING`),
logs it, and leaves the interrupted prebattle checkpoint intact. Command 1
explicitly reloads that checkpoint before retry; result-write retry command 4
cannot commit the draw. Campaign draw policy is not decided by this probe.

The fixture is deliberately **not a playable campaign**: it uses Bulbasaur
level 6 with experience one point below level 7 and DVs `0x8889`, against the
authentic lab rival team. Its canonical HP is 22 before and 24 after that KO;
Stadium's reversed HP-DV weights would produce 23 and 25. The synthetic probe
can set enemy HP to zero and a +1 Attack stage; a separate natural-KO run
must pass without that injection before real KO timing is accepted.
The first fixture has one living party member. Full six-member runtime
participation, compacted switching and other campaign encounters are not
proven by it; existing host tests cover the pure compacted mapping.

The two guarded checkpoints in `func_84300E88` observe before presentation/
battle logic and immediately after `func_8432AEE4`. Learning suppresses both
advancement paths; the interrupted frame's post-logic presentation and result
are resumed once after learning, without reexecuting its logic. Frame counters
make this assertion observable. KO refresh writes
the canonical original record and live HP/level/stats/moves while preserving
stage indices, Stadium's stage rounding and burn/paralysis modifiers.
Transform keeps its copied live stats/moves/PP while original party PP
remains authoritative. These are source/host assertions, **not proof of every
native stat recalculation, action ordering or Transform execution path**.

At accepted diagnostic head `777e7b6`, the authenticated catalog remained exact,
not a truncated probe catalog.
Its 141-KiB native image cannot fit in the remaining retail ROM gap.
Probe-only linker support places it at ROM `0x02040000` and reserves fixed
resident RAM before the main heap; checked synchronous cartridge DMA loads
and validates it before any story API. The linker rejects overlap with the
existing late RL overlay. Story state/bridge/mailbox also remain in fixed
main BSS, not fragment/BATL allocations. The harness compares catalog RAM
with its linked ROM image and verifies it survives pool release; actual
DMA, heap re-anchoring and residency still required runtime review. The
source/host hot/cold stage below supersedes this fixed-resident layout.

`tools/rltest/story_probe.py` requires explicit `--execute`; **do not run it
until the exclusive emulator slot is granted**. It prepares separate-process
SC01 creation, interrupted cold reload, synthetic and natural KO/learning/
stage checks, completed cold reload, and missing/full/damaged-Pak checks.
It seeds independent RL01 and unrelated notes and restricts writes to SC01
pages/directory/inode ownership. Outputs are ignored, and no savestates are
used. Its helpers and opt-in execution hold have host-only tests.
The native scheduling harness was first executed only after the exclusive
slot was granted, following master `1bc06e2`. That original resident-catalog
stage was **blocked before the first battle tick**. The later bounded hot/cold
evidence below supersedes that refusal, not the remaining proof requirements;
no merge or full story UI activation is authorized.

#### Bounded native investigation

The harness keeps the L menu trigger held until the command completes and
uses the installed core discovery API. It records mailbox/launch/thread
state on timeout and has a 180-second watchdog, canceled on success or error.
Separate-process SC01 create/foreign-note preservation has passed. The
`allocation` diagnostic phase is deliberately separate from the interrupted
live-battle phase: it expects an explicit initialization failure, then saves
the prebattle checkpoint for a fresh-process load. It does **not** prove any
KO, learning pause or battle scheduling.

The Game thread has a 6,368-byte stack. Initial checkpoint-store stalls
preceded fragment entry. Target frames for poll/store/scan/decode/validation
were 1,584/3,056/760/712/1,376 bytes, respectively. Moving poll snapshot/result
and commit scratch to probe-only resident BSS reduces poll to 112 bytes and
commit to 24 bytes and permits the checkpoint write. These workspaces belong
to the single synchronous Game-thread command; frame callbacks never call
poll/commit recursively. Full stack high-water/reentrancy proof is still
outstanding.

The next observed failure was a null stadium-model dispatch, not a KO issue.
The selected arena-3 archive entry has 18,560 compressed and 39,536 resident
bytes. `func_80003DC4` retains a 24-byte header and compressed block during
decompression. With 16-byte alignment/block headers and the pool accessor's
one-block adjustment, the required caller-visible space is 58,160 bytes.
The instrumented build has only about 23 KiB available there. Story-only
preflight checks the ROM entry bounds/header and required allocation before
calling the retail loader; failure logs an error, unwinds BATL/RLBT and
returns `0x100 + SC_INVALID`, without a result commit or native tick.

The full exact catalog still occupies fixed RAM before `gPool`. Main BSS
and catalog reservations reduce the retail 4-MiB pool; `Util_InitMainPools`
does not automatically enlarge it on an 8-MiB emulator. No expansion-memory
requirement, asset truncation, shared-pool catalog reuse or broad layout
workaround has been introduced. This is a real residency blocker requiring
a safe size/lifetime design before the remaining native cases can proceed.

The integrated diagnostic layout adds 31,552 bytes of story text, 432 bytes
of story data/rodata and 4,704 bytes of story BSS (4,720 including alignment)
to the main segment. The exact catalog is 141,360 bytes: 82,308 encounter
array bytes (361 reachable entries), 24,512 graph-node bytes, 6,232 species
bytes, 166 PP bytes, 480 healer bytes, 52 descriptor bytes and remaining
strings/alignment. Stored source records remain a separate inventory count.
The pool start moves by 178,352 bytes versus the integrated default RL build.

A default-RL exact-team comparison completed two 2-living/3-enemy battles.
Whole-core-frame observations reached 13,264 raw free pool bytes; this is
neither an intra-frame peak bound nor six-party proof. Removing only the
catalog cannot be assumed sufficient. A proposed retail-memory design is to
keep cold graph/catalog/codec/checkpoint logic in an owned, unloadable story
overlay; materialize a pointer-independent combat view before releasing it;
keep only hot native/XP/learning logic resident; reload and revalidate the
exact catalog after BATL/RLBT return before reward/save commit. Species and
PP for all 151 species consume only 6,398 bytes; a copied current encounter
adds 228 bytes. Combat APIs would need explicit entry/view inputs instead
of a sparse fake catalog. The existing pure full-catalog APIs and exact graph
identity must remain behaviorally equivalent, with separate differential
tests. This broader redesign is a **proposal, not an implemented or proven
memory solution**; pointer lifetime, paused learning, error retries, code
residency and six-party peaks all require another bounded review/proof.

#### Retail hot/cold source/host redesign (native proof pending)

The probe now links `fragmentSC` at relocatable address window `0x8F900000`
and ROM `0x02040000`. It uses the existing fragment header, generated two-pass
relocations, loader/free callback and owned `SCLD` pool state. The entire
authenticated catalog, graph, team materialization, campaign transitions,
codec/journal and SC01 Pak backend are cold. No canonical encounters, species,
moves, graph nodes or provenance identity were dropped or regenerated with
different data. Default vanilla/RL builds do not compile or load this overlay.

Resident orchestration calls cold prepare synchronously, waits for it to
return, then pops SCLD before invoking native battle. `ScCombat` is 732 bytes:
copied species records for the six party members and six enemies, move PP,
the current enemy team and value identity/kind/variant fields. It has **no
pointers**, including no `ScEncounter.id`. All party/opponent native records,
state, frame hooks, XP/stat calculation and learning remain resident.
The full-catalog APIs delegate to the same hot calculations; all emitted
encounters/variants are differentially exercised with six participants.

Cold loading preflights the larger of ROM/RAM images, the retail loader's
temporary 0x18 header, SCLD state and allocation block headers. Reentrancy and
cold calls during an active battle are rejected. The loader returns fully
out of cold code before popping SCLD, whose existing fragment free callback
clears the memmap. Diagnostics record the before/after pool balance and clear
the transient overlay address/busy flag after unload. An ELF check rejects
any resident relocation into the cold code/data/BSS window.

Only after native BATL/RLBT cleanup and active-flag reset may resident code
reload the full cold image, validate it, apply a result to a scratch snapshot
and commit SC01. Cold-load/validation/store failure retains the durable
prebattle checkpoint and leaves reward/state publication pending; commit
retry is still deterministic. A draw remains unresolved and cannot commit;
successful checkpoint load invalidates the previous combat view. The SC01
backend uses the resident SI event queue and synchronous completed transfers;
its temporary callback/context pointers do not survive the cold invocation.

The source-stage map places the pool at `0x8011eae0`, 13,712 bytes after
the integrated default RL pool start, versus 178,352 bytes at `777e7b6`.
The cold RAM image is 168,256 bytes; ROM plus relocation table is 170,736
bytes. These are **layout measurements, not proof of retail peak headroom**.
The previously measured default frame-boundary minimum was only 13,264 raw
free bytes; six-party/native setup peaks, cold-load stack high-water and
actual overlay relocation/ownership must still be measured. No Expansion
Pak requirement or pool-end extension is introduced.

Host tests cover loader exact-threshold refusal, nested-call rejection,
load/entry failure cleanup and retry, immutable value inputs, all
encounters/variants with six participants, duplicate KO rollback, learning
pause, draw refusal, cold-reload failure before commit and successful retry.
The watchdog no longer reads core/RDRAM from its Timer thread: it records
only a best-effort Python frame counter and terminates on deadline.
The old `777e7b6` checkpoint-only/refusal evidence stays separate; no emulator
was run for this new hot/cold stage before explicit slot reacquisition.

The first hot/cold source head (`731aa84`) still had nested journal frames
capable of exceeding the Game-thread stack. Probe-only scan/load/store
buffers now live in cold-overlay BSS, owned by the non-reentrant synchronous
SCLD invocation and discarded at unload. The non-probe pure implementation
retains automatic buffers. Target scan/load/store frames fall from
760/1,536/3,056 bytes to 88/48/64 bytes. Cold RAM grows to 173,456 bytes
(ROM plus relocations 170,960); hot residency is unchanged. Full target
call-stack/high-water and cold allocation peaks remain runtime proof tasks.

#### Accepted bounded live interruption and next diagnostics

Exact `a4f20fa3e555d703eff1f4e10b5f90ba342b4416` passed the explicitly
authorized sequential cap-4 `create`, `interrupt`, and fresh-process
`interrupted_reload` phases. Frozen ROM SHA256 is
`78dc9c6cb827ac772262a474a2efcb3eb78983bbef30e24c434de4a3738618d6`.
Ignored result/provenance files remain in
`.local/rltest/story-live-a4f20fa/`, separately from `777e7b6`.
The live callback observed two ticks, one logic and presentation step, zero
KOs, level 6/XP 235, HP/native maximum 22 and DVs `0x8889`. Cold-load pool
availability was 2,953,056 bytes both before and after unload; the sampled
live accessor value was 187,936 bytes. Fresh-process SC01 load restored the
interrupted ROUTE checkpoint with the same completed CRC and no storage write.
This proves interruption after real battle entry, not KO/learning or worst-case
memory. The slot was released without remaining emulator processes.

The next probe-only instrumentation records actual fragment `0xe9` vaddr and
size before load, after relocation and after SCLD pop. The harness independently
reads both words of `gFragments[0xe9]` at the native callback/reload boundary.
Main-pool diagnostics record minimum **raw** availability after every
allocation attempt, attempt/failure counts and largest aligned request including
its block header. Counters reset at each command, so transient loader/native
setup peaks are included rather than inferred from frame-boundary samples.
These counters include other threads' main-pool allocations during the interval.

The idle thread paints the Game stack with `0xA5` before creating/starting that
thread, never overwriting active frames. Main-thread harness reads report the
untouched bottom margin and written high-water since thread creation, covering
returned cold calls and live callbacks. The bottom 64-byte sentinel must remain
untouched. This measures stack **writes**, not the deepest unwritten reserved
SP or an exhaustive whole-program bound; combine it with the independently
inspected 3,600-byte journal-chain subtotal and further native cases.
Diagnostics do not change allocation decisions, pool ends, team data or
gameplay. Their source/host build is not native acceptance; another explicit
slot grant is required before rerunning the bounded sequence.

The first instrumented `62cdd43` create run stopped on its map assertion.
An incremental-build dependency omission left `sc_runtime_probe.o` with the
old 188-byte mailbox while the newly compiled loader wrote the 212-byte ABI.
ELF addresses placed `gScProbeActive` at mailbox offset 188; loader disassembly
stored new fields at 188..208, overlapping following resident storage.
That failed run is not accepted evidence, despite successful SC01 status and
direct post-pop map zeroes. Story objects now depend on all story headers;
the fragment-62 mailbox user has an explicit dependency too. Before any probe
write, the harness checks the exact ELF symbol spacing against its field
count and rejects the old ABI. It also reads the resident compiled
`sizeof(gScProbe)` constant, so neighboring-symbol alignment cannot hide a
wrong object size; a regression rejects wrong size even with matching spacing.
Frozen failed artifacts/evidence are retained.

The corrected `85011e0` create phase passed. Its written-stack margin was
260 bytes (6,108 written of 6,368), so the journal path remains close to the
stack bottom despite the intact 64-byte sentinel. The following live interrupt
stopped on one unattributed failed allocation among 97 attempts; raw minimum
was zero and written-stack margin 1,340 bytes. Direct map and canonical live
checks passed, but fresh reload was not run. These are bounded observations,
not complete memory acceptance.

Retail fragment-62 setup intentionally creates temporary geometry subpools
with `func_80002D10(main_pool_get_available(), 0)`, consuming raw main-pool
availability to zero, then shrinks them with `func_80002D60`. Thus raw zero
alone is not a required-resource shortage; the failed allocation must still
be attributed. Probe-only tail wrappers preserve the caller return PC in an
extra internal diagnostic argument without changing public allocator ABI or
allocation decisions. First-zero and first-failure records retain requested
and aligned/header-charged sizes, side, raw before/after, caller return PC,
probe stage, cold-busy/active flags and returned address. For
`main_pool_alloc`, the PC identifies its consumer; direct
`main_pool_alloc_from_pool` callers identify the pool-state/realloc wrapper.
Rejected reallocs that never reach that function remain outside these counters.
Result/memory JSON is persisted before assertions reject a boundary. No
failure is waived or threshold relaxed; another reviewed bounded attribution
run is needed to distinguish optional probing from required resources.

Before another create run, `native_open`'s OSPfs and fixed
zero/name/extension/original-ID buffers move to probe-only cold BSS under the
existing synchronous cold-call reentry guard. OSPfs is fully zeroed on each
creation; all other buffers are overwritten before use. No callback or
pointer survives the cold invocation. Pure/nonprobe code is unaffected.
Target `native_open` frame falls from 280 to 88 bytes, reducing every nested
create-allocation path by 192 bytes without changing underlying Pak frames.
Cold RAM rises from 173,456 to 173,760 bytes; ROM plus relocations is 171,296
bytes, so the caller-visible cold-load preflight grows to 173,840 bytes.
The previous 3,600-byte inspected decode/validation chain is unchanged: it
does not include native-open's separate create-allocation branch. Written
high-water must be measured again, not inferred from the 192-byte reduction.

Exact `241ba0f` attribution retained a 260-byte full-run create stack margin;
the reduced Pak frame does not identify the peak's phase. The harness now
compares a read-only precommand stack snapshot with the boundary snapshot,
retaining the full-run sentinel/watermark and reporting newly written bottom
bytes. It never repaints an active stack.

The failed 37,296-byte PRS destination is uniquely identified in the frozen
local ROM at `0x8CC400`, archive `0x8CC000` entry 0 (offset `0x400`,
compressed length `0x3E10`). The consumer chain is fragment-62
`func_8432D0D8(0)` -> `func_8432D150` -> `func_8432D178` ->
`func_8432C86C` -> `func_80019EA0(0)`. Its NULL result previously left all
256 animation-table pointers empty via `func_8432C830(NULL)` yet continued
to live entry. The probe now logs this required-resource failure, marks
stage 13, and returns through the existing BATL/RLBT cleanup without ticking
or committing. The vanilla/default-RL path retains its original behavior.
This preserves failure semantics, not a residency fix.

The dedicated `STORY_NATIVE_PROBE` build now excludes the unrelated RL
relic trace ring (8,200 bytes including count/dropped) and trace writes.
Relic calculations, battle hooks and public entrypoints remain intact;
normal RL compilation keeps the original trace code and byte-identical ROM
SHA256 `a4fab9f9a4979e7782ca8e1fce69fe3f6f92adec25342d36e14ad94d165a125d`.
This comparison is against the prior story-branch default build, not merged
master (`10872fc3...`): the earlier shared Pak identity-validation refactor
at `be663c0` already changed normal RL code versus master and remains part of
the final normal-RL review/gate scope.
Neither story nor its harness consumes that trace. Both native-relic and
relic-cadence acceptance harnesses reject a story-probe map before starting
an emulator; the normal host relic test remains unchanged and passing.
The story ELF contains no `gRlRelicTrace` symbol/import. Pool start moves
from `0x8011ED00` to `0x8011CCA0`, recovering 8,288 bytes including coupled
code/alignment changes; cold RAM/preflight and exact canonical/native assets
are unchanged. This measured recovery exceeds the observed 2,288-byte
PRS shortage but does not prove later allocations or six-party peaks.
The required-resource NULL guard remains active; another independently
reviewed, explicitly authorized native run must prove the asset actually loads.

Exact `68bb3dac06721b99f50f297dbe52d6af8ad1770f` subsequently passed the
authorized cap-4 `create` -> `interrupt` sequence with unchanged assertions.
Results, memory and exact-head/artifact provenance remain separately in
`.local/rltest/story-residency-68bb3da/`. Live entry recorded 100 allocation
attempts and zero failures, with the required animation NULL guard clear;
two ticks, one logic/presentation step, HP 22 and DVs `0x8889` were observed.
Fragment `0xe9` was absent before loading, mapped at `0x8012D100` with size
173,760 during the cold call, and directly absent (both words zero) afterward.
Cold pool accessor values were equal at 2,960,800 before/after unload.
Raw minimum zero was attributed to a successful full-payload geometry
reservation at `func_80002D10`, not a failed allocation.

Create's precommand/postcommand written margins were both 260 bytes; the
live-interrupt margins were both 1,340 bytes, with no newly written bytes in
either untouched bottom region. Thus those full-run written peaks predate
the commands; this does not establish unwritten reserved-SP bounds or future
callback/suballocator/six-party safety. The sampled live accessor was
195,504 bytes, not a worst-case bound. The slot was released process-free.
After a separate grant, fresh-process `interrupted_reload` on the same frozen
head restored ROUTE/interrupted=1, L6/XP235/HP22/DVs `0x8889` and completed CRC
1,972,200,246 without storage mutation or KO/logic/presentation replay.
Both direct map words cleared, cold pool values balanced at 2,960,800, and
55 allocation attempts had zero failures. Written pre/post margins were
1,340 with no new bottom writes. The slot was released process-free.
This run did **not** directly decode PP/status/moves: storage identity and
scalar mailbox observations are not that proof. Its evidence remains
immutable; KO/learning, Transform, six-party and full gate acceptance remain
outstanding.

#### Discriminating checkpoint decoder (source/host only)

The opt-in probe has a separate command 5 / `SC_COLD_CHECKPOINT`, not a change
to command 3's live-battle fixture. It writes an interrupted ROUTE snapshot
through the existing SC01 codec/journal/Pak backend: Bulbasaur L6/XP235,
DVs `0x8889`, HP17/22, nonfainted, status4, moves `[33,45,0,0]`,
PP `[7,40,0,0]` and max PP `[35,40,0,0]`. It never starts native battle.
The host scheduling regression clears resident state after writing, then
loads through the actual save codec and asserts these restored values.

Explicit `checkpoint` -> fresh-process `checkpoint_reload` harness phases
capture resident memory using target-compiled party/count offsets and
`sizeof(RlMon)`, reusing the existing big-endian canonical `Mon` ABI decoder.
They persist result/memory JSON before party assertions, plus raw state
and decoded party JSON. Every named mon field is recorded; equality excludes
undefined struct padding (raw state is diagnostic only). Reload asserts the
decoded discriminating fixture, equality with the written checkpoint party,
interrupted flag, completed CRC, no storage change and no battle accounting.
Helper tests reject full PP/zero status and altered resident PP, ignore
padding differences, and reject out-of-bounds party decoding.

After independent source/host clearance and a separate exclusive cap-4 grant,
exact `6e467fc0b707894abe73635eb4364098180feb17` passed `checkpoint` ->
fresh-process `checkpoint_reload`. The validator independently redecoded both
644-byte resident snapshots: all named mon fields match the party JSON and
each other, including HP17/22, PP7/40, status4, moves/max PP, stats and nickname.
Reload restored interrupted=1 and completed CRC 1,972,200,246 without storage
mutation or KO/logic/presentation replay. Each phase recorded 55 allocation
attempts and zero failures; cold pool values balanced at 2,960,752, the loaded
173,808-byte fragment cleared both direct map words, and written pre/post
margins were 1,340 with no new bottom writes. This is not exhaustive SP safety.

Evidence is separate in `.local/rltest/story-checkpoint-6e467fc/`, with frozen
artifacts in `.local/rltest/story-checkpoint-decoder/`. Source-save provenance
records the unchanged `68bb3da` input SHA256
`ca483ab3a0e5d562db80422cf0ab62023f683ab258f5e57198a9ed53f1c726d2`
and new synthetic checkpoint SHA256
`273bdf8034c670b3ae7eb96e801f83a881b0fdb1e4c28eb3938925b9eb33d241`.
The slot was released process-free. Acceptance is **synthetic checkpoint
restoration only**, not preservation of PP/status through the native
live-party bridge, KO/learning, six-party or full gate acceptance. Existing
`68bb3da` live/reload evidence retains its narrower limitations.

#### Bridge-only initializer/carry instrumentation (pre-native review)

Command 6 uses a separate synthetic canonical lab fixture: HP17/22, PP7/40,
L6/XP235/DVs `0x8889` and paralysis status `0x40`. Commands 3 and 5 remain
unchanged. A probe-only, pointer-free 496-byte diagnostic holds four value
views (initial/carry original-party and live-mon) plus enabled/action/boundary/
error words. Its target-compiled size is checked independently of the unchanged
212-byte mailbox.

At the first frame gate, **before `publish_live` or any tick/XP/action**, the
bridge-only path captures the actual native original-party and live mon.
It requires canonical HP/PP/status/moves/level/DVs and stats, neutral stages,
original XP and live paralysis Speed 2 from canonical base Speed 11. A mismatch
logs explicitly, marks the probe invalid and renders only; no corrective
publication can hide the failed initializer. Boundary 1 holds without native
logic/presentation accounting, observation or awards.

The separately labeled injected-carry action 1 changes only original/live
HP to 16, PP slot 0 to 6 and status to `0x48` (poison plus retained paralysis).
It captures both native views and invokes the existing
`sc_native_observe_combat` once with the still-living enemy. Boundary 2
requires matching resident persistence, unchanged canonical stats/stages,
XP235 and no KO/learning. Action replay, unknown actions, invalid count/index
or initializer mismatches fail explicitly. All subsequent held frames render
without further observation. The host regression exercises both boundaries,
unchanged durable prebattle data, initializer mismatch, invalid/repeated action
rejection, invalid party count, native HP mismatch, unmodified paralysis Speed
and normal command-3 behavior.
Error codes distinguish input bounds (1), initializer comparison (2),
invalid/repeated action (3) and carry observation/comparison (4).

Explicit `bridge_init` and `bridge_carry` phases use separate fresh processes.
They preserve prebattle save bytes and initial native/resident/result/memory
snapshots before assertions, then hold 60 frames and require no native
logic/presentation/tick/KO/storage advancement. Injected carry may alter only
resident HP/PP/status; all other decoded named fields must remain identical.
The existing strict resource/allocation/map/pool/written-stack checks remain.
These phases stop while BATL is still active; they do **not** complete a battle,
exercise natural poisoning/PP usage, KO/learning or postbattle commit.
No emulator run has been authorized for this delta.

The source-stage pool starts at `0x8011D670`, 2,464 bytes above the `6e467fc`
checkpoint stage, including the 496-byte value diagnostic and code/alignment.
Cold RAM is 173,872 bytes (64-byte growth). The target tick frame stays at
112 bytes; poll grows from 120 to 128, and the bridge hold has a 64-byte frame
before its existing native/combat observation callees. These are static
measurements, not a whole-stack or required-allocation safety claim; the
strict bounded run must remeasure both.

Result lifetime source review: `func_8002B700` returns a slot of resident
`D_800AE540.unk_0004`, and `func_8002B888` copies records into its inline
`unk_01C` party. `rl_start_battle` reads those resident records after BATL/RLBT
pop into resident `gRlLastBattleResult`; it does not read freed live-mon views.
The new diagnostics copy values while the side/live pointers are valid and
retain no such pointer. Final native-result versus story-resident carry
comparison and synthetic/natural KO-learning execution remain a later,
separately reviewed milestone.

## Verification scope and remaining review

`story-test` checks source inventory/overrides/rivals/statics, parser errors and
cache tampering, every species at levels 1..100, all 65,536 stat-experience
values under eight asymmetric DV fixtures, 3,000 deterministic starter cases,
synthetic gate/retirement transactions, replacement cancellation, unrestricted
transfer, participant/rounding/learning behavior, healing, loss and evolution.
The C suite runs strict C89, ASan/UBSan and IDO 7.1 compilation. Generated data
also participates in strict C89 compilation.

The C suite now also activates the accepted 383-node canonical region graph,
fully consumes all nodes and separately traverses minimally to Champion for
both versions, all rival branches and both fossils.
It checks wrong-warp rejection, optional ship retirement, fossil
locking and reusable healing. Save tests exercise every byte's corruption,
graph/version mismatch, malformed state, torn writes at all three commit
steps, checkpoint replay and persisted terminal defeat. KO bridge tests prove
learning pause, duplicate rejection and save-error retry without mutation.
The actual native record layout has a guarded 32-bit host stat/copy/KO probe
and target IDO compilation. It tests compacted party-index mapping, a level
change, pause before repeat observation, learned-move publication and canonical
stat preservation. Assertions remain active despite the decomp headers'
disabled `assert` macro.
**There is no claim of native KO timing, physical Pak durability, campaign
balance or emulator-proven residency yet.** No emulator runs were made while another owner
held the exclusive runtime slot. The full `scripts/rl-gate.sh` is still required before
the complete story feature can be declared finished; it must not be weakened
or described as passed based on host-only tests.
