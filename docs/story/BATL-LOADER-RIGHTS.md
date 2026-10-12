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
| M (model pool) | 0x3C000 | 2 | `unk_04[0]` | worker `func_80019484` | **yes**: 8-insn entry stub |
| A (animation pool) | 0x18000 | 2 | `unk_18` | worker `func_80019514` | no |
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
  - The only consumer, `func_80019514`, uses `frag+0x20` as a data table and never calls the entry. A is therefore data that is relocated but never executed.

## Per-load phases of one M buffer (worker thread, priority 10)

1. **Pool reinit.** `func_80002DA4` writes the header at [base, base+0x10).
2. **DMA.** `func_80003B30` loads the compressed bytes into S (PI DMA into S only; M is not touched).
3. **Decompress.** The CPU writes the fragment at `base+0x10` (`func_80002DCC` with alignment 0x10).
4. **Relocate.** `func_80019128`:
   - RL import patch, then `osWritebackDCache(frag, relocOffset)`.
   - `Memmap_RelocateFragment`: `osInvalICache` and `osInvalDCache` over `sizeInRam`, CPU relocation writes (including text +0x00/+0x2C/+0x30), `bzero` of the reloc tail, then `osWritebackDCache`.
5. **Execute.** `func_80019328` calls `frag(0,0)` once, in the worker thread. It returns a header pointer.
6. **Data phase.** Still in the worker:
   - `process_geo_layout` allocates nodes in the same pool.
   - `func_80018C40` recolours textures (writes after execution, at addresses ≥ frag+0x40).
   - Then the main thread and RSP consume geo, DL and texture data.
   - Text is never fetched again: `unk_14` and node `unk_28` store the fragment pointer, but no call through them exists in the audited source.
7. **Retire.** The next load into the same buffer repeats from step 1.
   - The stub's stale ICache lines are invalidated at step 4 before the next execution.
   - Every model fragment uses archive id 0xEF, so `Memmap_SetFragmentMap(0xEF)` is overwritten on each load, as in vanilla.

So each M buffer cycles mutable → execute → mutable many times per battle. The
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
- It is mapped READ|WRITE only and never EXEC. The CPU writes and reads it; RSP
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
  - Bind is done in `launch()` before `rl_start_battle`.
  - Release returns `SC_PENDING` while the span is live. The span becomes free
    only after `sc_batl_scope_close` sees both workers drained (`unk_01 & 3 == 0`)
    and the BATL pop.
  - Unbind fail-stops if refused.
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

M could be split per load into RW data plus a 32-byte-aligned RX window that
covers only the stub:
- choose base ≡ 0x10 (mod 32), so `frag` is 32-aligned and the text window is [frag, frag+0x40);
- the pool header sits in the previous line;
- the window is executable only between step 4's writeback/invalidate and the
  stub's return, with no CPU or DMA writes to it in that window.

This would need a new, reviewed rights-transition primitive. It is deferred
until the A/S-only result is measured.
