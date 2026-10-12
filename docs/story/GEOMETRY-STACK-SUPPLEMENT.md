# Geometry / stack supplement (static, bounded)

Static ELF analysis only. No ROM build, emulator, native run or gate is implied. Nothing here is a fit,
contiguity or stack grant; ledger margin 164,624 stays conditional.

ELFs:

- **C**: candidate `.local/rltest/story-service-ui-native-layout-v1/pokestadium-rl.elf` (sha `3b01dccb…`).
- **O**: `build/story/menu-entry-4bbe01c-objects-v1/pokestadium-rl.elf` (sha `1804f58e…`), whose addresses the
  UI session quoted.

## Method

`stackdepth.py` (sealed with its inputs) works per section:

- Functions come from section-qualified `F` symbols. Code with no symbol (IDO statics) becomes pseudo-functions
  `anon_<addr>`, split at every `jal` target and requested root.
- Disassembly uses `objdump -z`, and missing instructions are reported.
- `jal` resolves in the same section or in resident `.text`. Any other target is followed only if exactly one
  code section contains it (`XSEC`); otherwise it is reported as `CROSS`/`CROSS-AMBIG`.
- Every `jalr` is reported unless an explicit resolve file maps that site (function, section, ordinal) to a
  target domain. A site that stays unresolved is kept as `UNRESOLVED`, never dropped.
- Every non-`ra` `jr` is reported (`TAILJR`) and must match a switch table that `jtables.py` validates for that
  section, with every target inside the enclosing function.
- Frame = the largest `addiu sp,sp,-N` in the function.
- Depth = own frame + maximum callee depth, including the root's frame. Recursion is reported and cut.

Static depth over-approximates: it ignores branch conditions unless a site is explicitly gated.

## Indirect-site domains

All of these are identical in C and O, with addresses shifted.

| Site (function, ordinal) | Domain | Evidence |
|---|---|---|
| `main_pool_free` both `jalr` (C 80002c70/80002cd8) | registered destructors: C 13, O 12 (O has no `restore_upper`) | every `main_pool_set_func`/`alloc_with_func` site (`dtor_sites.py`), materialized `lui`/`addiu` per registrar; DYN0/DYN1 register NULL; realloc preserves |
| `_Printf` `jalr s4` (9 sites) | `func_80008130`, `func_8001F1C4`, `crash_screen_copy_to_buf` | every linked `_Printf(prout,…)` caller (stage_loader.c:749/777, 1CF30.c:524/631, crash_screen.c:174/364); sprintf/syncprintf are not linked |
| `func_8432C0D0` #0/#1 | `D_84386480` (145 entries, 124 unique) | fragment62_315D50.c:950 static table, located by exact word content (C 84387380, O 84387340) |
| `func_8432C14C` #0 | `D_843866C4` (90 entries, 79 unique) | :973 (C 843875c4) |
| `func_8432C1E0` #0 / #1 | `D_84388280` (150/23) / `D_843884D8` (100/8) | fragment62_347330.c:35/60 (C 84389180 / 843893d8) |
| `func_8432CEF0` (`D_84390300[i]`) | `D_84388668` ∪ `D_843888C0` | the only non-NULL writer is `func_8432C194` (:4645), called only from `func_8432C1E0` (:4681/:4698) |
| `func_8432CF74` (`D_84390320[i]`) | `D_84388A50` ∪ `D_84388CA8` | same writer (:4646) |
| `func_8140C468` (`unk_04`), `func_8140C530` (`unk_08`, `unk_04.func`), `func_8432E184` (`unk_08` over 300 `D_8140E720` records) | effect-callback domain: **250** functions in C and O (v2; see below) | every function named outside a call in fragment34/62 C source whose first parameter is `unk_D_8140E720*` (`srcdomain.py`); all 250 resolve to F symbols in both ELFs; the only `#include`d `.c` (`src/story/sc_ai_native.c`) does not use the type; no casts into these types; fragment62 asm-only bodies have no `%hi`/`%lo(func_…)` materializations |
| `func_8003260C` #0 | `func_810007A8` (fragment31, depth 144) | `Util_ConvertAddrToVirtAddr(func_810007A8)`; taken only if `unk_01A < 0x98` (32D10.c:147) |
| **`func_8001BD04` #0 (UNRESOLVED)** | entry of a relocated Pokémon model fragment from the 0x920000 archive | `func_80019328` stores the `Fragment*` in node `unk_28` (19840.c:212); the code is asset bytes, not ELF |
| **`func_80015220` `unk_10` (UNRESOLVED)** | GraphNode callback domain, not enumerated | 12D80.c:1035; recurses over `unk_0C` (:1071), so depth = scene-graph depth |

