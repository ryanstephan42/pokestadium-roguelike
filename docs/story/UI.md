# Story campaign presentation unit

`src/story/sc_ui.c` is a pure-C controller using the tracked canonical graph,
progression, bridge and isolated save journal. `src/story/sc_scene.c`
renders it using the existing shared UI, species/move names, icons and HP/PP
detail panel. **Neither file is wired into a ROM profile yet.** This unit does
not establish a playable/native campaign acceptance claim.

The controller implements creation/continue, locked version/starter/healing
options, seed selection, region and finite encounter lists, nonbattle story
actions, healing sites, six-member party inspection, confirmed recruitment and
replacement, donor move transfer, pending learning, level evolution, save retry,
defeat/interruption and Champion terminal screens. Optional trainers remain
optional unless the reviewed graph makes them a gate. Rival and fossil variants
stay locked. Ship departure warns before passing the graph's explicit dock warp.
Champion completion is explicit so optional postgame encounters remain reachable
before ending the campaign.

Creation confirms replacement of an existing campaign save. Every serializable
mutation is journaled before the committed UI state changes. Failed saves retain
the exact pending choice and require retry with the original Pak; they do not
silently discard a reward, heal or evolution. Battle start uses
`sc_bridge_begin`'s interrupted checkpoint before issuing a request. Continue
of that checkpoint terminates the interrupted campaign instead of granting a
battle retry. Learning during a battle stays inside the paused bridge transaction
and is not passed to the route-only codec. Finish applies `sc_finish` to a
pending snapshot and journals it with the same commit-before-publish semantics
as `sc_bridge_commit`, retaining that snapshot for I/O retry.

## Integration contract

- Supply a validated, reviewed `ScCatalog`, a real `ScSaveDevice`, a seed and
  admitted storage for `ScUi` to `sc_ui_init`. Keep `ScUi` and its referenced
  catalog/device alive across battle swaps. It contains its own
  `ScState`/`ScBridge`, pending transaction, list strings and selection state;
  **do not allocate it as a large automatic stack object**.
- Initialize shared UI resources in the owning display-list/pool scope before
  `sc_scene_draw`. Pass controller button presses to `sc_ui_input`. It returns
  `SC_UI_STAY`, `SC_UI_EXIT`, or `SC_UI_REQUEST`; an exit asks the caller to
  return to its mode menu, not to destroy an unresolved save transaction.
- On `SC_UI_REQUEST`, copy `ScUiBattleRequest` with `sc_ui_battle_request`.
  The request contains six-party/team values and copied `ScCombat` progression
  values, encounter/variant, counts and a controller-lifetime `operation_id`.
  It has **no native pointers, allocator generations, AI selectors or loader
  authority**. Keep only one outstanding battle per controller lifetime.
- Return snapshots through `sc_ui_observe(ui, operation_id, party, active_slot,
  enemy_slot, enemy_hp)`. Pause native progression when the state is
  `SC_LEARNING`; present its replace/decline choice until state returns to
  `SC_BATTLE`. Deliver each KO once, as required by the existing bridge.
- Call `sc_ui_finish(ui, operation_id, outcome)` only after the final persistent
  snapshot. A win requires all enemy KO transactions to have settled; any loss
  or draw terminates the campaign. Old operation IDs and duplicate finish
  delivery cannot mutate a later battle. `SC_PENDING` with a save-error screen
  means retain the UI transaction and let the user retry; do not run another
  battle. Never deliver a callback to a destroyed/reinitialized controller.

The native service owner retains battle image, allocator, loader and AI
ownership. Shared state must be placed in reviewed backing **after Expansion
Pak residency admission**, not merely allocated above 4 MiB optimistically.
Normal roguelike and vanilla profiles are unchanged and must remain 4 MiB.
The menu entry/dispatcher, scene resource lifetime, story-only build profile
and reviewed native battle-service call remain integration dependencies.
No uncommitted service prototypes were copied into this unit.

Stone/trade evolution is safely disabled until the story owner supplies
reviewed campaign-derived permission authority. This is an **unresolved
integration requirement, not a permanent scope exclusion**; the UI must not
invent permissions or expose unrestricted toggles to `sc_evolve`.
No PC, repeatable wild grinding, shops, NPC trades, Game Corner or in-battle
items are introduced.

## Source/host verification

```sh
python3 tools/story_data.py --fetch  # only if authenticated local cache is absent
make -C host_tests story-ui-test
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
