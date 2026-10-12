# BATL model-loader buffers: section rights and revised relocation design

Status: DESIGN for review. This does not accept fit or grant native work. It
replaces the earlier "all five buffers in one DATA lease" proposal, which is
withdrawn because the two M buffers execute relocated code.

Inputs:
- Frozen audit `build/story/batl-buffer-audit-5ca01ba-v1` (source identical to `4bbe01c`).
- Metadata-only classifier `build/story/batl-loader-rights-v1/rights.py`. It
  gives the same results on the retail baserom and on candidate ROM `7ce046a0…`.

## Buffers (campaign BATL, `func_80019760(3)` x2 + `func_8001987C`)

| Buf | Size | Count | Allocation | Filled by | Executed? |
|-----|------|-------|------------|-----------|-----------|
| M (model pool) | 0x3C000 | 2 | `unk_04[0]` | worker `func_80019484` | **yes**: 8-insn entry stub, worker and main thread |
| A (animation pool) | 0x18000 | 2 | `unk_18` | worker `func_80019514` | no (CPU and RSP/RDP read it; see below) |
| S (staging) | 0x28000 | 1 | `unk_A04.unk_08` | worker DMA via `func_80003B30` | no |

Archive facts, checked over all entries:

- **Model archive** (0x920000):
  - 215 FRAGMENTs.
  - Relocations that touch text: R_MIPS_26 at +0x00 (the entry `j`) and HI16/LO16 at +0x2C/+0x30.
  - Every entry jumps to a stub at +0x20: `sw a1; bnez a0; move v1,0; lui; addiu; jr ra; move v0,v1; nop`. The stub ends at +0x40.
  - Everything after +0x40 is data: geo layouts, display lists and textures, with only R_MIPS_32 relocations.
- **Animation archive** (0x8CC000):
  - 63 FRAGMENTs; the only text relocation is R_MIPS_26 at +0x00.
  - The entry jumps to +0x20, which holds data (for example `0x03000030`). Executing it would be invalid.
  - The loader, `func_80019514`, uses `frag+0x20` as a data table and never
    calls the entry. The table reaches controller `unk_28` and then actor
    `unk_024` (1C720.c:229/237).

### A is not CPU-only (validator trace, exact 4bbe01c)

- The RCP reads A: `D_843920C0` feeds texture and display-list commands
  (e.g. fragment62_315D50.c:4831/4910, fragment62_31AA30.c:40/50).
- A's geo layouts carry 72 command-8 callback sites. `func_80017B60` passes
  each to `func_80010FDC` (11BA0.c:20-31), which converts the address with
  `Util_ConvertAddrToVirtAddr`, stores it in the node and **calls it at once**;
  later graph traversal may call it again. A's own entry is still never called.
- The 72 targets are encoded 0x81000028..0x81000160. Fragment id is
  `((addr & 0x0FF00000) >> 20) - 0x10` = 0 (memmap.c:216-229), so they resolve
  through `gFragments[0]`, not through A.

### Fragment-0 provenance and lifetime

- `gFragments[0]` is fragment31 (yaml vram 0x81000000). Campaign BATL loads it
  at fragment62_2EA8E0.c:621 (`FRAGMENT_LOAD(fragment31)`), directly after the
  loader allocations and before any geo layout is processed.
- Path: `func_80004454(0, …)` → `func_800043BC` (3FB0.c:519-547) →
  `Memmap_RelocateFragment(0, …)` → `Memmap_SetFragmentMap(0, frag, size)`
  (memmap.c:127). The block is a lower `main_pool` block inside the BATL state.
  Its destructor `func_80004364` (3FB0.c:504-517) clears the map only when the
  BATL state pops. No story code sets or clears id 0.
- 0x81000020..0x810001FF is `fragment31_unk_table`, a raw textbin of
  `j target; nop` thunks with retail R_MIPS_26 relocations. 0x28..0x160 are the
  callback thunks. 32 of them jump into **fragment62** (id 0x33) at retail
  offsets, e.g. 0x81000028 → `func_84343D24`; the rest stay inside fragment31.
