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
| `cmd8weight.py` | Wmax **5208** as recorded here is **stale** (see v11): the tool substituted a 10⁶ placeholder for 21 unbound local fragment-31 callbacks, so the real N6 output was 1002776, an unknown depth. Stadium archive `0x595d50`: Wmax 648. |
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

## v10 CI short-map endpoint, shadow-slot admission and CI-owned extent; N7 refresh (static)

All changes are behind `ROGUELIKE`. Vanilla `make` still reports `build/pokestadium-us.z64: OK`.

**Commits since N6 (`da7ac74`):**

| Commit | Change |
|---|---|
| `ebdb924` | `rl_ci_frame` checks the residency of the **final byte** `0x862082AF` of the 4-byte index-3 load instead of its first byte `0x862082AC`. The first endpoint `0x862082A0` is kept. `Memmap_GetLoadedFragmentVaddr` accepts iff `diff < size`, so registered sizes `0x82ad/0x82ae/0x82af` passed the old check. They are now refused; `0x82b0` is accepted. These are new fixtures in `host_tests/test_ci8_strip.py`, and the 12492 earlier fixtures are kept. This completes the advertised short-map contract. It is **not** an installed crash: the installed fragment-8 size `0x82b0` contains the last word. |
| `a116429` | Shadow-slot admission in `src/334D0.c`; see below. |

**Shadow-slot admission (`a116429`).**

The counter:
- `D_800AF7B0[row]` is an s16 counter. It increments on each `func_80032F94` call with mode < 4. In modes 2/3 it increments only when `D_800AF7C0 >= 2`.
- It is reset only by `func_80032670`, which is called from `12D80.c` when `unk_18 < 0 && unk_01A > 0`.
- No writer bounds it below 3. It indexes `D_800AFFC8[row][3]` (`StadiumModelSlot`, 0x7D0).

Where it is used:
- the writer `func_80032BD4` (copy via `func_80033B2C`);
- `func_80032D28` (`func_800361C4`);
- the mode-2/3 direct path of `func_80032F94` (seg6 vertexSegment + DL);
- case 2/3 of `func_80032F94` (`func_800366A4`).

Why an unpopulated slot is a problem:
- `modelSegment` is written only by `func_80033B2C` (`= &slot.unkSegment`). A never-populated BSS slot holds 0, which `361C4`/`366A4` would dereference.
- `func_80005F5C` returns NULL when the arena is exhausted. Case 2/3 passed that NULL unchecked to `8003658C` and segment 14.

The fix:
- `rl_shadow_slot(row, counter, requirePopulated)` refuses `row >= 2` and `(u16)counter >= 3`.
- With `requirePopulated` set, it also refuses `modelSegment != &unkSegment`.

How each site handles a refusal:

| Site | Behaviour on refusal |
|---|---|
| `32BD4` | Skips only the copy; the matrix calls are kept. |
| `32D28` | Requires a populated slot before `361C4`. |
| `32F94` mode 2/3 | Checks after the 0xA0 allocation. On refusal it increments the counter exactly as vanilla does and returns. |
| `32F94` case 2/3 | Fails closed on `vtx == NULL` or a refusal, keeping the vanilla counter update. |

Template facts:
- Templates `D_800777A8` and `D_80077950` have type 0x0a/0x0b and 10 vertices.
- The `mvtx` span is `0x170 + 10·0x94 = 0x738`, which is ≤ 0x7D0.
- The DLs issue `G_VTX` 10 from seg14 offset 0 with TRI indices ≤ 9.

HOST evidence (`host_tests/test_shadow_slot.py`):
- The real helper and the three functions are extracted and compiled under ASan/UBSan, ROGUELIKE versus vanilla.
- In-domain traces are identical (1437 lines), with 66 refusals.
- Vanilla reproduces UBSan "index 3 out of bounds" for `[0][3]` and `[1][3]`.
- The mutants `>= 4`, "populated check disabled" and "vtx check removed" all fail.

IDO evidence on N7:
- The helper is `__rla_58` @ `80033840`: `sltiu row<2`, `andi 0xffff` + `slti 3`, `lw 0x130` versus `+0x138`.
- `func_80032BD4` calls it with `a2=0` before `jal 33B2C`.
- `func_80032D28` calls it with `a2=1` before `361C4`.
- In both `func_80032F94` paths, alloc → `__rla_58` precedes `32A7C`. In case 2/3, `beqz v0` precedes the helper and `366A4`.

