# Native UI and text kit (W4)

## Text font and encoding

The game font renderer is in `src/1CF30.c`; it is separate from the
debug-oriented `HAL_Printf`. `func_8001E94C(0x1C, 0)` initializes the font
context and loads the `fonts` archive. Mask 0x1C selects face 4 (16x16),
face 8 (24x20), and face 16 (24x24). The normal draw sequence is
`func_8001F3F4`, `func_8001EBE0(face, spacing)`, set RGBA with
`func_8001F324`, draw with `func_8001F1E8(x,y,fmt,...)`, and finish with
`func_8001F444`. `func_8001F5B0` measures text width.

Both printf paths format into 256-byte stack buffers. `rl_ui_text` uses
the same native glyph renderer directly through `func_8001ECB4`, one
unsigned encoded byte at a time; this supports literal percent characters
and avoids the fixed buffer limit. Newline advances one font line and
resets x. Keep strings in the game's encoding, not UTF-8.

`oldnotes/utils/txt2strtbl.c` documents/implements the text encoding:
ASCII passes through; yen A5, male A9, middle dot B7, female BE,
multiplication D7, and e-acute E9 are the supported UTF-8 conversions.
The comment lists degree BA, although no corresponding conversion is
implemented. `@` becomes newline. String-table data is a big-endian count,
then big-endian table-relative 32-bit offsets, NUL-terminated byte strings,
and FF padding to 16-byte alignment. In C, `"Pok\xe9" "mon"` preserves
the E9 byte without extending the hexadecimal escape.

## Panels, list, and menu rendering

Despite the preliminary pointer to `src/12D80.c`, the relevant 2D helpers
are in `src/1CF30.c`:

| Function | Rendering operation |
| --- | --- |
| `func_8001C330` | Clipped texture rectangle |
| `func_8001C604` | Primitive rectangle using caller's RDP state |
| `func_8001F504` | Self-contained translucent primitive rectangle |
| `func_8001C6AC` | RGBA16 texture |
| `func_8001C8C4` | RGBA32 texture |
| `func_8001CADC` / `8001CCF8` | IA8 / IA16 texture |
| `func_8001CF10` / `8001D12C` | I4 / I8 texture |

Fragment 38's `func_82300020` animates four IA8 cursor corners from
common_menu2_ui (segment-2 addresses 02000C80-02000F80).
`func_82300214` draws an eight-piece border; `func_8230047C` creates
tinted rounded I4 panels. `src/20470.c` has reusable rounded window
helpers that use common-menu texture assets. Standard menus load
common_menu1_ui in segment 1, common_menu2_ui in segment 2, and a
screen-specific UI archive (menu_select_ui, area_select_ui, or
rental_rules_ui) in segment 3. Those assets are not implicitly available
to a new fragment. W4 panels and cursor selection use the self-contained
`func_8001F504` rectangle to avoid undeclared asset dependencies.

`D_8006F518` / `D_8006F630` establish and restore the UI texture RDP state.
Sound IDs used by fragment 38 through `func_80048B90` are 1 for movement,
2 for confirm, 3 for cancel, and 0x1D for a rejected/disabled item.
The list model keeps cursor and scroll state in caller-owned `RlUiList`;
up/down move one item, left/right page by the visible row count, A returns
the enabled item index, and B returns `RL_UI_CANCEL`. Disabled rows remain
visible and navigable, but A returns `RL_UI_DISABLED`.

## Pokemon icons and sprites

`yamls/us/rom.yaml` defines `pokemon_icons` as a 152-entry multi-bin
asset: 40x40 RGBA16 textures, including the Substitute Doll. Legacy
`func_8001B058` / `func_8001B088` in `src/1AB70.c` load the archive using
hardcoded ROM address 0x820000 and DMA into caller memory. Fragment 38
draws icon buffers through `func_8001C6AC` with 40-pixel stride.

