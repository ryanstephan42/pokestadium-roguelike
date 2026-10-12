# Story `.fragmentSC` whole-closure stack budget (static)

Static ELF analysis only. No ROM/emulator/native/gate. Candidate C =
`.local/rltest/story-service-ui-native-layout-v1/pokestadium-rl.elf` (sha256 `3b01dccb…afa6`),
section-qualified (`.fragmentSC` overlaps the AI section VMA). Supersedes nothing; complements
`GEOMETRY-STACK-SUPPLEMENT.md` (bff5547 v2, accepted) and the UI root seal
`ryanstephan42-super-giggle/build/story/ui-callback-roots-8fdce7a/`
(SHA256SUMS digest `89adb151046c65e4732c1b3e9c3d998906371789ac50854ba62e114943d9e82e`).

## Budget

Game_Thread stack `D_80080000[0x18E0]` = **6368** bytes (`src/main.c:16,41`). All campaign entry
points run on Game_Thread. Depths are per-thread single-chain upper bounds (frame = largest
`addiu sp,sp,-N`; libultra exception context is saved in the OSThread, not on the stack —
assumption). Alternate call paths are maxed, never summed.

## Indirect resolution (resolve-SC-C.txt = resolve-C-v2.txt + the entries below)

Every `jalr` reachable from `sc_cold_entry@.fragmentSC` is resolved or gated; **0 INDIRECT /
0 UNRESOLVED / 0 CROSS / 0 MISSING** over 582 functions.

| Site(s) | Domain | Evidence |
|---|---|---|
| owner `calls.*` (loop_open/step/close, anon_8f909ef0, anon_8f90a350, display_retire ×2) | 7 statics 8f90e47c…8f90e780 | sc_native_campaign.c:108-110 sole construction; only `lui/addiu` SC materializations |
| UI driver launch/draw/leave (anon_8f909018, sc_ui_driver_step ×2) | 8f909f38 / 8f909f6c / 8f909f98 | sc_native_loop.c:75 sole bind; = UI seal 89adb151 three bindings |
| storage `device->open/read/write` (anon_8f904f7c, anon_8f904fdc, sc_storage_store ×5) | native_open 8007c57c, native_read 8007c428, native_write 8007c448 | sc_native_pak.c:183-185 (stores at 8007c9d4-dc) sole device |
| `record()` trace (anon_8f90e9f8) | **gated NULL** | story_six_fixture.c:11; sole chain sc_cold.c:59 passes 0 (`move a2,zero` 8f905a10) → s6 → a0; `beqz a0` |
| retirement->validate (anon_8007e4d4, anon_8007e7a0) | retirement_validate 8007b04c | only store sc_native_retirement.c:57; sole acquire call 8007b420/28 |
| finalize retire_backing | 8007b500 | sole call 8007b714/18 |
| memory_release retired/unpublished | 8007b4ac / 8007b578 | sole calls 8007b534/48, 8007b6d4/e0 |
| crash_restore validate | crash_validate 8007b2c8 | sole call 8007b474/7c |
| main_pool_pop_state | 13 destructors | prior accepted classification |
| func_80029008 | fragment32 811007f8 | only SC-reachable caller func_8002B274 (Transfer Pak removal, `func_8000B330` via Cont_ReadInputs) |
| func_800078D4 | fragment32 811007ac | s0 = `lui 0x8110/addiu 1964` @811008d8/dc; only SC-reachable caller func_811007F8 |
| sc_persist_native_enter | sc_cold_entry | sc_persist_native.c:105 image = loaded fragmentSC, header `j sc_cold_entry` |

Tool correction: anonymous-function extents previously ended only at the next symbol or `jal`
target, so `retired`@8007b4ac overran into materialized-only `retire_backing`@8007b500 and
produced a false `memory_release` recursion. `stackdepth.py` now bounds anon extents at every
resolved address token. The accepted bff5547 v2 outputs (geom/effdepth, C and O) reproduce
byte-identically with the corrected tool.

## Recursion

One SCC: `func_800367A0`→`func_800479C0`→`func_8004E810` (vanilla audio, reached via the
Transfer Pak error path). Both back-edges in `func_800479C0` sit under `arg2==0||arg2==2`
(485C0.c:347-357); `func_800367A0` only calls `func_800479C0(0xA6,1,n)` (373A0.c:403-423),
and 0xA6 forces `arg2=0xFF` (485C0.c:185-187), so the cycle cannot close but `func_800479C0`
may appear twice. Argument-feasible bounds valid for any entry: 800367A0 ≤ 344,
8004E810 ≤ 392, 800479C0 ≤ 432 (tool cycle-cut gave 392 for 800367A0; superseded).

## Results (beneath each entry, inclusive of its frame)

| Entry | Depth |
|---|---|
| sc_cold_entry (SC whole closure) | **5632** |
| sc_native_display_begin (Transfer Pak branch) | 1568 |
| sc_campaign_state (resident close path) | 5816 |
| sc_campaign_open / step / close / observe | 5768 / 5768 / 5776 / 5784 |
| sc_campaign_battle_gate / after | 5960 |
| **func_84300E88@.fragment62 (battle hook)** | **6000** |
| sc_campaign_battle_paused / menu_request / menu_hook / menu_hint | 24 / 48 / 48 / 832 |

Worst chain (all entries): … `sc_campaign_dispatch` 88 → `sc_native_loop_step` 64 →
`sc_native_ui_input` 32 → `sc_ui_driver_step` 40 → `sc_ui_input` 720 → anon_8f90726c 64 →
`sc_bridge_begin` 1344 → `sc_storage_store` 64 → anon_8f904fdc 88 → `sc_save_decode` 720 →
`sc_state_valid` 1384 → `sc_new` 728 → `sc_make_mon` 152 → `sc_recalculate` 56.

Concrete Game_Thread prefix above the battle hook (frames): Game_Thread 32 → func_80029BC0 80
→ func_80029008 32 → func_84301A2C 40 (fragment62 header `j`) → func_800078D4 88 →
func_843010EC 32 → func_84300FBC 32 = **336**. func_843010EC is materialized only at 843020bc
(other `addiu …,4552` hits pair with different `lui`). **336 + 6000 = 6336 of 6368: static
margin 32 bytes.** Resident-close path: 32 + 5816 = 5848 (margin 520).

## Path dependence (not a feasibility proof)

During battle, `tick()` (sc_campaign_run.c:71) calls `sc_campaign_step` via `step_consumed`;
`sc_native_loop_step` takes `sc_native_ui_input` unless the held-presented/LEARNING draw
conditions hold (sc_native_loop.c:173-176). Excluding `sc_bridge_begin` still leaves 4704
beneath the hook (save store via anon_8f907118); excluding storage too leaves 3344
(evolve via anon_8f90667c 896 → sc_evolve_owned 720). Proving which UI phases are reachable
inside a battle frame needs a state-machine invariant argument not attempted here.

## Not claimed

No overflow is demonstrated and none is excluded: the path-insensitive bound leaves 32 bytes,
below any reasonable safety margin. Not covered: other fragment62/57 hook callers beyond the
shown prefix as a maximum (the 336 prefix is a concrete chain, a lower bound on the worst
prefix); interrupt/exception stack use; the geometry residuals (BD04 asset entry, `unk_10`
callback recursion) which are on separate paths. No fit/native/gate claim.
