# Roguelike build (W0)

## Targets

| Command | Result |
|---|---|
| `make` | Vanilla build, unchanged. Must print `build/pokestadium-us.z64: OK` (md5 `ed1378bc12115f71209a77844965ba50`). |
| `make rl` | Builds vanilla first (its map drives the RL layout), then `ROGUELIKE=1 COMPARE=0` into `build_rl/`. Produces `build_rl/pokestadium-rl.z64` with fixed header CRCs. |
| `make rlclean` | Deletes `build_rl/`. |
| `tools/rl/boot_test.py` | Headless smoke test using W1's `tools/rltest` frontend and a mupen64plus_next libretro core (`RL_CORE=` overrides the path). It goes title → Menu Select → RL scene → B → Menu Select, three times. |
| `scripts/rl-gate.sh` | Runs the vanilla match, `make rl`, the RL boot test (when `tools/rltest` exists), host tests and smoke tests. |

`build_rl/` is git-ignored. Objects don't depend on CFLAGS, so `make rlclean` after changing
build flags.

### NON_MATCHING

`make rl` uses the **matching asm bodies** (`RL_NON_MATCHING ?= 0`). The decomp's
`NON_MATCHING` C bodies don't link: for example, `3D140.c` references undefined
`D_8007CB80`…`D_8007CB90`, and `fragment2.c` references `D_800A6CF4`. `make rl RL_NON_MATCHING=1` is
available once those are fixed upstream. Because `COMPARE=0`, neither choice affects the vanilla ROM.

## What `ROGUELIKE=1` changes

- `-DROGUELIKE` is defined.
  - `-DRL_HAVE_UI_DEMO` is added when `src/fragments/rl/rl_ui.c` exists (W4).
  - `-DRL_HAVE_BATTLE` is added when `src/roguelike/rl_battle.c` exists (W3).
- `src/roguelike/**` and `src/fragments/rl/**` are compiled. The vanilla build filters them out.
- `include/variables.h` adds `STATE_ROGUELIKE = 0x30` and `STATE_ROGUELIKE_BATTLE_TEST = 0x31`.
- `src/29BA0.c` adds the following, all under `#ifdef ROGUELIKE`:
  - dispatcher cases for 0x30 and 0x31, which call `rl_main_state_roguelike()` and `rl_main_state_battle_test()`;
  - the Menu Select hook `rl_main_menu_hook()`.
- Linking uses a generated linker script (`tools/rl/rl.mk`, `tools/rl/gen_rl_ld.py`) instead of
  `linker_scripts/us/pokestadium.ld`.

## Entering roguelike mode

On Menu Select, **hold L and confirm any entry with A**. Keep L held until the menu
finishes fading out, because the hook runs when fragment 57 returns. B on the RL screen returns
to Menu Select, and re-entry works. In the libretro harness, RetroPad B is N64 A and RetroPad Y is N64 B.

Fragment 57 starts with the Game Pak Check screen, so one A press is needed first.

## Layout and how growth is made safe

### Main segment (resident)

Main is VRAM 0x80000400 and ROM 0x1000. IPL3 (CIC-6103) copies 1 MB, and main is about 0x7EA00, so it has
plenty of room. New resident code from `src/roguelike/*` is appended to the end of main
`.text`/`.data`/`.rodata`/`.bss`.

- **Anchoring.** Splat's `undefined_syms*.ld` gives absolute addresses for many main
  symbols (data referenced from asm, jump tables, …). In the RL script, `gen_rl_ld.py` puts an
  anchor `__rla_N = ALIGN(16);` before every main/heap input section, taken from the vanilla map. It then rewrites
  every absolute symbol in [0x80000400, heap end) as `__rla_N + offset`, so those symbols move with
  their section. `check_rl_anchors.py` verifies after linking that every anchor equals its section's
  real address (currently 523 anchors).
- **Entry/bss.** Vanilla `entry.s` hardcodes the bss clear length (0x85E30). The RL link
  replaces it with `src/roguelike/rl_entry.s`, which clears `text_BSS_SIZE` from the linker. The entry stays
  0x60 bytes.
- **Heap.** `gPool` follows main bss, and `main_pool_init(&gPool, POOL_END_6MB)` uses a fixed end,
  so main growth only shrinks the pool. `heap_VRAM_END` in the map passes 0x80600000, but that's
  only the nominal array size and has no runtime effect.
- **Bad disassembly.** `asm/us/49190.s` decodes data words as `beql …, .L8006873C` at
  0x80048738/0x8004873C. Once main grows, that branch is out of PC16 range. `tools/rl/asm_fixups.py`
  emits those instructions as raw `.word` values in the RL build only. Add more sites there if needed.

### Fragments (overlays)

