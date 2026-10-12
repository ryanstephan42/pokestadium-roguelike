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

## v5 renderer weight, caller prefix and stack remedy (static)

**Renderer weight** (`tools/cmd8weight.py`). This is the worst weighted layout cost (W) over every parsed model
layout, with renderer recursion following the layout's nesting and each cmd8 callback charged at its v4 depth.
- Maximum is 5208 at archive `0x920000` entry 22. It is identical in vanilla.
- Archive 0x920000 is indexed by species − 1 (`19840.c:501`). So entry 22 is Ekans, entry 23 Arbok (3864) and
  entry 70 Victreebel (4056). Ekans appears in Red/Blue story battles, so the bound is battle-reachable.
- The scene chain from the entry of `func_80015094` down to the model instance costs 632.
- Stage layouts are small.
- The IRQ charge on the Game-thread stack is 0, because the handlers run on their own stacks.

**Caller prefix** (`tools/prefixdepth.py`, `tools/revclosure.py`, `tools/framediff.py`). The callable
Game-thread stack is 6352: `D_80080000[0x18E0]` is 6368 nominal.
- Fragment62's main `func_84301A2C` is the only battle root of `func_80015094` (`outputs/revclosure-80015094-N.txt`).
  The other roots are called only from other fragments.
- The only nested `func_800078D4` loop reachable from battle is fragment32, the transfer-pak fatal screen.
  Its `while (1)` never returns.
- **Original N (`0b4eae42`), story path:**

  | Segment | Bytes |
  |---|---:|
  | Game_Thread | 32 |
  | `sc_campaign_state` → `launch` → `rl_start_battle` | 280 |
  | `func_80029008` | 32 |
  | fragment62 prefix | 280 |
  | scene chain | 632 |
  | Ekans layout | 5208 |
  | **Total** | **6464** |

  This is **112 over callable**. The vanilla equivalent is 6224, a margin of 128.
  Frame growth against vanilla is in `outputs/framediff-N-vanilla.txt`; for example, `func_800078D4` is 88
  against vanilla's 48.

**Remedy** (commit `c5f4bb0`).
- `sc_campaign_state` is split into `sc_campaign_state_poll` and `sc_campaign_state_launch`. The state machine
  now launches after the poll frame has returned.
- `rl_start_battle` moves native-mon conversion (`rl_add_native`) and side preparation (`rl_prepare_sides`)
  into helpers. Both return before fragment62 runs. Its frame drops from 192 to 56.
- The `func_800078D4` story frame bookkeeping moves into helpers with no loop locals, dropping its frame from
  88 to 56. On exit it resets to idle rather than restoring an outer state. The only nested loop never returns
  (fragment32). If a nested loop did return, a later outer consume would be refused and the native frame shown,
  so the change fails safe.
- **New N2 (`40b61d83`), story path:** 32 + 120 + 32 + 248 + 632 + 5208 = **6272**, a margin of 80 against
  callable (`outputs/prefix-*-N2.txt`, `outputs/framediff-N-N2-text.txt`).

**Pre-existing, out of scope.** The Game_Thread prefix to `func_80029008` reaches its maximum of 1048 through
the master roguelike pending-battle path (`anon_80069ad0`, frame 928). With an Ekans-class layout that path
exceeds the stack. It is reported only and not changed here.

**Still open after v5.**
- Resolution lists were derived on N and keyed by symbol. The `main_pool_free` destructor addresses are N-specific,
  but they are not on the measured paths.
- 25 INDIRECT and 146 TAILJR sites in the fragment62 prefix are not qualified.
- The upstream prefix is maximal only along the identified roots.
- N2 is a new ELF. The same-N evidence (SC closure, arena ledger, cmd8/geometry seals) must be re-derived or
  qualified for it.

## v6 probe-route fix and N3 refresh (static)