Reachability of counter ≥ 3 is **not** asserted. This is fail-closed admission, not a canonical crash claim.

**Item D (callback 0x140 output):** the frame list emitted through `32F94` uses at most 19 Gfx = 152 of 320 bytes.

**CI texture-owned extent (witness; ownership withdrawn in v11 — only the initialized window and palette barrier are accepted).**
- `n7-ci-extent-witness.py` authenticates the N5/N7 ROMs and the N7 ELF.
- Archive `920000` table and entry 183 bytes are identical in N5 and N7.
- The texture-owned extent `[8ff0f9d8, 8ff32c60)` = 0x23288 has:
  - no relocation slot and no other relocation target;
  - no code HI/LO target;
  - no segment-relative Gfx operand into it;
  - the final palette as the next object.
- `rl_ci_frame` admits travel ≤ 33954. The worst consumed end offset is 144000 ≤ 144008.
- The disassembly contains `0x84a3`, `0x3288`, `ori 0x82a0` and `ori 0x82af`.
- The witness ends with "CI-OWNED-EXTENT WITNESS PASSED".

**N7 candidate:** fixed point v99 = v100 (v97 → v98 → v99 changed).

| Artifact | SHA256 |
|---|---|
| ELF | `b91099843f11a1af2a7e58f9386ca1678a055e907413db31b67c7fc647c471bf` |
| ROM | `9fb8af3aae2479bcb86c7191f5d224e4e9e811b6e5664d88c0245e3f50bca332` |
| Map | `99032aac1566d69fa2cb826bb067a4bdd8d171b134b6c768ca03e7fb9b2e4067` |

Remaps and stage runs:
- The N6→N7 remaps of the v3, prefix, SC and 20-site lists report no SHIFT or MISMATCH.
- `remapres2.py` now also masks an immediate already masked to `X` (`-X`). That relocated `%lo` previously caused one false MISMATCH.
- The stage GEN run and an independent fresh-scratch run both end with VERIFY PASSED (77 OK).

| Artifact | Result on N7 |
|---|---|
| Handler table | `0x8008b7d4`. |
| Stadium archive | `0x595eb0`. |
| Geo size table | `0x80098660`. |
| Normalised outputs | Identical to N6 for frames, routes, `renderrec`, `cmd8weight`, scenechain/domain, SC 2832 and `indirect20` 5248. Stadium weights are identical modulo the archive address. |
| `cmd8depth` | `func_81000420` stays 1024 (64 functions, including the helper). |
| `cmd8scan-v2.py` | 622194 candidates, **0 hits**. |

**Lower-pool static key 1:**
- gPool is at `0x8012e130`.
- The pool is `[0x8012e140, 0x803ffff0)`, capacity **2956976** (−352 versus N6).
- fragment62 still charges 815648 at ROM `[0x310970, 0x39fe40)`.
- Arithmetic-only remainder: 548000 − 352 = **547648**. This is not a fit claim.

**Still open after v10:**
- Deferred items:
  - A: recursive borrowed G_DL with inherited segment state;
  - C: alternate-image/flag-4 last-task lifetime;
  - E: synthesised or replaced streams and writers.
- `14D70` segment-1 shadow targets.
- cmdA/cmd17 payloads and the remaining callback argument schemas.
- Effect whole-turn history.
- Stadium 144 / BD9C 592; the two fragment43 sites.
- Lower keys 2–6; co-occurrence, fit/contiguity and native/gate.

## v11 fragment-31 relocation regeneration, sticky shadow refusal; N8 refresh (static)

All changes are behind `ROGUELIKE`. Vanilla `make` still reports `build/pokestadium-us.z64: OK`.