Format (`src/memmap.h`):
- a 0x20-byte header (`j entry; nop; "FRAGMENT"; 0x20; relocOffset; sizeInRom;
  sizeInRam`), then the code/data, then a reloc table (`count`, then `(type<<24)|offset` words) right
  after it in ROM;
- `relocOffset` is the size of the fragment's progbits, and `sizeInRam` = relocOffset + bss;
- the loader (`Memmap_RelocateFragment`) rewrites only values in 0x81000000–0x8FFFFFFF, using fragment
  ID = `((vram & 0x0FF00000) >> 20) - 0x10`, which must be < 240;
- the table is zeroed after relocation, and that zeroing also clears bss.

`fragmentRL` uses VRAM **0x8F800000** (ID 232, unused). Its sources are `src/fragments/rl/*.c`/`.s`;
`rl_header.s` must stay first. Relocations come from a **two-pass link**:
1. Pass 1 links with an empty table and `--emit-relocs`.
2. `gen_rl_relocs.py` collects every REL entry for `.fragmentRL`, in ELF order, because HI16/LO16 pairing is order-dependent.
3. The final link includes the table, and the build checks that a re-generation matches.

Fragment bss is re-zeroed on every load. Persistent state belongs in resident `src/roguelike/`.

**Editing an existing native fragment** changes its layout. Set `RL_REGEN_FRAGMENTS` to the
space-separated fragment numbers that need regenerated headers and relocations (default: `62`):
`make rl RL_REGEN_FRAGMENTS="62 63"`. Empty disables native regeneration.

The generated header preserves the jump/nop/`FRAGMENT` format, but resolves the entry function
through its vanilla map symbol: a literal jump would become stale if code before the entry grows.
The linker computes relocation offset, ROM size, and RAM size. Native input sections receive
anchors, including BSS, so absolute symbols also follow their new positions.

`gen_rl_relocs.py --section .fragmentN` collects code and symbolic data relocations from the
pass-one ELF. It also retains original raw data-pointer entries missing from ELF relocation
sections, remapping their locations and final pointer values with the input-section anchors.
Tables are padded to 16 bytes and checked for stability after the final link. The original
table's bit31 is ignored by the loader and is normalized during comparison.

Run `python3 -m unittest tools.rl.test_native_relocs` after a vanilla build to compare all
27,903 fragment62 entries against the original unmodified table. This proves table parity,
not a modified battle's runtime behavior. The fallback cannot infer arbitrary layout changes
inside an input section containing raw pointers; convert those pointers to named symbols
before changing that section's internal layout.

### ROM

Fragments and assets after main shift by the growth. Loads use linker symbols
(`*_ROM_START`/`*_ROM_END`), so they follow automatically. The GB Tower ROM block stays hard-aligned at
**0x7C0000**. Vanilla pads with `ALIGN(__romPos, 0x7C0000)`. The RL script instead asserts
`__romPos <= 0x7C0000` and then sets it to 0x7C0000. Current headroom is 0x24140 bytes (about 144 KB) for main +
fragment growth. Beyond that, data would have to move after the GB block.

Hardcoded ROM/RAM assumptions checked while shifting:
- the entry bss size (fixed above);
- the copy-protection check reads ROM 0xE38 (`0x828A`), which is inside IPL3 and unchanged;
- `POOL_START` is only used as an array size;
- the ROM offsets 0x7C0000 and 0x898000 are preserved by alignment.

### CRC

`tools/rl/n64crc.py` detects the CIC from the IPL3 (6103 for this ROM) and rewrites CRC1/CRC2.
`--check` verifies them, and `make rl` runs the check.

## Adding code from other workstreams

- **Resident code:** drop `.c`/`.s` files into `src/roguelike/`. They're linked into main automatically.
  - Pure host-testable files must not include libultra headers.
  - W5 owns `rl_core.{c,h}`. W0's glue is `rl_main.{c,h}`.
- **Overlay code:** drop files into `src/fragments/rl/`. If `rl_ui.c` exists, `rl_fragment.c` uses the W4
  demo API:
  - init runs after the display context is created;
  - update runs after the controller read, and the overlay exits on -2/-3;
  - draw runs between `func_800079C4` and `func_80007778`, after a 640x480 ortho is set up every frame.
- **Battle test (state 0x31):** define `void rl_battle_test_entry(void)` in `src/roguelike/rl_battle.c`.
  It must set `gCurrentGameState`.
- **Extra make rules** (generated tables etc.): put a `src/roguelike/*.mk` or `src/fragments/rl/*.mk`
  in place. It's included only when `ROGUELIKE=1`. Write outputs under `$(BUILD_DIR)` (= `build_rl`).
- **Debug:** `gRlMain` (resident) has these fields:
  - `magic` = 'RLM0';
  - `visits`;
  - `lastResult`;
  - `returnState`;
  - `frames` (overlay frames drawn).

  Use the map file to find its address.