- RL builds regenerate fragment62, so those retail targets move. They are
  repaired at load time, not at link time: `func_800043BC` (3FB0.c:531-539)
  calls `rl_relocate_archive_imports(0, frag)` (src/roguelike/rl_native_imports.c)
  before `Memmap_RelocateFragment`. It rewrites each fragment-0 R_MIPS_26 target
  in 0x8430xxxx..0x843Fxxxx through the generated exact-function table
  (`tools/rl/gen_archive_imports.py`, vanilla ELF `func_*` → linked symbol) and
  halts the loader thread on any unmapped target. All 32 retail targets are in
  the candidate table (2,055 rows). The linked address depends on the
  candidate layout; in the combined candidate `e051361`, 0x81000028:
  `func_84343D24` 0x84343D24 → 0x843448D4. The successor seal carries all 32
  rows, emulated from the linked table bytes. See docs/roguelike/BUILD.md,
  "Original animation archive imports". Static evidence only; no native witness.

### Graphics retirement of A and S

- Overwrite on each load keeps vanilla ordering, which does not depend on the
  buffer address. Model loads wait for `func_80001C90` (dp_intro.c:396; graphics
  idle) before `func_800198E4` (1C720.c:190-192, 1AB70.c:312/321/370). The
  animation reload `func_8001C248` → `func_80019A7C` (1C720.c:222-232) has no
  vanilla fence; that is unchanged, not added or claimed.
- Release is new, so it gets a fence: `sc_batl_loader_unbind` waits on the
  same `func_80001C90` idle predicate after the drained scope close and BATL
  pop, before the span can return to the campaign lease.
- Crash retention, per-chunk PI completion (worker waits per chunk; the PI
  consumers use `osVirtualToPhysical`, no lower-only mask) and cache handling
  are unchanged.

### Aliases into A and S after release (static trace, combined candidate)

Who can still hold an A or S address once `sc_batl_loader_unbind` returns the
span to the campaign lease:

| Alias | Storage | Written by | Read by | Ends |
|-------|---------|------------|---------|------|
| Controller `unk_18` (A base), `unk_28`/`unk_20` (last A table/size) | `func_80019760` block, lower `main_pool` (19840.c) | 19840.c `func_80019760`, `func_80019B48` case 2 | worker jobs from `func_80019A7C`; `func_8001C248`/`func_8001C2D8` (1C720.c:222-240) | The block is freed by the BATL pop. Its only owner, `D_8438E798[i].unk_10` (fragment62_2EA8E0.c:602-603), is fragment62 BSS. That BSS stays mapped, holding a dangling pointer, until the RLBT pop; no code reads it after fragment62 returns. |
| `D_843924E0[]` (A table pointers) | fragment62 BSS | worker `func_80019514` through `func_80019A7C(…, D_843924C0, D_843924E0)` (`func_8432C958`) | `func_8432CA48` (fragment62_315D50.c:4908-4920) | fragment62 is loaded under the RLBT state (rl_battle.c), so this BSS lives until the RLBT pop, not the BATL pop. No reader runs after fragment62 returns. |
| `D_843920C0[256]` (A and lower addresses) | fragment62 BSS (`fragment62.h:1036`) | `func_8432C830` from `func_80019EA0(0)` (a lower `func_8000484C` block) and from `func_8432CA48` (A) | fragment62 only: `func_8432C86C` clears it on entry; CPU/RCP users at 315D50.c:5023-5040 and 31AA30.c:11-50 | No reader outside fragment62. The BSS lives until the RLBT pop. |
| `D_800ABE10.unk_A04.unk_08` (S) | main BSS (19840.c:27) | `func_8001987C` | the worker (`func_80019484`/`func_80019514`) only | **Stays stale after the pop** (vanilla also leaves a freed lower pointer). Every other fragment that creates controllers (`func_80019760`) also calls `func_8001987C`, which rewrites it before any worker job. |
| `D_800ABE10.unk_A04.unk_00/04` (archive handles) | main BSS; the handles are lower `main_pool` blocks (`func_800044F4`) | `func_8001987C` | `func_80019204`, `func_8000484C` users | Same stale-after-pop behaviour as vanilla. The handles never point into A or S, and opener flag 4 (`arg3 == 1`, 3FB0.c:601-603) bypasses the per-file `unk_08` cache that `func_80019204` writes. |
| `gFragments[0xEF]` (last model or animation fragment; both archives are id 0xEF) | memmap.c:13 | `Memmap_RelocateFragment` from `func_80019128` (no destructor registered) | `Memmap_GetFragmentVaddr` for 0x8FFxxxxx and `func_80004364` (compare only) | **Stays stale after the pop**, as in vanilla (a freed lower M/A block). Every 0xEF load sets the entry before it relocates (memmap.c:127), and the shared id means deferred resolution would already be wrong in vanilla. No new reader is added. |
| `gFragments[0]` (fragment31 thunks, the 72 callback targets) | memmap.c:13 | `func_80004454(0, …)` at fragment62_2EA8E0.c:621 | `Util_ConvertAddrToVirtAddr` from `func_80010FDC` | Lower block; destructor `func_80004364` clears id 0 at the pop (3FB0.c:504-517). It never points into A. |