**Correction to v5.** v5 omitted the native-probe launch route. The candidate defines `STORY_SERVICE_CANDIDATE`,
which forces `STORY_SIX_PROBE`, so `sc_runtime_probe_poll` is linked. When `gScProbe.command` is 2, 3, 5 or 6 it
launches real `SC_COLD_PREPARE` opponents.
- On N2 the route was Game_Thread 32 → `rl_main_state_roguelike` 32 → `sc_runtime_probe_poll` 144 → `rl_start_battle` 56.
- Total 264 + 32 + 248 + 632 + 5208 = **6384**, which is **32 over** callable.
- v5 also called the ordinary roguelike pending-battle route "out of scope". It is an open obligation of this task.

**Fix** (commit `0b7a4da`). `sc_runtime_probe_poll` keeps its cold request in a `static` buffer, zeroed byte-wise on
each poll after the `!command` early return, and its rules in a `static const`. The frame drops from 144 to 88.
The harness, its budgets and its models are unchanged.

**Per-build guard.** `tools/rl/check_story_frames.py` runs from `tools/rl/rl.mk` on every story build. It fails the
build unless all of these hold:
- Frame caps: Game_Thread 32, `rl_main_state_roguelike` 32, `sc_runtime_probe_poll` 88, `sc_campaign_state_launch` 64,
  `sc_campaign_state_poll` 40, `rl_start_battle` 56, `func_80029008` 32, `func_800078D4` 56.
- `rl_start_battle` calls exactly one local helper (frame ≤ 96) before its first `func_80029008` call.
- The `func_800078D4` story helpers are frameless leaves.
- Nothing calls the unreferenced `sc_campaign_state` composer.

IDO has no `noinline`, so non-inlining is checked per build rather than assumed.

**Candidate N3** = `.local/rltest/story-service-ui-native-stackfix-v1/pokestadium-rl.elf`.
- ELF sha256 `8fc62e8b8f5799c55c00286ae3e8806486f2f32cdb2a2ec3cd4064c592aff7df`.
- ROM sha256 `fb5d182066fe49a61ebeb4fb08fe6ee9e3a61189ec02572a535c5d38a899932b`.
- Build fixed point: v65 == v66.

Prefixes at `func_80029008` entry (`outputs/prefix-*-N3.txt`, 0 UNRESOLVED; the resolve files are N2 lists remapped to N3):

| Route | Prefix | Total (+32 +248 +632 +5208) | Margin vs 6352 |
|---|---:|---:|---:|
| story: Game_Thread 32 + `sc_campaign_state_launch` 120 | 152 | 6272 | 80 |
| probe: Game_Thread 32 + `rl_main_state_roguelike` 32 + `sc_runtime_probe_poll` 88 + `rl_start_battle` 56 | 208 | **6328** | **24** |
| ordinary roguelike: Game_Thread prefix 1048 (`anon_80069ad0` frame 928) | 1048 | 7168 | **−816 (open)** |

Notes on the table:
- Correction (v7): an earlier revision of the probe row printed the N2 poll frame 144; the N3 terms are 32 + 32 + 88 + 56 = 208.
- The only callers, found by `jal` and whole-word scans over all PROGBITS, are:
  - `sc_runtime_probe_poll` ← `rl_main_state_roguelike` @80069ee0;
  - `rl_main_state_roguelike` ← Game_Thread @8002c280;
  - `sc_campaign_state_launch` ← Game_Thread @8002c2b0, and the unreferenced composer.
- The frag62 prefix to `func_80015094` stays 248.
- The ordinary route is not yet fixed. A same-ELF Ekans path and a narrow frame reduction follow as a separate commit.
- The candidate as a whole has **no** accepted stack clearance while it remains open.

**Address-keyed refresh on N3.** N3 differs from N in `.text` +240 and `.text_bss` +48, so `gPool` moves +0x120
(`0x8012dc10` → `0x8012dd30`). Fragment, `.fragmentSC` and archive section sizes are unchanged. Reproduced:

| Artifact | Result on N3 |
|---|---|
| `renderrec.py` | Handler table `0x8008b454` (N `0x8008b344`). Output equals N's after masking addresses. Frame R 40, Pmax 176, NRmax 480, CBmax 1144. |
| `cmd8depth` (`stackdepth-v3.py`, 56 roots) | ROOT lines identical to N. Maximum 1144. |
| `cmd8weight.py` | Wmax **5208** (0x920000:22). Stadium archive (`0x595800` in N3) 648. Byte-identical to N apart from the archive offset. |
| `scenechain.py` (new) | **632** from the ELF alone. Path: three fragment62 scene layouts → graft of hosts `D_800AC840`/`D_800AC858` → instance type 0x16. Breakdown 24 + 216 + 72 + 88 + 72 + 160. N and vanilla also give 632. |
| `cmd8domain.py` | 56 operands, 0 anomalies, rows identical to N. |
| `cmd8scan-v2.py` | Range `[0x80015ec0, 0x80015fdc)`, gPool `0x8012dd30`, 4 and 8 MiB: 622335 candidates, **0 hits** (N had 622400). |
| `refscan2.py` | Only the self `jal` @80015f8c, plus the same asset DATA words at the same ROM offsets as N. |
| `yay0scan.py` | 4 hits, all in unaligned streams or non-4-aligned values. |
| SC closure (`resolve-SC-N3.txt`) | `sc_cold_entry` **2832** over 591 functions (+4 helpers), 0 UNRESOLVED. `func_84300E88` 3200. The unreferenced `sc_campaign_state` is 3040 (+24, via poll). All other entries unchanged. |
| `main_pool_free` destructors (`dtor_sites.py`) | 18 registrar sites. Their 13 materialized targets equal the 13 resolve entries exactly. The anonymous SC targets moved by +0x120. |
| TAILJR (`switchcheck.py`, every site reached in N3 outputs) | 177 sites. 176 validate against switch tables with 0 outside targets. The remaining one, `__LeoBootGame3` `jr a0`, is a terminal transfer to a booted image. |
| IRQ (`irqcheck.py`) | See below. |
| Arena | Persistent-arena block unchanged: cold RAM 225840 → aligned 225856, still **+2368** versus C. New charge: the main pool (`POOL_END − gPool`) is **288 bytes smaller** than on N. |

`irqcheck.py` result in full:
- `__osExceptionPreamble`, `__osException`, `send_mesg`, `handle_CpU`, `__osEnqueueAndYield`, `__osEnqueueThread`,
  `__osPopThread` and `__osDispatchThread` make **no** stack-memory accesses.
- Their only `sp` writes are:
  - the handler-stack load `lw sp,4(t1)` in the `jalr` delay slot;
  - the scheduler restore `ld sp,240(k0)`.
- The only `__osSetHWIntrRoutine` call installs `(1, __osLeoInterrupt, leoDiskStack)`.
- `__osHwIntTable` is initially zero.
- So, under this installer/routine model, the exception path charges 0 bytes to the interrupted Game-thread stack.
  The setter evidence is a direct-`jal` scan; computed setter calls or stores into `__osHwIntTable` are not
  exhaustively excluded (qualified in v7).

**Still open after v6.**
- Ordinary roguelike route: 816 over.
- 25 INDIRECT sites (frag62 prefix) and 20 (Game_Thread prefix) are listed but not classified.
  - Reverse closures are unchanged from N2: `func_80029008` 423 functions, 37 address-taken; `func_80015094` 253 / 13.
- Renderer co-occurrence beyond the single-layout bound.
- The 0x820000/0x898000/stadium-model static layouts are scan-only.
- Synthesised runtime layouts.

## v7 ordinary-route fix, fail-closed guard and N4 refresh (static)

**Ordinary route fix** (commit `3c74ab2`). `rl_main_run_pending_battle` (static, emitted as `anon_80069ad0`) kept a
local `RlLaunchResult` (frame 928). `rl_start_battle` already fills the global `gRlLastBattleResult` and only copies it
to `out` on success, so the runner now passes `NULL` and reads `&gRlLastBattleResult`. Semantics are unchanged: the
runner only reads the result after a successful launch, which is exactly when the copy happened before. The host
stub in `host_tests/test_play_ui.c` mirrors the launcher contract. Runner frame 928 → **48** in both the plain
`make rl` build (`rl-plain-base.elf` / `rl-plain-fix.elf`) and the candidate. No stack size was increased.