**Corrections to v9/v10 (N5–N7).**
- **fragment-31 bindings were stale.** From `ae9115b` onward the fragment-31 layout changed but its relocation table, header and raw `j` thunk table were still the vanilla ones:
  - The 60-entry thunk table at offset `0x20–0x200` kept vanilla targets. 21 admitted local `0x810` thunks pointed at old offsets inside other functions (e.g. `81000180` → `81002f38`, but `func_81002F38` was at `81003090`).
  - `rl_relocate_archive_imports` repairs only `[0x84300000, 0x84400000)` targets. Given a correct relocation table it would map the 30 admitted `0x843` imports and nothing else; on N5–N7 the stale header meant no such table was read (see v12). `Memmap_RelocateFragment` rebases without changing offsets.
  - The header still held vanilla `relocOffset/sizeInRom/sizeInRam` = `0x47e0/0x5480/0x4be0` against an actual layout of `0x4a70/0x5710/0x4e70`. Both loaders read `relocOffset`, so N5–N7 would read a relocation count from code bytes.
  - The direct-function HOST and IDO acceptance in v9/v10 (CI helper, getter, phase/counter) therefore did **not** certify the installed callback-table path on N5–N7.
- **Wmax 5208 for N6/N7 was stale.** `cmd8weight.py` used a 10⁶ placeholder for an unbound callback, so the N7 output was 1002776, an unknown depth, and `cmd8domain.py` printed 21 `NO-FUNC-SYMBOL-AT-ADDR` lines. Verification passed only because the stage compared output identity.
- **CI texture-owned extent is withdrawn.** The v10 witness (no relocation slot/target, one HI/LO target, a heuristic Gfx walk that stops on unlisted opcodes) does not establish object ownership or exhaust reachable segmented operands. Only the initialized 144008-byte window with worst end 144000 and the palette barrier remain accepted. Closing ownership needs a positive asset object size/format proof or an exhaustive root-qualified consumer decode.

**Commits since N7 (`cf0f6d3`):**

| Commit | Change |
|---|---|
| `304a8e1` | Sticky shadow refusal. An out-of-domain `(u16)counter >= 3` latches `rl_shadow_dead[row]`, and a dead row refuses every later admission until `func_80032670` resets the counter and calls `rl_shadow_slot_reset(row)`. A non-latched s16 counter could previously wrap from 3 to 0 after 65533 mode-2 refusals and admit a stale self-marked slot (UI review). |
| `82fde6d` | `RL_REGEN_FRAGMENTS` defaults to `62 31`. fragment-31's relocation table and header are regenerated from the linked ELF. Each raw local/foreign `j` thunk is rebound to the linked address of the vanilla function its vanilla word targeted. `0x843` imports are left raw for the existing runtime repair (no double repair). `check_native_thunks.py` fails the build on any unbound thunk. |
| `26acadc` | `check_installed_thunks.py` runs on the final ROM. It requires the header to match the map, simulates the exact `rl_relocate_archive_imports` rule, derives each expected target independently (vanilla ROM word → vanilla function → linked address), and checks the opcode and the loader R26 entry. |

HOST evidence for the shadow latch (`host_tests/test_shadow_slot.py`):
- The 1437 in-domain lines are identical, with 66 refusals.
- The 65533-refusal wrap fixture now stays refused until reset, and reset re-admits.
- Mutants removing the latch or the reset are rejected.

IDO evidence on N8:
- `rl_shadow_slot_reset` @ `80033840` clears `rl_shadow_dead[row & 1]`. The static helper follows it @ `80033854`, with `sltiu row<2`, `andi 0xffff`/`slti 3` and a latching store, the dead check, then `lw 0x130` versus `+0x138`.
- `func_80032670` @ `800335e0` executes `sh zero` to the counter and then `jal rl_shadow_slot_reset`.
- Reachability of counter ≥ 3 and the reset cadence are still not asserted.

**N8 candidate:** fixed point v102 = v103 (v101 changed).

| Artifact | SHA256 |
|---|---|
| ELF | `86b4f537c5217b9bf3c8f06b4e7828c521d1d0bd5d673f3e5ebb1a72ba55705b` |
| ROM | `f227d4c4db14af67352c03204dc1762c4ec8e06ad0f6fe7349ed720e5f41c5f1` |
| Map | `957064ccd5fab3afcf8938ab03a29de818c9989cdb2598dd497b608dce68b34d` |

The fragment-31 header is now `0x4a70/0x5740/0x4e70`, matching the map.