**Corrections (validator, `8fdce7a`).**
- fragment62 is loaded under the RLBT push in `rl_start_battle`, so its BSS dies at the RLBT pop, not at the BATL pop.
- `sc_batl_worker_admit` does not refuse jobs after a successful close: with `!scope.active` it returns `!scope.failed`.

Safety after close rests instead on the producer exclusion below.

**Producer exclusion: close → BATL pop → RLBT pop → unbind → re-entry.** This is a static trace over the linked candidate ELF `3b01dccb…aafa6`, using `.local/rltest/seal-tools/cfgreach.py` (conservative) and `producers.py` (reverse closure, informational).

1. *Nothing is outstanding at close.*
   - `func_800198E4` and `func_80019A7C` set the worker's `unk_01` bit 1 or 2 on the main thread when they enqueue.
   - Only `func_80019B48` clears it, also on the main thread. It runs after the worker thread has finished `func_80019484`/`func_80019514` and posted completion (19840.c:281-296).
   - The fast path at 19840.c:378 posts directly to the worker's own queue and never reaches the worker thread.
   - `sc_batl_scope_close` refuses unless both captured workers have `unk_01 & 3 == 0`.
   - So every job on those two workers has finished. The worker thread is idle only if no other controller has queued a job; item 2 bounds that case.
2. *No other controller exists in the story battle.*
   - The story rules have `show_intro = 0` (sc_campaign_run.c:169), so fragment63's `func_80019760(5)` controllers are never created.
   - fragment62's only controller sites are 2EA8E0.c:602-603, inside the armed window.
   - There is no `func_80019760` site in fragment34, `src/roguelike` or `src/story`.
