# Story campaign presentation unit

`src/story/sc_ui.c` is a pure-C controller using the tracked canonical graph,
progression, bridge and isolated save journal. `src/story/sc_scene.c`
renders it using the existing shared UI, species/move names, icons and HP/PP
detail panel. The controller and renderer are compiled into the guarded
candidate path, but this unit does not establish a playable/native campaign
acceptance claim. Sequential runner wiring exists in that path, but its full
lifecycle has not been accepted by native/emulator validation.

The controller implements creation/continue, locked version/starter/healing
options, seed selection, region and finite encounter lists, nonbattle story
actions, healing sites, six-member party inspection, confirmed recruitment and
replacement, donor move transfer, pending learning, level/Moon evolution, save retry,
defeat/interruption and Champion terminal screens. Optional trainers remain
optional unless the reviewed graph makes them a gate. Rival and fossil variants
stay locked. Ship departure warns before passing the graph's explicit dock warp.
Champion completion is explicit so optional postgame encounters remain reachable
before ending the campaign.

Creation confirms replacement of an existing campaign save. Every serializable
mutation is journaled before the committed UI state changes. Failed saves retain
the exact pending choice and require retry with the original Pak; they do not
silently discard a reward, heal or evolution. `sc_bridge_begin` journals the
pre-battle route checkpoint with `interrupted=1` before issuing a request.
Continue of that checkpoint terminates the interrupted campaign instead of
granting a battle retry. **Quitting or resetting after starting a battle counts
as a loss**, by explicit user decision: unfinished battles reload as terminal
to prevent save-scumming, with the prebattle party/progression unchanged.
In sequential battles, KO-time participants, eligible
recipients, operation identity and encounter identity are captured without
changing progression. A win applies XP, stat experience, level gains, learning
and level-up evolution in KO order after the battle; a loss discards the ledger
and leaves the interrupted checkpoint as the persisted run-ending result.
Combat stats therefore remain fixed for the entire battle. The resolved win
applies `sc_finish` to a pending snapshot and journals it with the same
commit-before-publish semantics as `sc_bridge_commit`, retaining that snapshot
for I/O retry.

## Integration contract

- Supply a validated, reviewed `ScCatalog`, a real `ScSaveDevice`, a seed and
  admitted storage for `ScUi` to `sc_ui_init`. `ScUi` contains its own
  `ScState`/`ScBridge`, pending transaction, list strings and selection state;
  **do not allocate it as a large automatic stack object**.
- Initialize shared UI resources in the owning display-list/pool scope before
  `sc_scene_draw`. Pass controller button presses to `sc_ui_input`. It returns
  `SC_UI_STAY`, `SC_UI_EXIT`, or `SC_UI_REQUEST`; an exit asks the caller to
  return to its mode menu, not to destroy an unresolved save transaction.
- On `SC_UI_REQUEST`, copy `ScUiBattleRequest` with `sc_ui_battle_request`,
  copy compact campaign state and bridge/ledger values to resident control,
  detach the request, then fully close and retire the story shell before
  starting the standard battle. Preserve display close, task/DP retirement and
  memory release order; do not retain the story framebuffer or UI slab.
  The request contains six-party/team values and copied `ScCombat` progression
  values, encounter/variant, counts and a controller-lifetime `operation_id`.
  It has **no native pointers, allocator generations, AI selectors or loader
  authority**. Keep only one outstanding battle per controller lifetime.
- During battle, capture KO facts and final carry/result values only; do not
  mutate campaign XP, stat experience, moves, evolutions or currency. A winning
  result reopens the shell and resolves each KO's progression and resulting
  learning/evolution choices before the next KO event. A loss discards all
  captured KO progression and ends the run; only the pre-battle interrupted
  checkpoint is durable. Never deliver a callback to a destroyed/reinitialized
  controller.

The native service owner retains battle image, allocator, loader and AI
ownership. The sequential runner reacquires only separate BATL loader storage
for battle and releases it after loader unbind; campaign request/state/result
values remain resident outside the story UI shell. Shared state must be placed
in reviewed backing **after Expansion Pak residency admission**, not merely
allocated above 4 MiB optimistically. Normal roguelike and vanilla profiles
are unchanged and must remain 4 MiB. Source/host tests do not establish the
native close/reopen, save-device or emulator lifecycle; those remain acceptance
dependencies.

Moon evolution uses `sc_evolve_owned` and the finite campaign ledger, not caller
permission bits. Other stones are unavailable because their sourced acquisition
requires excluded shops; trade evolution requires an actual completed exchange,
which this campaign does not implement. These unsupported triggers require a
separate scope decision, not invented region/badge unlocks or UI toggles.
No PC, repeatable wild grinding, shops, NPC trades, Game Corner or in-battle
items are introduced.

## Source/host verification

```sh
python3 tools/story_data.py --fetch --moon-stones --out build/story/moon-data.json --c-out build/story/sc_moon_data.c
make -C host_tests story-ui-test
make -C host_tests story-stone-test
make -C host_tests story-core-test
bash host_tests/run_story_campaign_run.sh build/story/campaign-run-review
bash host_tests/run_story_campaign.sh build/story/campaign-installer-review
make -j2
make rl -j2
make -C host_tests test
```