**Fail-closed guard** (commit `0c3df6f`; addresses the UI reviewer's negative fixtures on N3).
- `frame()` fails when control flow precedes the prologue (a bare `jr ra` leaf counts as 0), on a positive sp adjust,
  or when no prologue appears in 16 instructions.
- `leaf()` is a bounded CFG walk with delay slots: only forward non-linking conditional branches are allowed, every
  path must end in `jr ra`, and calls, `jalr`, tail `j`, backward branches, traps and sp writes fail.
- The composer check rejects J/JAL and branch targets, aligned data words in any PROGBITS, and `lui`/`addiu|ori`
  pairs equal to `sc_campaign_state`.
- New runner rule: `rl_main_state_roguelike` has exactly one local helper that calls `rl_start_battle`, frame ≤ 48.
- `tools/rl/test_check_story_frames.py` patches ELF copies and requires all 10 mutations to be rejected (oversized
  probe prologue, composer tail-j, helper tail-j to a framed target, backward branch, jump in a branch delay slot,
  `jalr`, unrecognised prologue, raised `rl_start_battle` frame, composer data word, composer lui/addiu).
  N3 is rejected by the runner rule (928 > 48).

**Candidate N4** = same build profile as N3 with `3c74ab2` + `0c3df6f`.
- ELF sha256 `ab138b611938aa6b3e56efc99122e47a06b70828a40b05a0adffc0838d5f0bbc`.
- ROM sha256 `97e41701a08bfa46382222fc62d9278dcacc81d2f6cff25f24cd573b91d4700f`.
- Module-rights and AI-authority fixed point: the build consumed v69 and regenerated v70 identically; both bind the
  N4 ELF (and the rights bind the N4 map `c4ffb20d…cbf`). Residency lint is reported separately and is not this evidence.

Prefixes at `func_80029008` entry on N4 (0 UNRESOLVED):

| Route | Prefix | Total (+32 +248 +632 +5208) | Margin vs 6352 |
|---|---:|---:|---:|
| story: Game_Thread 32 + `sc_campaign_state_launch` 120 | 152 | 6272 | 80 |
| probe: Game_Thread 32 + `rl_main_state_roguelike` 32 + `sc_runtime_probe_poll` 88 + `rl_start_battle` 56 | 208 | 6328 | 24 |
| ordinary: Game_Thread 32 + `rl_main_state_roguelike` 32 + runner 48 + `rl_start_battle` 56 | 168 | **6288** | **64** |

- Game_Thread's overall prefix maximum 488 is the transfer-pak fatal screen (`func_8000A798` → `func_8002B274` →
  fragment32), not a battle route; `rl_main_state_roguelike`'s 176 maximum is the probe route above.
- These are static bounds over the stated paths with the single-layout renderer bound, not clearance.
- Probe premise: `sc_runtime_probe_poll`'s static request buffer is only valid because the poll runs only on the
  Game thread, is not re-entered, and its cold dispatch is synchronous. There is no runtime re-entry guard; this is a
  stated premise, not a checked property.

**Address-keyed refresh on N4.** N4 vs N3: `.text` −416, `.text_bss` unchanged, `gPool` `0x8012dd30` → `0x8012db90`, so
the main pool is 416 bytes larger than N3 (128 larger than N). `.fragmentSC`, fragment and archive sections and the cold
RAM extent (225840 → aligned 225856, +2368 arena charge) are unchanged. Code after `rl_main.c` moved −0x1a0.

| Artifact | Result on N4 |
|---|---|
| `renderrec.py` | Handler table `0x8008b2b4`. CBmax 1144. |
| `cmd8weight.py` | Wmax **5208**; stadium archive (`0x595660`) evaluated separately. |
| `scenechain.py` | **632** (hosts `D_800AC840`/`D_800AC858` now at `800d20d0`/`800d20e8`). |
| `cmd8domain.py` | 56 operands, 0 anomalies. |
| `cmd8scan-v2.py` | `[0x80015ec0, 0x80015fdc)`, gPool `0x8012db90`, 4 and 8 MiB: 622231 candidates, **0 hits**. |
| SC closure | `sc_cold_entry` **2832** over 591 functions, 0 UNRESOLVED; entry depths identical to N3. |
| Destructors | 18 registrar sites; the 13 targets equal the resolve list (anonymous `.text` targets −0x1a0). |
| TAILJR | 179 reached sites (new: io_status `0x80072528`, 12 entries; fragment43-parser closure `0x800289a4`). 178 validate with 0 outside targets; `__LeoBootGame3` remains the terminal transfer. Arguments are now produced by `tailjr_args.py`, which reproduces the N3 list byte-for-byte. |
| IRQ | Same routine audit as v6. |