3. *No producer is reachable after close.* These are the calls that run after close, in order:
   1. In the linked fragment62 entry (`func_84301A2C`), after `sc_batl_scope_close` at 0x843020F4: `rl_native_relics_end`, `func_800077B4(2)` (fill-rect blank frames, no frame callback), `func_800076C0`, `func_80005EAC`, then the BATL `main_pool_pop_state`.
   2. `rl_start_battle` then runs `sc_runtime_probe_terminal` and the RLBT pop. In the canonical path (`!gScProbeActive`) the pop comes first, before the hook `drain` (`hook_drain` → `sc_native_terminal_drain`), `rl_from_native` and `sc_native_return_identity`. In the probe-active branch the RLBT pop is deferred. Both orders run the same callees.
   3. `sc_campaign_run.c:191` calls `sc_batl_loader_unbind`.

   Conservative check (`cfgreach.py`, validator review of the earlier jal-only graph). The traversal works per instruction, keyed by (section, address), so `.fragmentSC` and `.fragmentAI` at 0x8F900000 are never conflated:
   - Every conditional or branch-likely edge follows both the target and the fallthrough. Calls continue at the return address.
   - `jr ra` ends a path. Delay slots are checked, and no control transfer sits in one on this path.
   - A target outside the caller's section resolves to every code section containing it.
   - Roots: `.fragment62:0x843020F4` (the close call), `.text:0x80067D24` (the `rl_start_battle` return from fragment62, covering both the probe and canonical orders) and `.text:0x80078964`. Traversal stops at `.text:0x80078978`, after the `sc_batl_loader_unbind` call and its delay slot.
   - Every indirect site on the path is resolved explicitly (`resolved.txt`):
     - the four destructor `jalr` sites, resolved to the 13 registered destructors;
     - the hook `drain` and `abort` sites, pinned to `.fragmentSC`;
     - the 52-entry `__rla_199` jump table;
     - nine `_Printf` `prout` sites, resolved to the four sinks that the seven `_Printf` call sites pass as literals.
   - Result: 9,165 instructions in `.text`, `.fragment62` and `.fragmentSC`. There are no producer hits, no unresolved indirect site, and no producer entry is reached.

   The reverse closure of the producers has two non-fragment roots:
   - `func_8001AD90` has no caller in source or the ELF.
   - `func_80031390` is called directly at 30640.c:694. Its only "data" word is an asset byte pattern in `.late_assets_6`.

   Neither changes the forward result. An auxiliary scan from the resident thread entries also found no producer, but 28 of its indirect sites (audio, RSP, scheduler, model stub) are unresolved, so it is not exhaustive.
4. *Re-entry.*
   - The next `bind`/`arm` requires `!live`.
   - The next producer can only use the controllers created at 2EA8E0.c:602-603.
   - `func_8001987C` (604) rewrites `unk_A04.{00,04,08}` before the first producer runs (702).
   - So the stale fragment62 BSS and `unk_A04` words are overwritten before anything reads them.

Between close and the RLBT pop, the stale fragment62 BSS pointers therefore have no reader. The stale main-BSS words have no consumer until the next armed window rewrites them. `unbind` waits for graphics idle (`func_80001C90`) before release.

Bounds:
- This is a static, conservative control-flow graph of the main-thread story launch path from close to unbind. Concurrent threads are bounded only by the non-exhaustive auxiliary scan.
- It does not cover controllers created by other game states that survive into the battle.
- There is no VI or native witness.

## Per-load phases of one M buffer (worker thread, priority 10)

1. **Pool reinit.** `func_80002DA4` writes the header at [base, base+0x10).
2. **DMA.** `func_80003B30` loads the compressed bytes into S (PI DMA into S only; M is not touched).
3. **Decompress.** The CPU writes the fragment at `base+0x10` (`func_80002DCC` with alignment 0x10).
4. **Relocate.** `func_80019128`:
   - RL import patch, then `osWritebackDCache(frag, relocOffset)`.
   - `Memmap_RelocateFragment`: `osInvalICache` and `osInvalDCache` over `sizeInRam`, CPU relocation writes (including text +0x00/+0x2C/+0x30), `bzero` of the reloc tail, then `osWritebackDCache`.
5. **Execute (worker).** `func_80019328` calls `frag(0,0)` in the worker
   thread. It returns a header pointer, then sets node 0xE `unk_28 = frag`.
6. **Data phase.** Still in the worker:
   - `process_geo_layout` allocates nodes in the same pool.
   - `func_80018C40` recolours textures (writes after execution, at addresses ≥ frag+0x40).
   - Then the main thread and RSP consume geo, DL and texture data.
   - **Correction (validator, exact 4bbe01c):** the stub is fetched again for
     the whole residency. `unk_28` is a typed function in the 0xE node
     (global.h:447-456). It is called by `func_8001BD04` and `func_8001BD9C`
     (1C720.c:71-89, `unk_28(0,0)`) and by `func_8001BE90` (1C720.c:111-124),
     reached from BATL at fragment62_2EC3C0.c:661/664 and 2F23A0.c:472/510.
     The earlier claim that it runs once, only in the worker, is withdrawn.
