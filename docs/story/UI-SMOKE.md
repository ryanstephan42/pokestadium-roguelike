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
| Confirm trainer | `SC_UI_REQUEST`; interrupted route checkpoint already durable; copy encounter/variant/party/enemy/progression/operation ID |
| Native persistent callback | `sc_ui_observe(ui, id, snapshots, active, enemy, enemy_hp)`; actual HP/PP/status copied; stale IDs refused |
| KO with pending learning | `SC_UI_LEARN`; pause native progression; replace/decline; resume only when state returns to `SC_BATTLE` |
| Finish win | After final snapshot and all KO transactions, `sc_ui_finish(ui,id,SC_WIN)` journals pending result; `SC_UI_REWARD` |
| Recruit/transfer confirmation | No mutation on cancel; actual offered donor move only; full party replacement explicitly warns no PC |
| Save failure and retry | `SC_UI_SAVE_ERROR`; committed state unchanged, exact pending result retained; original Pak retry required |
| Save/menu/fresh Continue | Decoded full party, options, completion/retirement ledger and pending reward retained; v1 upgrade message when applicable |
| Native loss/draw or interrupted Continue | `SC_UI_TERMINAL`, `SC_LOST`; no heal/retry to resurrect campaign |
| Champion terminal | Completion available after Champion; explicit close preserves optional postgame until chosen; `SC_FINISHED` reload remains terminal |

Expected callback refusal: wrong operation ID, duplicate finish, invalid snapshot
values or unsupported evolution acquisition do not grant progression. `SC_PENDING`
means settle learning or retry journal according to screen, never reinterpret
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