The RL UI opens the linked `pokemon_icons` archive with `ASSET_LOAD2` and
uses `func_8000484C(archive, species - 1)` to retrieve a cached icon. The
archive's entries must remain alive until the display list has executed;
therefore they are cached in the scene main-pool scope rather than
overwritten in a single scratch buffer. Species accepted by the helper
are 1 through 151, and the entry must report 3200 bytes before drawing.
Trainer portraits are distinct assets in `battle_portraits` (fragment 65
uses gfx_file_idx); 3D Pokemon previews use the model/offscreen pipeline
in `src/19840.c` and `src/1AB70.c`, not these 2D icons.

## Per-frame scene lifecycle

Fragment 37 entry `func_82202DC4` pushes a main-pool state, creates
display-list workspace with `func_80005E40(0x10000,0)`, and creates a
640x480 scene context with `func_80007444(1,0,2,0,2,1)`. It initializes
font and scene assets/data, then activates the scene using
`func_80007678`. The frame loop reads controller input, updates the menu
state, then calls a draw routine that begins with `func_800079C4`, sets
viewport/projection, appends screen drawing to `gDisplayListHead`, and
submits with `func_80007778`. It releases the scene with
`func_800076C0`, `func_80005EAC`, and pool pop.

Scene activation must be followed by `func_80006C6C(0xA)` to fade in.
Omitting that call leaves the framebuffer fully white even while the
list input and icon loading work. On exit, `func_80006CB4(0xA)` starts
fade-out; submit frames until `func_80007604() == 1` before teardown.

The RL overlay API is `rl_ui_demo_init`, `rl_ui_demo_update(pressed)`,
and `rl_ui_demo_draw`. Init runs inside the scene's pool after display
workspace/context setup; update follows controller polling; draw is called
between frame begin and submission. Demo uses a 10-entry list with 5
visible rows, a starter icon, and a sample HP bar. The icon cycles through
Bulbasaur, Charmander, and Squirtle as the cursor moves, so repeated frames
exercise cache reuse across multiple species.
The caller must establish the 640x480 orthographic viewport/projection each
frame before drawing; the UI kit appends 2D display-list commands but does
not own the scene's projection state.

## Integrated emulator validation

Run `make rl`, then `python3 -m tools.rltest.ui_demo`. The scenario uses
W1's `Frontend` API and the installed Mupen64Plus-Next core at
`/usr/lib/libretro/mupen64plus_next_libretro.so`, with Angrylion software
rendering. Screenshots and results go under ignored `.local/rltest/ui/`.
`--rom`, `--map`, `--core`, `--output`, and `--timeout` override those inputs.

Observed on the integrated W0/W4 ROM: native-font text (including E9),
window borders, highlighted rows, the disabled Move Tutor row, all three
starter icons, and a green 65/100 HP bar render correctly. The scenario
checks all ten cursor positions and scroll offsets, left/right paging,
repeated icon reuse, B exit to Menu Select, and a fresh cursor on re-entry.
A selects Mewtwo and displays its label; the locked row rejects A.

After all three species have been drawn, the cached icon pointers stay
unchanged through 48 cursor moves and another 120 frames per visit.
Main-pool available bytes are also unchanged, and agree across two visits,
confirming that teardown reclaims the scene allocations. The initial run
measured 1,489,744 available bytes; that value can change as resident code
or the overlay grows, so the scenario compares runtime values, not a
hardcoded address or allocation budget.

With W0's fade lifecycle and W1's smoke runner integrated,
`scripts/rl-gate.sh` completes every stage (vanilla matching ROM, RL build,
host UI tests, and vanilla emulator smoke) and prints `GATE PASSED`;
the separate UI scenario prints `UI DEMO PASSED`. No emulator stage was
skipped in that integrated validation.

Button names in `Frontend` are RetroPad names, not N64 names. On the
tested Mupen core, RetroPad B produces native A (8000), RetroPad Y
produces native B (4000), and L produces 0020. The scenario uses W1's
`N64_A` and `N64_B` aliases to express that intent. Hold L through the Menu
Select fade while confirming, until state 30 is reached. State 4 initially
contains Game Pak Check, which also needs native A before Menu Select is
interactive.