7. **Retire.** The next load into the same buffer repeats from step 1.
   - The stub's stale ICache lines are invalidated at step 4 before the next execution.
   - Every model fragment uses archive id 0xEF, so `Memmap_SetFragmentMap(0xEF)` is overwritten on each load, as in vanilla.

So each M buffer cycles through mutable → executable for the whole residency,
with main-thread fetches → mutable on the next load, many times per battle. The
existing executable-image authority (sc_ma staging → `sc_ma_seal` W^X spans →
`sc_ma_module`) is publish-once. It has no rewrite cycle, and using it from
the worker thread would mean per-load arena allocation, sealing and release.
Reusing it for M would therefore need a new rights-transition mechanism. That
is out of scope: there is no blanket EXEC DATA slab and no weakening of arena
rights.

## Revised proposal (needs coordinator approval)

**Relocate only the genuinely DATA-only buffers, A, A and S, into the campaign
DATA lease. Leave both M buffers in the lower pool, exactly as in vanilla.**

- The upper DATA lease grows by 0x58000 = 360,448 bytes, not 851,968.
- It is mapped READ|WRITE only and never EXEC. The CPU writes and reads it; RSP/RDP
  and PI reach it through `osVirtualToPhysical` or `addr & 0x1FFFFFFF`, as the
  audit established.
- Lower relief is 360,496 bytes charged (raw + 16 per block, three blocks).
  M stays charged in lower at 491,552.
- The shim keeps vanilla allocation order M,A,M,A,S:
  - M calls always go to `main_pool_alloc(size, 1)`, even when armed, but still
    advance and check the sequence.
  - A and S calls take the next fixed offset in the bound span (alignment 64).
  - A mismatch in size or order fail-stops through `sc_batl_scope_require(0)`.
  - It arms and disarms only around the two `func_80019760(3)` calls and
    `func_8001987C` in campaign BATL (`gScCampaignBattle`). Probe and ordinary
    RL never arm it.
- Lifetime is unchanged from the frozen audit:
  - Bind is done in `launch()` before `rl_start_battle`; unbind right after it.
  - Release returns `SC_PENDING` while the span is live. The span becomes free
    only after `sc_batl_scope_close` sees both workers drained (`unk_01 & 3 == 0`)
    and the BATL pop.
  - Disarm records both controllers and the next scope generation. The next
    `sc_batl_scope_open` must capture exactly those workers in that
    generation, otherwise it refuses. Only that scope's drained close marks the
    spans drained. Unbind refuses unless they are drained and there is no
    active, ticketed or failed scope, then waits for graphics idle.
    `launch()` fail-stops if bind or unbind is refused.
  - The lower destructor never sees upper spans, because upper blocks are not
    `main_pool` blocks.

### Lower fit arithmetic (conditional; the validator owns the ledger)

| Item | Bytes |
|------|------:|
| Zero-UI staging deficit (validator late ledger, baseline 66,992) | 59,344 |
| Story display, still lower | 131,296 |
| UI lower residual (record + scope) | 128 |
| **Need before the validator's `+3216k` term and geometry** | **190,768** |
| A,A,S relief | 360,496 |
| **Margin before the `+3216k` term** | **169,728** |

- The fixed animation0 peak (2,955,712) happens while decompressing into A
  from S. Both are now upper, so that peak also drops by the A/S charge.
- This is not a fit claim:
  - Geometry and stack closure remain open (6,344 of 6,352, with trainer 0x9A,
    update binding and fragment34 callbacks unresolved).
  - Contiguity and DYN side checks need the remeasurement seal.

### Fallback if the margin later proves insufficient (not proposed now)

Any executable-M design would have to keep the stub lines
[frag, frag+0x40) RX from step 4's writeback/invalidate until every
main-thread holder of node `unk_28` has retired and the next load has been
fenced. That is the whole residency, not a worker-only window. It would need a
new, reviewed rights-transition primitive. It is not proposed, and per the
coordinator it must not be implemented.