TAILJR: C 61 / O 60 reached sites, all matching validated switch tables (0 unverified).

## v2 correction: effect-callback domain (189 → 250)

`srcdomain.py` v1 stripped comments with `re.sub(r'//.*|/\*.*?\*/', '', s, flags=re.S)`. Under `re.S` the
first `//` consumed the rest of each file, so only definitions and address-taken uses before the first line
comment were seen. The validator caught this.

v2 uses a C-aware stripper:
- string and char literals are preserved;
- `//` runs to end of line;
- `/* */` blocks are replaced, keeping their newlines.

It yields 250 typed targets, which is the 189 from v1 plus 61 added, none removed. The number of
address-taken names undefined in source drops from 3 to 0.

With `func_8003260C` resolved in every run:
- largest member depth: 312 for both the v1 and v2 domains;
- largest among the 61 added members: 296.

The unsent figure of 248 came from a run without the resolve file. Every root depth below is unchanged
under v2, and so are the UNRESOLVED, CROSS, MISSING, and recursion counts. The reached switch dispatches
(TAILJR) are still C 61 / O 60, with 0 unverified.

## Results (worst static depth in bytes, root frame included)

Identical in C and O except `main_pool_free`.

| Root | Depth | Residual |
|---|---|---|
| `main_pool_free` | C **832** (`restore_upper`→`HAL_Printf`→`_Printf`→`_Ldtob`); O 192 | none |
| destructors | `func_80004200` 40; `func_80004364` 152; `restore_upper` (C 8007dccc) 792; C 8007a890/8007a8bc (O 8007a400/8007a42c) 24 each; all others 0 | none |
| graft `func_84306FB8` (0x9A) | 120 with the 0x9A gate; 240 ungated | `func_8001BD04` model entry (asset) |
| `func_8001BB58` / `BC34` / `BD04` | 64 / 80 gated (200 ungated) / 72 | BD04 asset entry |
| `func_8140C468` / `func_8140C530` | 336 / 368 | none |
| `func_8432C0D0` / `C14C` / `C1E0` | 288 / 240 / 480 | none |
| `func_8432CEF0` / `CF74` | 456 / **1240** (`D_84388A50`→`func_84359D28`→`func_84362084`→`func_8436EDE8`→`func_8436ECB0`→`guMtxCatL`→`guMtxCatF`) | none |
| `func_8432E184` | 352 | none |
| `func_80015220` | 64 per recursion level | recursion (graph depth) + `unk_10` |

## Graft call chain (0x9A)

1. `func_84306FB8` (fragment62_2EC3C0.c:2038, frame 40) calls `func_8001BB58(ptr)`, then
   `func_8001BC34(ptr, 0, 0x9A, unk_720->unk_18)`, then `func_8001BD04`.
2. `unk_720` is stored at fragment62_2EC3C0.c:1923 from a caller argument; tracing its value to a load is
   not claimed here. Battle setup loads model 0x9A with `func_80019D18(0x9A)` (fragment62_2EA8E0.c:700). That
   function fetches archive entry `0x9A-1` from 0x920000 (19840.c:501) and builds nodes through
   `func_80019328`, which stores the model `Fragment*` in node `unk_28` (19840.c:212). `func_8001BD04` calls
   that pointer, so it is treated as the UNRESOLVED asset entry.
3. `func_8001BC34` (1C720.c:36) links the node (`func_80012094`) and calls `func_8003260C`. Because
   `arg2 = 0x9A >= 0x98`, its fragment31 thunk call is skipped.

## Corrections to earlier unsent figures

The earlier unsent destructor table (old `dtor_tree.py`, no `-z`, not section-aware) merged adjacent static
functions:

- `restore_font` (C 8007a840) is a leaf (0), not 24.
- `func_80004364` is 152, not 264.
- `restore_upper` is 792 standalone (832 through `main_pool_free`), not 856.
- Its "47 unresolved" were `_Printf` prout sites, now resolved to 3 targets.

## v3 residual closure on candidate N (static)

Candidate N is the scratch-remedy ELF, `0b4eae42…c142b`.

**N against C.** `.fragment31`, `.fragment34` and `.fragment62` are byte-identical between N and C. `.text` is
the same size. Only 15 `.text` words differ, and every one is the low half of a `lui`/`ori`/`addiu` immediate
(SC size words). Every function address is unchanged.

Running the v2 tool on N with `resolve-C-v2.txt` reproduces `geom-C-v2.txt` and `effdepth-C-v2.txt` byte for
byte.

**BD04 asset entry (`func_8001BD04#0`): resolved as a 0-byte leaf for every model.**

- *Where the pointer comes from.* The `unk_28` call sites are 1C720.c:73, :87 and :121. The only `unk_28`
  writer for a model node is `func_80019328` (19840.c:212). It runs only for type-0xE roots, and every caller
  passes a `Fragment` taken from `D_800ABE10.unk_A04.unk_00`. That field is set once to the 0x920000 archive
  (19840.c:343), and every use of it reads that same archive (19840.c:239/501/518/534).
- *What every entry function looks like.*
  - `archive_audit.py` decompresses all 215 entries of the archive inside the N ROM. The archive bytes are
    identical in the N and vanilla ROMs.
  - Every entry is a FRAGMENT whose entry is at +0x20. Every header's `unk_14` is that entry.
  - Every entry function has the same instruction pattern once its `lui`/`addiu` immediates are masked
    (pattern `ec0bcab3…`, 215 of 215).
  - That pattern is a 7-instruction non-calling leaf. It has no `sp` adjust. It stores `a1` to `4(sp)`, which
    is inside the caller's o32 argument area; `func_8001BD04` has a 24-byte frame. It returns the header or 0.
- *Graft 0x9A specifically.* The graft child is `record->unk_18`. Its only typed writer is
  `func_843013E4` (fragment62_2EA8E0.c:561), which stores the layout-0 root of `func_80019D18(0x9A)`.
  - That root is built by command 0x17, the first constructor in the layout, so it is a type-0xE node and its
    `unk_28` is entry 0x99.
  - Model 0x9A has no cmd8 and a maximum open depth of 5.
- *Tool change.* `stackdepth-v3.py` adds one token, `LEAF0:`, reported as `ASSETLEAF0`, which contributes 0
  bytes. Every root depth is unchanged (BD04 is 72, set by `func_800173DC`). The UNRESOLVED count drops
  from 3 to 1, and the effect-member maximum stays 312 with 0 UNRESOLVED.

**`func_80015220` (`unk_10` and recursion): no caller found in N or vanilla.**

`refscan.py` searches for three kinds of reference:
- the function's address as a data word;
- a `jal` encoding of it;
- a `lui`+`addiu`/`ori`/`lw`/`addi` pair building it (low half within 32 words).

It searches every PROGBITS section of the ELF, the raw ROM, and every decompressed entry of both runtime
archives (0x920000, 215 entries, and 0x8CC000, 63 entries). The only hit in each ELF is the function's own
recursive `jal`: N 80015f7c inside 80015eb0..80015fcc, and vanilla 800152ec inside 80015220..8001533c.

It is also absent from the renderer handler table `D_8006F0A4` (12D80.c:32-37). It appears in no reached set
of any geometry root, effect member, or SC closure output. It stays listed as a root only to show its
self-contained 64-per-level figure.

**Graft `unk_10` domain is empty.**
- The graft node is set up by `func_80011938`, which calls `func_80010FA0` and so leaves `unk_10` NULL
  (11BA0.c:15).
- The only non-NULL writer of `unk_10` is cmd8, through `func_80010FDC` (geo_layout.c:150), and model 0x9A
  has no cmd8.

**Still open after v3.**
- *Other models' callbacks.* Archive-wide, models do install cmd8 callbacks: 26 distinct targets in the
  `.fragment31` jump thunks `0x810000b8..0x810001f8`. Most land in fragment31. Three cross-fragment thunks
  land in fragment3 (`0x8790xxxx`, used by entries 161 and 170), and `0x810001d0` lands in
  `func_81407874` in fragment34 (entries 164–170). The archive-wide maximum open depth is 28. These belong to
  vanilla model rendering and are not part of the 0x9A graft.