**Game_Thread INDIRECT sites (20).** The UI reviewer's N3 classification is reproduced on N4 by `indirect20.py`:
installed targets are derived from the ELF (`sc_native_terminal_install` materialises arm/drain/abort `8f90e304`/
`8f90e374`/`8f90e410`; `native_call` materialises open/read/write `80072958`/`8007285c`/`8007287c`), each `jalr`'s field is
taken from its `lw $25,OFF` (4/8/12), all installed, header and destructor targets are seeded as boundaries before
analysis, and overlay headers are decoded from the raw `j` word. Per-site P+D maximum is **5248** (storage read under
`read_slot`); hooks run before (arm) or after (drain/abort) fragment62 and are not summed with the renderer. The two
fragment43 initialiser sites stay OPEN: they reach `process_geo_layout` (`jalr 0x800198a0`). The Game_Thread prefix at
the fragment43 header target `func_83400020` entry is 496. The UI reviewer's corrected N3 figures (parser entry
`0x80019810`, 40-byte frame; parser-live 600/712; dispatch-live 792/904 or 752/864 depending on whether the
validator's 192-byte handler closure includes the parser) are conditional and are not re-derived here.

**IRQ qualification.** The 0-byte exception-path charge holds for the audited routines and the single direct-`jal`
`__osSetHWIntrRoutine` installer. Computed setter calls or stores into `__osHwIntTable` are not exhaustively excluded.

**Still open after v7.**
- 25 INDIRECT sites on the frag62 prefix; the two fragment43 parser sites above.
- Renderer co-occurrence beyond the single-layout bound; static 0x820000/0x898000/stadium layouts are scan-only;
  synthesised runtime layouts.
- Full resource fit (the UI reviewer owns the phase ledger).

## v8 lifetime, effect-index and geo-parser fixes; N5 refresh (static)

Each fix has its own commit and host regression. None of them raises a stack size.

**Story font binding** (`8defdac`). The retained story font is bound around each story frame. After a nested child or
battle replaces `D_800AC870`, or leaves it NULL, the font is re-bound and restored. Crash-retention guards are kept.

**Masked host runners** (`0a8a474`). These runners test the documented idle-handoff contract of `func_800078D4`:
display, lifetime, crash, retirement and campaign-lifetime. They had stopped running because an earlier assertion
aborted.

**Prepared-close driver retirement** (`5e4485f`). `sc_ui_driver_retire` clears `driver.bound`. It is identity-checked
(driver, UI and request), and only refuses when the exit has not drained (launch pending, request ready or save
pending). `sc_native_loop_close` calls it:
- after `sc_native_ui_release`'s bound-identity check;
- before the Pak, display and backing release.

A loop-owned `driver_bound` flag prevents a release retry from touching backing that was already freed. Crash paths
fail earlier and keep the binding. As a result, a normal exit now frees the backing, the DP token, the LIFETIME pin and
the SC image, and the campaign can be reopened.

**Effect-index refusal** (`bbc202d`). `func_8437670C` indexes `D_8438AE54[idx-1]` using the *current actor's* move
effect. Under `ROGUELIKE`, an index outside 1..87 is now refused instead of being called through the table. In a story
battle this also latches the existing invalid-observation policy through `sc_campaign_battle_fault`. Vanilla behaviour
is unchanged.

The UI reviewer showed that the `D_843C5238` predicates do not bind the current actor (confusion swap at
`359F90.c:1431-35`). That actor selection is **not** changed here and remains an open audit.

**Geo-layout admission** (`76bb9fd`, `54fddbf`). Under `ROGUELIKE`, `process_geo_layout` calls the non-inlined
`rl_geo_layout_check` before and after every table dispatch. The pure `rl_geo_admit` (host-tested, normal and ASan/UBSan)
checks, in order:
- the state is valid;
- the command is 4-aligned and its exact `sizeof` extent lies within `[0x80000000, 0x80000000+osMemSize)`;
- the opcode is below 39;
- the size is non-zero, which rejects the non-advancing `0x12` handler.