Installed bindings:
- `check_installed_thunks.py` on N8: **60 bindings PASS** (24 local, 32 import, 4 foreign), including all **56** operands from the validator's post-repair witness.
- On N7 it rejects the stale header; `check_native_thunks.py` on N7 rejects the stale local thunks.
- Mutants rejected: stale local word `08400bce`, opcode swap, vanilla header, dropped loader entry, unmapped `0x843`, pre-mapped `0x843` (double repair).

Stage tools now fail closed:
- `cmd8weight.py` no longer uses a placeholder. Any unbound callback or callback without a computed depth makes the run exit 1 with `Wmax UNBOUNDED` and lists it.
- `cmd8domain.py` exits 1 unless every admitted operand binds to a function symbol after the import repair.
- On N7 both reject: 21 unresolved callbacks across 28 layouts.

Remaps and stage runs:
- The N7→N8 remaps of the v3, prefix, SC and 20-site lists report no SHIFT or MISMATCH.
- `remapres2.py` now masks hex immediates. The fragment43 ROM `%hi` (`lui 0x28/0x29`) previously caused a false MISMATCH.
- The stage GEN run and an independent fresh-scratch run both end with VERIFY PASSED (101 OK).

| Artifact | Result on N8 |
|---|---|
| Handler table | `0x8008b824`. |
| Stadium archive | `0x595f30`. |
| Geo size table | `0x800986b0`. |
| `cmd8domain.py` | 56 operands, 0 anomalies, **0 unresolved** bindings. |
| `cmd8weight.py` | Wmax **5208** with all 56 callbacks bound (0 unresolved). Stadium archive: Wmax 648 (18 layouts). This is the first refresh in which 5208 is a real bound rather than a masked placeholder. |
| `renderrec.py` | CBmax **1144**, terminal 1184. |
| `cmd8depth` | `func_81000420` stays 1024 (64 functions). |
| `cmd8scan-v2.py` | 622260 candidates, **0 hits** (4 and 8 MiB). |
| SC closure | `sc_cold_entry` **2832** (594 functions). |
| Other normalised outputs | Identical to N7 apart from addresses and `anon_` names. `cmd8super` lists 2 more superset candidates from the new code words. |

**Lower-pool static key 1:**
- gPool is at `0x8012e190`.
- The pool is `[0x8012e1a0, 0x803ffff0)`, capacity **2956880** (−96 versus N7).
- fragment62 still charges 815648 at ROM `[0x3109f0, 0x39fec0)`.
- Arithmetic-only remainder: 547648 − 96 = **547552**. This is not a fit claim.

**Still open after v11:**
- CI texture ownership (see the corrections above).
- Deferred items A (recursive borrowed G_DL with inherited segment state), C (alternate-image/flag-4 last-task lifetime) and E (synthesised or replaced streams and writers).
- `14D70` segment-1 shadow targets.
- cmdA/cmd17 payloads and the remaining callback argument schemas.
- Effect whole-turn history; Stadium 144 / BD9C 592; the two fragment43 sites.
- Lower keys 2–6; co-occurrence, fit/contiguity and native/gate.

## v12 fragment-31 entry gate, 14D70 segment-1 admission; N9 refresh (static)

All changes are behind `ROGUELIKE`. Vanilla `make` still reports `build/pokestadium-us.z64: OK`.

**Correction to v11 (N7 history).** The 30 admitted `0x843` imports were mapped correctly only *conditionally on a
correct relocation table*. On N5–N7 the stale header made the loader read relocations from offset `0x47e0`, so the
installed entry dispatched to `81001b78` (validator). The import-map trace is therefore not N7 runtime success.

N8 installed repair was accepted independently (validator `n8-installed-review.txt` SHA
`ec92885012831f045ef20af36df7e1cd1c8c9c288573e23a0876b84b91e0745e`). This is a bounded binding component, not a stack, fit or native claim.