The UI target runs real-catalog C89 fixtures, ASan/UBSan, IDO controller
compilation and IDO renderer compilation against real N64 headers. Renderer
tests replace platform drawing only; they exercise the
actual controller, renderer and shared mon-detail logic. Fixture battle outcomes
are **not native battle execution proof**. These commands provide partial
source/host/build coverage only. `scripts/rl-gate.sh` is unchanged and must print
`GATE PASSED` before full integration acceptance; its emulator executions were
not authorized for this source-only assignment.

The connected campaign runner target uses actual installer, persistent loader,
arena and KO capture code. Recorded loop/lifetime, display, launcher and result
UI seams check shell release/unmap before a separate aligned BATL lease, ordered
non-mutating KO capture, lease release before reopen, operation continuity and
failure/pending-retirement paths. The arena allocation includes 64 bytes of
alignment padding so its size remains a multiple of 16. This is HOST sequencing
coverage, not proof of native DP/task fences, physical heap fit or save-device
behavior. Both scripts require fresh output directories.

## Finite Moon Stone unit

These facts were checked against authenticated Git blobs at the same pinned
[`pret/pokered` revision](https://github.com/pret/pokered/tree/af519899719f0754965776faac0e836a3b906e6d)
used by the campaign importer. `--moon-stones` produces the independently
reviewed five-action extension; a missing review pin leaves its generated
catalog unreviewed, and only explicit host fixtures can exercise it before
acceptance. There is no CLI flag granting native review authority.

| Source fact | Pinned evidence |
|---|---|
| Three visible, finite Moon Stone pickups: Route 2, Mt. Moon 1F, Rocket Hideout B2F | `data/maps/objects/Route2.asm:21`, `MtMoon1F.asm:38`, `RocketHideoutB2F.asm:22` |
| Two hidden Moon Stones: Mt. Moon B2F and Pokemon Mansion 1F | `data/events/hidden_events.asm:351-353,452-453` |
| Hidden pickups give one item and set their obtained flag only after success | `engine/events/hidden_items.asm:4-12,26-35` |
| Fire, Thunder, Water and Leaf Stones are sold at Celadon Mart 4F | `data/items/marts.asm:28-29`; shops remain excluded |
| Stone use is refused in battle and consumes one stone only after evolution succeeds; cancel/no-effect does not consume it | `engine/items/item_effects.asm:760-791` |
| Trade evolution requires `LINK_STATE_TRADING` and the species' source level requirement, not a badge/region unlock | `engine/pokemon/evos_moves.asm:69-97` |
| Cable Club enters trading state and calls evolution after the completed exchange | `engine/link/cable_club.asm:273-277,845-851` |
| English Red/Blue NPC-trade evolution helper contains a historical name check; its original Graveler/Haunter trades were replaced in the final release | `engine/events/evolve_trade.asm:1-27`; NPC trades remain excluded |

The extension appends indices 383-387 without changing the 383 legacy nodes.
Mt. Moon pickups require Brock, Hideout requires the existing Game Corner
Rocket action, Mansion requires Surf, and Route 2 requires Cut. Route 2 object
coordinates and its numeric map/cut-block swaps are authenticated against the
pinned Git tree. The north corridor has a Cut block at walking-step origin
(14,22), and the item corridor has one at (12,52); no Diglett-Cave shortcut
bypassing Cut is granted. This is a reviewed **region-menu gate policy**, not a
complete original-game tile/path proof. No graphics/blockset data is downloaded.
The original review digest remains unchanged; the extension identity binds the
base digest, source facts/hashes and extended graph.

`ScState.moon_stones` and `moon_stones_spent` enforce
`owned + spent == completed pickup actions <= 5`. Both `sc_pickup_stone` and
generic `sc_action` execute the same typed one-time pickup/ledger transition.
`sc_evolve_owned` derives permissions internally and consumes exactly one on
successful Moon evolution; cancel, no-effect, invalid/refused actions and level
evolution do not consume a stone. The UI applies these to a pending state and
journals before publishing, retaining the exact pending state after failed I/O.

Codec v2 appends owned/spent at bytes 710-711 of the unchanged 736-byte record;
remaining reserved bytes must be zero. V1 decode accepts only the exact legacy
review identity, refuses pickup completion bits/nonzero reserved inventory,
and migrates with all existing state retained and zero fabricated pickup
history. `ScSnapshot.wire_version` lets Continue report the legacy format;
the next journal write uses v2/current graph identity. Unknown formats/identities
return `SC_SAVE_BAD_VERSION`, and invalid ledger/history returns `SC_SAVE_BAD_DATA`.
The unchanged journal still preserves the prior slot on a failed save.
The migration target generates a genuine v1 fixture from the accepted
`764fb69435fdcaa24e7627668b3b97d832b6d5ba` core/codec in ignored output, checks
its pinned SHA-256, and passes its unchanged bytes to the new reader. No ROM,
emulator savestate or binary save fixture is committed.

Trade evolution needs a separate actual completed-trade authority or an
explicitly approved adaptation. Species eligibility/level data is already in
`ScSpecies`; `trade_unlocked` is only a caller input to `sc_evolve`, not evidence
that an exchange happened. The current campaign has neither a link-trade
transaction nor a canonical milestone granting one. A synthetic "unlocked at
Saffron" or free self-trade menu would be an invented schedule. Until the owner
has a reviewed completed-trade callback/receipt (or an approved replacement
policy), `sc_evolve_owned` refuses trade methods. All four purchased stone types
and trade-trigger evolution remain **unsupported within the current scope**.
Do not widen the no-shop/no-NPC-trade scope or manufacture permissions.