It then applies per-opcode limits: branch-stack push ≤ 16 words (op 0: +2; op 3: +1), op 1 return index ≥ 2,
op 4 pop ≥ 1, and node list open < 32 (op 5) / close ≥ 1 (op 6). The post-dispatch check keeps the restored
stack/return/node indices within range.

A violation prints and calls `osStopThread(NULL)`, the same pattern as the unsupported archive import in `func_80019128`.

In N5 the size table `sRlGeoCommandSizes` is at `0x800984a0` (bytes equal the host fixture). The `switch` was rewritten
as an if-chain so `rl_geo_admit` has no jump table: no new TAILJR.

Stack effects:
- The parser frame is **40 → 32**.
- The guard chain beneath the parser is check 40 + admit 24 + `rl_geo_state_ok` 0 (normal path). On the halting path it
  is check 40 + `osStopThread` 56 + `__osDequeueThread` 8 = 104 (`geodepth-N5.txt` worst 136 including the parser).
  Either way this is below the validator's 192-byte handler closure, so it adds nothing to the dispatch-live maximum.
- The UI reviewer's conditional dispatch-live figures therefore drop by 8 (792/904 → 784/896 under the same
  handler-closure premise).

The guard does **not** bind handler payload pointers: cmd7 data, cmd8 callbacks, BD9C objects or the stadium leaves.
Those remain under the cmd8/model domain obligations below.

**Candidate N5** = N4 + the six commits above, same build profile.
- ELF sha256 `738518bc9ffcafff8ad018ce37fa610d95d23e8627f3b58ddc526d5b611963c6`.
- ROM sha256 `dd8c0d96ba7978b5f85d575b9c207c9fa65234ca76fcdf3d007ac3016348fca6`.
- Map `ec999661…352a`.
- Rights/authority fixed point: the build consumed v81 and regenerated v82 identically; both bind the N5 ELF and map.

**Address-keyed refresh on N5.** Compared with N4, `.text` grew by +992 (`0xa41a0` → `0xa4580`), and `gPool` moved
`0x8012db90` → `0x8012df70`.

Stage resolves, prefix/SC resolves and the 20 Game_Thread sites were remapped from N4 by `remapres2.py`. It maps by
section + symbol offset and requires a masked 8-instruction match. Where the same offset mismatches, it accepts only
a *unique* 16-instruction match within ±0x100. Four SHIFTs were accepted:
- `sc_native_loop_open` +0xc;
- `sc_ui_driver_bind` +0xc0;
- `sc_native_resources_acquire_upper` +0xc;
- site `sc_campaign_menu_hook` +0x8.

| Artifact | Result on N5 |
|---|---|
| `check_story_frames.py` | OK. `func_80029008`=32, runner@`80069d20`=48, `rl_start_battle`=56, probe 88. All 10 negative fixtures are rejected. |
| Route totals | Unchanged: story **6272**, probe **6328**, ordinary **6288** of callable 6352. |
| `renderrec.py` | Handler table `0x8008b614` (N4 `0x8008b2b4`). CBmax 1144. |
| `cmd8weight.py` | Wmax **5208**. Stadium archive `0x595a60`: Wmax 648 (18 layouts). |
| `scenechain.py` | **632**. |
| `cmd8domain.py` | 56 operands, 0 anomalies. |
| `cmd8scan-v2.py` | 622118 candidates, **0 hits** (4 and 8 MiB). |
| SC closure | `sc_cold_entry` **2832** over 594 functions (+3: driver retirement). |
| `indirect20.py` | Per-site P+D maximum **5248**. The two fragment43 sites stay OPEN; the predecessor prefix at `func_83400020` is unchanged (496). |
| TAILJR, destructors, IRQ | Same disposition as N4. Switch tables: 0 outside targets. |

After stripping addresses, the renderer, weight, scene and depth outputs are identical to N4.