- *Renderer recursion.* `func_80013330` / `func_800133D8` / `D_8006F0A4` handlers, and the `unk_10(2|5)`
  sites over the whole battle scene graph, are not roots here. The graft adds its subtree (open depth 5)
  under a scene node.
- *Prefix and IRQ.* The caller prefix and IRQ accounting are not qualified.

Seal: `build/story/geometry-residuals-<commit>-v1/`.

## v4 installed-binding domain on candidate N (static)

This section derives the domain of installed `GraphNode.unk_10` values from the installer and relocation
code. It replaces the v3 "26 distinct targets" estimate.

**Installer path (source).**
- The only `GraphNode.unk_10` writers are `func_80010FA0`, which writes NULL (11BA0.c:15), and
  `func_80010FDC` (11BA0.c:27).
- `func_80010FDC` is called only from geo cmd8, `func_80017B60` (geo_layout.c:150), with the layout word
  `cmd->unk_04`.
- It converts a non-NULL operand with `Util_ConvertAddrToVirtAddr` (11BA0.c:22, util.c:23) before storing it.
- The other `unk_10` writers in `src/` act on non-GraphNode types.
- Therefore every installed value is the converter image of a cmd8 operand word, as it stands after load-time
  relocation.

**Converter domains (`Util_ConvertAddrToVirtAddr`, osMemSize 4 or 8 MiB).**

| Operand | Result |
|---|---|
| `v < osMemSize` (physical) | `v | 0x80000000` |
| `v < 0x10000000` (segmented) | `gSegments[(v>>24)&15].vaddr + (v & 0xffffff)`; the raw value if the base is NULL |
| `[0x81000000,0x90000000)` | `Memmap_GetFragmentVaddr` |
| `[0x80000000,0x80000000+osMemSize)` | Passed through unchanged |
| Anything else | NULL |

Segment bases:
- Segment 0 is `0x80000000` (stage_loader.c:319). This is the same as the physical path.
- Segments 1–15 are set only to main-pool blocks (3FB0.c:486/498).
- `main_pool_init(&gPool, …)` (util.c:66/68) puts every pool block at or above `gPool`, which is above both
  target ranges.
- No other `gSegments` writer exists; `Memmap_ClearSegmentMemmap` sets NULL.
- So a segmented operand with segment id ≥ 1 cannot resolve into `.text`. A segment-0 operand at or above
  osMemSize resolves to at least `0x80400000`.

**Load-time translation (source).**
- `Memmap_RelocateFragment` (memmap.c) applies R_MIPS_32, R_MIPS_26 and HI16/LO16 relocations through
  `Memmap_GetFragmentVaddr` (memmap.c:216). That function rewrites only addresses in
  `[0x81000000,0x90000000)`, mapping them to `gFragments[id].vaddr`.
- Fragment buffers are pool allocations at or above `gPool` (N `0x8012dc10`, vanilla `0x80104bb0`).
- The ROGUELIKE import relocator `rl_relocate_archive_imports` (rl_native_imports.c) runs only for
  fragment id 0 (fragment31).
  - It rewrites R_26 `j`/`jal` targets in `0x843xxxxx` through the generated `rl_archive_imports` table (2055
    entries) into linked N `0x843xxxxx` addresses.
  - A missing entry stops the thread.
- Neither translation can produce a `.text` address such as `0x80015eb0`.

**Exact parsed domain** (`tools/cmd8domain.py`):
- Archive `0x920000` holds 215 model entries. Archive `0x8CC000` holds 63 record-list entries, loaded through
  19840.c:263/551 → `func_80019420`.
  - The record lists are `{s8 type, s16, void*}` at fragment+0x20, terminated by type 0, where type 3 is a
    geo layout.
  - Entries 1 and 45 are 64-byte empty stubs.
- Result: **56 distinct cmd8 operands** and 0 parse anomalies. Maximum open depth is 28 (0x920000) and 15
  (0x8CC000). Record kinds are `{3:62, 4:49, 2:2, 1:84}`.
