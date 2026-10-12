# Native presentation integration proposal (no native execution)

`host_tests/run_story_ui_flow.sh` exercises edge-button input sequences and
actual `sc_ui_input`/`sc_scene_draw`/shared mon-detail implementations with
host drawing callbacks. Battle callbacks are explicit fixtures, not a battle
simulator or native play proof. No emulator runs are included.

The native owner must first admit Expansion Pak backing and the reviewed
catalog, initialize a campaign-specific `ScSaveDevice` and shared UI resources,
then keep the current 43,472-byte `ScUi` backing across battle swaps. The copied
request is 1,560 bytes. These are layout measurements, not allocator admission
or complete call-chain stack proof. `sc_evolve_owned` adds a measured 720-byte
own frame; include nested calls in the service fit evidence.

Feed `gPlayer1Controller->buttonPressed`, not held buttons, to `sc_ui_input`.
The controller has no frame clock or repeat timer: one press or explicitly
delivered repeat pulse advances once. The host fixture verifies that holding
without a new edge does not advance; it does not replace the native controller
driver. B takes precedence over A, directional input takes precedence over A,
and page movement clamps rather than wrapping outside a list.

| Native smoke step | Screen/value assertions |
|---|---|
| Enter separate opt-in story mode | `SC_UI_MENU`; no change to normal roguelike/vanilla state or memory admission |
| New, edit version/starter/healing/seed | `SC_UI_CREATE`; six creation rows; options mutable only here; seed edge changes once |
| Cancel creation confirmation | Returns to creation; no Pak write/new campaign state |
| Confirm creation | Pending route snapshot writes to isolated story note; `SC_UI_REGIONS` only after successful journal |
| Party and member details | `SC_UI_PARTY`/`SC_UI_MON`; real species/move names, HP bar, all PP/status; six slots maximum, no PC |
| Select unlocked region/encounter | `SC_UI_ENCOUNTERS`; full selected canonical ID in panel, unavailable/retired nodes absent; rivals/fossils locked |
| Confirm trainer | `SC_UI_REQUEST`; interrupted route checkpoint already durable; copy encounter/variant/party/enemy/progression/operation ID, detach request, retire full UI/display shell before battle |
| Native battle callbacks | Capture active participants, KO recipients and final carry/result only; campaign XP, stat experience, moves and evolution stay unchanged during combat |
| Win resolution | Reopen the shell; apply each KO in order, pausing after each event for its level-up, learning and evolution choices before advancing |
| Loss/draw | Discard the KO ledger; preserve no XP/stat experience/learning/evolution from the lost battle; show terminal result backed by the interrupted prebattle checkpoint |
| Finish win | After ordered progression resolves, `sc_finish` journals the resulting reward state with `interrupted=0`; `SC_UI_REWARD` |
| Recruit/transfer confirmation | No mutation on cancel; actual offered donor move only; full party replacement explicitly warns no PC |
| Save failure and retry | `SC_UI_SAVE_ERROR`; committed state unchanged, exact pending result retained; original Pak retry required |
| Save/menu/fresh Continue | Decoded full party, options, completion/retirement ledger and pending reward retained; v1 upgrade message when applicable |
| Native loss/draw or interrupted Continue | `SC_UI_TERMINAL`, `SC_LOST`; no heal/retry to resurrect campaign |
| Quit/reset after battle START, before result | Counts as a loss by user decision; fresh Continue is terminal with exact prebattle party/progression and no retry |
| Champion terminal | Completion available after Champion; explicit close preserves optional postgame until chosen; `SC_FINISHED` reload remains terminal |

Expected refusal: wrong operation/encounter/variant/party identity, duplicate KO
or invalid capture does not grant progression. `SC_PENDING` means resolve the
current ordered learning/evolution choice or retry the journal, never reinterpret
it as battle success. Service owns battle loader, AI, allocator/image generations
and native state; none appear in the UI request.

## Supported versus still unwired

The independent controller/renderer supports creation/Continue, regional
navigation, story actions/variant selection, all fifteen canon healing sites,
party inspection, reward/recruit/replacement, unrestricted actual donor move
transfer, learning, level/finite Moon evolution, save-error retry and terminal
screens. Broad state tests sweep both region graphs; flow tests prove ordered
button/presentation transitions. Renderer stubs do not prove actual font, icon,
graphics lifetime, screen visibility or controller repeat on hardware.

Still unimplemented native surfaces: region-mode menu entry and dispatcher,
admitted UI/catalog/code residency, scene/font/icon resource lifetime across
battle swaps, real Pak backend binding, approved native service launch and
pause/resume presentation callback scheduling. Thus there is **no visible
playable campaign acceptance yet**. Non-Moon shop stones and trade evolution
remain unavailable under excluded shops/no actual exchange; no synthetic
permission toggles are exposed. These integration tasks stay with their owners,
not duplicated by host test battle logic.

## Current six-party harness readiness (source/HOST only)

The current harness requires compiled 648-byte state, 652-byte snapshot and
648-byte precommit metadata, snapshot state/interrupted/wire offsets 0/648/649,
save version/size 2/736, the reviewed Moon catalog and v2 codec. Historical
644-byte/v1 evidence and its separate reload validator are not rewritten or
accepted as current proof. Combat/event/terminal/bridge/launcher storage remains
732/416/696/736/828 bytes. Shared combat budget remains 18,000 frames, excluding
only one verified 60-frame hold per unique learning generation; the 180-second
watchdog remains unchanged.

After a same-candidate guarded link, prepare the required package without
starting an emulator:

```sh
TMPDIR="$PWD/build/story/tmp" python3 -m tools.rltest.seal_story_current_six \
  --rom BUILD/pokestadium-rl.z64 --map BUILD/pokestadium-rl.map \
  --elf BUILD/pokestadium-rl.elf --catalog-object BUILD/story/sc_data.o
```

`BUILD/story/current-six` must not already exist. The sealer reads actual
ELF32/MSB/MIPS constants, checks map addresses and corresponding ROM load
bytes, reconstructs the complete catalog `.data`/`.rodata` from its object
(supported `R_MIPS_32` relocations only), then compares both linked ELF and ROM.
It generates fresh audited Moon data, real HOST six-party checkpoint/proof,
current codec and malformed-input refusal fixtures; an independent IDO compile
of that fresh catalog must reconstruct the same linked bytes. Unsupported
packing, missing metadata, stale catalogs or mutated candidates fail closed.
Header/LEO postprocessing is outside these catalog/constant checks.

The manifest seals the ROM/map/ELF, candidate catalog source/data/object,
codec/checkpoint/proof and explicit source set. The consumer checks the seal,
exact artifact/source set, workspace/build bounds, every hash and actual linked
catalog/ABI binding before starting the harness. Source or candidate changes
require a fresh package; neither old evidence nor an existing package is
overwritten. These checks establish current harness readiness, **not** allocator
rights, full memory fit, service retirement safety or native battle completion.

`python3 -m unittest host_tests.test_story_current_six_manifest
host_tests.test_story_probe` covers synthetic ELF relocation/load fixtures and
mutation refusals alongside existing ABI, full-party and budget tests.
`bash host_tests/run_story_current_reload.sh FRESH_OUTPUT` checks genuine current
HOST codec/648-byte terminal/precommit fixtures in normal and sanitizer modes.
The synthetic ELF fixtures are explicitly not a production ROM or native proof.