| Commit | Change |
|---|---|
| `d597b42` | `check_installed_thunks.py` also gates the entry path. The header bytes `0..0x14` must equal vanilla: `j BASE+0x20`, nop, `FRAGMENT`. The loader R26 entry at offset 0 must be present iff vanilla has one. The stub at `0x20` must be a checked binding. N9: `entry j 81000020 -> stub -> func_810002DC PASS`. Five header/stub mutants are rejected. |
| `16aad3b` | `func_80014D70` admits the shadow node only with a non-NULL `func_80005F5C(16)` DL, `Memmap_GetSegmentBaseVaddr(1) != 0` and `Memmap_GetSegmentSize(1) >= 0x2590`; refusal follows the vanilla flag-off path. `0x2590` covers both shadow DLs (`0x2480`/`0x2508`, `0x88` each), textures `0x2000–0x2400` and vertices `0x2400–0x2440`. The genuine install is `0x3f20`. Sizes `0x2508–0x258F` would be refused for branch 0 where vanilla accepts; this is a documented conservative difference. Segment-1 identity is not proven, because some non-constant installers remain unresolved; this is an extent check only. |

HOST evidence (`host_tests/test_shadow_seg1.py`, extracted `func_80014D70` + memmap, `-m32` ASan/UBSan):
- The installed, exact-`0x2590` and flag-off traces are identical to vanilla.
- Unmapped, `0x258F`, cleared, NULL-base and NULL-alloc are refused with the flag-off trace; vanilla faults on them.
- Mutants removing the NULL, base or bound check are rejected.

IDO evidence on N9:
- `rl_shadow_node_ok` @ `80015a20` checks `sltu a0`, then `Memmap_GetSegmentBaseVaddr(1)`, then `Memmap_GetSegmentSize(1)` (`lw gSegments+4`) with `sltiu 0x2590`.
- In `func_80014D70` the call sits after `andi 0x40` and before the `10A0` increment, and `beqz` goes to the epilogue at `80015d88`.

**N9 candidate:** fixed point v106 = v107.

| Artifact | SHA256 |
|---|---|
| ELF | `3c62f5d3cb2b61da6de7241ef8bc0b67012ce2c8e2df0509a12db4813de68ba4` |
| ROM | `621e97a193841a397d1a9119bcbae770f7761770d051e938c902ee7b8cdd8a9b` |
| Map | `e9a89606324b6cdd3b6edba9c22e1ecbe134169e0d9b9d5c71a850c411965536` |

`.text` after `12D80.o` moved +0x70. Handler table `0x8008b894`, stadium archive `0x595fa0`, geo size table `0x80098720`, gPool `0x8012e200`.

Remaps and stage runs:
- The N8→N9 remaps of the v3, prefix, SC and 20-site lists report no SHIFT or MISMATCH.
- `remapres2.py` now applies the decimal-immediate mask before the 5-digit hex mask. Previously `addiu …,10080` became `X`, giving a false MISMATCH.
- GEN and a fresh-scratch run both end with VERIFY PASSED (108 OK).

| Artifact | Result on N9 |
|---|---|
| Installed thunks | 60 bindings PASS, all 56 validator operands, entry PASS. N7 is rejected. |
| `cmd8domain.py` / `cmd8weight.py` | 0 unresolved; Wmax **5208**; stadium 648. |
| `renderrec` / `cmd8depth` / SC | CBmax 1144; `func_81000420` 1024; `sc_cold_entry` 2832 (594 functions). Normalised output is identical to N8. |
| `cmd8scan-v2.py` | 622618 candidates, **2 hits** (N8: 0). Both are the phase-2 data word `00016001` in `.pokemon_models` (ELF `+73c2c6`, ROM `+105c2c6`). Its phys resolution `0x80016001` now falls inside the range of `func_80015220`, which shifted by +0x68. A derived check requires zero 4-aligned hits and zero entry hits. It passes, and both mutants are rejected. Exclusion of `func_80015220` now additionally rests on the premise that a misaligned `jalr` target faults before executing any instruction in the range. |
| `cmd8super` | Superset heuristic: 1192 candidates (N8: 1184) from shifted coincidental words. |

**Lower-pool static key 1:**
- The pool is `[0x8012e210, 0x803ffff0)`, capacity **2956768** (−112 versus N8).
- fragment62 charges 815648.
- Arithmetic-only remainder: **547440**. This is not a fit claim.

**Still open after v12:**
- CI texture ownership (the initialized window only).
- Deferred A, C and E.
- cmdA/cmd17: per-producer arena epoch versus live child attachments, or checked handler admission (validator `n8-cmda17-review.txt`).
- Effect whole-turn history; Stadium 144 / BD9C 592; the two fragment43 sites.
- Keys 2–6 phase placement and contiguity; native/gate.

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