- All 56 operands are fragment31 jump thunks `0x81000028..0x810001f8`:
  - 30 reach fragment62 through `rl_archive_imports`, for example `84343d24 → 843448d4`. These come from the
    0x8CC000 models, which v3 had not included. No import is missing.
  - The rest land in fragment31, fragment3 (`87902f80`, `8790320c`, `87903d64`) or fragment34
    (`func_81407874`).
- N and vanilla domains are identical apart from symbol names.

**Over-approximation** (`tools/cmd8scan.py`, v4.1 `tools/cmd8scan-v2.py`). This scan does not assume the
archive parser above is complete:
- It treats every byte 0x08 at any phase as a candidate cmd8 and reads the operand at +4.
- It covers every ELF PROGBITS section, the raw ROM, and every decompressed Yay0 stream. Yay0 (PERS-SZP) is
  the only decompressor; the ROMs contain no MIO0 or Yaz0 streams.
- The KSEG1 alias is included.

v4.1 resolves every candidate through the full converter model above (physical, segment 0, segment ≥ 1,
fragment, KSEG0 passthrough, NULL), for both osMemSize values:

| Build | Candidates | KSEG0 literal hits (v4) | Converter-model hits (v4.1, 4 and 8 MiB) |
|---|---:|---:|---|
| N | 622,400 | 0 | **0** |
| Vanilla | 617,050 | 0 | 29 |

- All 29 vanilla hits are odd physical-alias values (for example `0x0001526f` and `0x00015295`), found in
  icon/image data and Yay0 image streams. An odd target cannot be an instruction address.
- N's per-class counts are in `outputs/cmd8scan-v2-*`.

`refscan2.py` and `yay0scan.py` add register-tracked `lui` and all-phase stream scans. Their only non-self
hits are unaligned or odd DATA values, or bytes inside compressed streams.

**`func_80015220` status.**
- Under this installer, converter and relocation model it is **excluded** from the installed `unk_10` domain.
- v4 omitted the physical and segmented converter paths. v4.1 corrects this; the N result is unchanged (0 hits).
- Qualification: this assumes geo layouts are not synthesised at runtime by code that stores computed words.
  That has not been proven exhaustively.

**Callback depth** (`tools/stackdepth-v3.py --resolve=resolve-N-v3.txt` over the 56 runtime targets;
`outputs/cmd8depth-N-v3.txt`). Values are worst static depth in bytes, callback frame included:

| Callback | Depth | Path |
|---|---:|---|
| `func_81001E24` | 1144 | `→81001D80→810010BC(664)→guMtxCatL→guMtxCatF` |
| `func_81000420` | 1024 | `→81000330→80032F94→80032E00(560)→guMtxCatL→guMtxCatF` |
| `func_81003D2C` | 752 | `→81003A54→8002D7C0→8002D628→sprintf→_Printf→…→_Ldtob` |
| `func_84344CE8`, `func_84343D24` (fragment62) | 336 each | |

All other targets are ≤ 248.
- Cross-section edges into fragment34 are followed.
- All 9 TAILJR sites in the closure are validated jump-table switches with **0** out-of-function entries
  (`tools/switchcheck.py`, `outputs/cmd8-switches-N.txt`).

**Still open after v4.**
- *Renderer recursion.* The per-level renderer cost with co-occurrence weighting is not bounded.
- *Unparsed layout sources.* Archive 0x820000 and 0x898000, `stadium_models`, and static layouts are covered
  only by the over-approximation, not by an exact parse.
- *Battle reachability.* Which models are battle-reachable is not established.
- *Caller prefix and IRQ chain.* Not bounded.

## Not claimed

- After v3, BD04 is bounded as a 0-byte leaf.
- After v4, `func_80015220` is excluded from the installed `unk_10` domain. This holds only under the installer
  model stated in v4 and is not a proof against layouts synthesised at runtime.
- The 56 cmd8 callbacks have per-callback static bounds (maximum 1144). Renderer recursion, co-occurrence
  and the unparsed layout sources are not bounded.
- Thread-stack budgets, interrupt/exception frames and concurrent-thread interleavings are not assessed.
- Data-indexed tables assume well-formed indices.
- No fit, contiguity, stack, native or gate claim.