**Lower-pool static key 1** (`poolkey1.py`, which reproduces the validator's N4 values exactly):

| | N4 | N5 |
|---|---|---|
| gPool | `0x8012db90` | `0x8012df70` |
| Pool | capacity 2958416 | `[0x8012df80, 0x803ffff0)`, capacity **2957424** (−992) |
| fragment62 | charge 815616 | PROGBITS `0x8f4d0` + BSS `0x37d40`, charge **815648** (+32) |
| fragment62 ROM | | `[0x310520, 0x39f9f0)` |
| Header | | raw `FRAGMENT` magic, so the uncompressed loader branch |

Applying these deltas to the validator's corrected arithmetic-only N4 remainder (549120) gives 549120 − 992 − 32 =
**548096**. This is still arithmetic only.

**Still open after v8** (the successor must carry these concretely):
- **Geo handler payload pointers.** Cmd7/cmd8 data and callbacks; runtime-synthesised streams; the static
  0x820000/0x898000 layouts are scan-only.
- **Effect actor.** The actual-actor effect history and the confusion-callback actor selection (`359F90.c:1431-35`).
  The index is now refused fail-closed, but the whole-history domain is unproven.
- **Model domains.** The stadium-models leaves (live 144 each) and the BD9C conditional 592 (from the UI reviewer's
  `n3-model-domain-review.txt`, `511fe91b…0689`). These are conditional and not added to W.
- **fragment43.** The two fragment43 INDIRECT sites. Other callers of `func_80029008` remain caller-specific.
- **Lower ledger keys 2–6.** Push/pop phase traces, phase-addressed heads, model/graph measurement, fr31/menu/battle_ui
  payloads, per-phase occupancy and contiguous spans. These need owner traces or native runs.
- **Clearance.** Renderer co-occurrence, full fit/contiguity, and native/gate. This is not clearance.

## v9 actor-semantics, payload-admission and fragment31 callback fixes; N6 refresh (static)

All changes are behind `ROGUELIKE`. Vanilla `make` still reports `build/pokestadium-us.z64: OK`.

**Commits since N5 (`0911f55`):**

| Commit | Change |
|---|---|
| `e4dc3dc` / `960de92` | The global "acting battler only" effect-dispatch guard was reverted. It changed the vanilla dispatcher semantics: confusion's self-hit deliberately keeps the current index for the phase-14 notification. |
| `aba9fca` | In both effect-0x1D-gated blocks of `func_843703BC`, the roguelike build calls `func_84374E08` directly. This is table slot 0x1C, which is exact. The current index, the self-hit notification and the scratch mutation are preserved. There is no actor flip and no change to the global dispatcher. `host_tests/test_confusion_effect29.py` runs the 8 extracted fixtures plus edge cases under ASan/UBSan, and the vanilla mutant reproduces the fault or the wrong callback. |
| `5b40b8a` | Geo cmd7/cmd8 payload pointers are admitted before dispatch (`rl_geo_guard`). |
| `ae9115b` | `func_81000420` reads `func_800325B4()` only inside `arg0 == 2`, so phase-0 import never reads the actor. The getter is a side-effect-free read. In IDO, `bne a0,2` precedes `jal func_800325B4` (`host_tests/test_phase0_actor_getter.py`). |
| `648b1f5` | The same callback's index is now `((u32)B0*3 + (u32)counter) & 7`, and the persistent counter is updated as `(counter + 1) & 7`. Normal 0..7 cycling and the positive B0 writer semantics are preserved. Fixtures: wrap at sum `0x80000001`, negative B0, and 6000-call repetition. The vanilla `-fwrapv` mutant reproduces index −7. IDO emits `andi a2,7` for the index and `andi t7,t6,7` for the counter. |
| `9f8c9fc` | The CI callbacks `81000180/188/190/198` (`func_81002F38/FC0/31C0/33C0`) consume the fragment-8 offset through the fail-closed `rl_ci_frame`. See the checks below. |

`rl_ci_frame` makes three checks:
- Fragment 8 is resident at the exact relocation binding: `Memmap_GetLoadedFragmentVaddr(&D_862082A0[0]) == 0x862082A0` and `[3] == 0x862082AC`.
- `palette − texture == 0x23288`.
- `travel <= (0x23288 − 4·0x800)/4 = 33954`.

Any violation calls `osSyncPrintf` and then `osStopThread`. The maximum legitimate travel is 32760: `func_862015C8` initialises the value positive and the updates are capped at 910. `host_tests/test_ci8_strip.py` covers 12492 fixtures. The writer's value history is not asserted; checked consumption covers it instead.

**N6 candidate:** fixed point v94 = v95.

| Artifact | SHA256 |
|---|---|
| ELF | `a5733fd7d05bffbf5d7d7be42318570ace9b7ce3dc53804a2a8843e736698321` |
| ROM | `27c4e87a6624f23d83db6763b53cda85ac6443f650e2c53faed2765b86481a4d` |
| Map | `58ccc63bd7277c5d8fba99595582774ae4e462f48d6a32f4e45c88e325f36d73` |

The N5→N6 remaps of the v3, prefix, SC and site lists report no SHIFT. The stage GEN run and an independent fresh-scratch run both end with VERIFY PASSED.

| Artifact | Result on N6 |
|---|---|
| `check_story_frames.py` | OK; the frames are identical to N5. |
| Route totals | Unchanged: story **6272**, probe **6328**, ordinary **6288** of callable 6352. |
| `renderrec.py` | Handler table `0x8008b674`. CBmax **1144**, terminal 1184. |
| `cmd8weight.py` | Wmax **5208**. Stadium archive `0x595d50`: Wmax 648. |
| `cmd8depth` | The maximum root is still `func_81001E24` at 1144. `func_81000420` is 1024, as before. The new CI path through `func_81002F38` reaches a worst of 224, via `rl_ci_fail`/`osStopThread`. |
| `scenechain.py` / `cmd8domain.py` | 632. 56 operands, 0 anomalies. |
| `cmd8scan-v2.py` | 622053 candidates, **0 hits** (4 and 8 MiB). |
| SC closure | `sc_cold_entry` **2832**. |
| `indirect20.py` | Per-site P+D maximum **5248**. The two fragment43 sites stay OPEN. |

**Lower-pool static key 1:**
- gPool is at `0x8012dfd0`.
- The pool is `[0x8012dfe0, 0x803ffff0)`, capacity **2957328** (−96 versus N5).
- fragment62 still charges 815648, has a raw `FRAGMENT` header, and occupies ROM `[0x310810, 0x39fce0)`.
- Arithmetic-only remainder: 548096 − 96 = **548000**. This is not a fit claim.

**Still open after v9:**
- **Deferred render payloads.** Recursive borrowed G_DL with inherited GPU segment state. Segment-14 VTX (image or `0xa0`-generated through `366A4`). The callback 0x140 output bound through `32F94`. The lifetime of the last referencing task for the alternate image/frame generation.
- **Render-domain payloads.** cmdA/cmd17 payloads. Runtime-synthesised or replaced roots and writers. The remaining 56-domain callback argument schemas.
- **CI texture extent.** The CI strip is bounded only by the texture-to-palette distance `0x23288`. A texture-owned extent remains qualified.
- **Effect history.** Whole-turn reachability and canon intent of the confusion and Substitute-break histories.
- **Carried over from v8.** Stadium-models live 144 and BD9C conditional 592; the two fragment43 sites; lower keys 2–6; co-occurrence, fit/contiguity and native/gate.

## Not claimed

- After v3, BD04 is bounded as a 0-byte leaf.
- After v4, `func_80015220` is excluded from the installed `unk_10` domain. This holds only under the installer
  model stated in v4 and is not a proof against layouts synthesised at runtime.
- The 56 cmd8 callbacks have per-callback static bounds (maximum 1144). Renderer recursion, co-occurrence
  and the unparsed layout sources are not bounded.
- Thread-stack budgets other than the Game thread, and concurrent-thread interleavings, are not assessed.
  Interrupt/exception frames are covered only to the extent of the v6 `irqcheck.py` routine audit.
- Data-indexed tables assume well-formed indices.
- The v5 stack totals are static arithmetic over the stated paths, not a runtime high-water measurement.
- No fit, contiguity, native or gate claim.
