# Isolated visible preview (W12)

```sh
make rl
scripts/rl-preview.sh
```

Requires Linux RetroArch, X11/XWayland (`DISPLAY`), and Mupen64Plus-Next with
Angrylion and Parallel RSP. The default core is
`/usr/lib/libretro/mupen64plus_next_libretro.so`; set `RL_PREVIEW_CORE` for a
different installed Mupen64Plus-Next library. All configuration, core options,
SRAM/Controller Pak data, states, logs, caches and implicit RetroArch config
paths are isolated under `.local/rltest/preview/`. The script never copies or
edits the user's global config or existing saves. It regenerates its own
config/options on launch but preserves its local saves.

`config_directory`, `core_options_path`, `global_core_options`, disabled
automatic overrides/remaps/shaders, and an isolated `XDG_CONFIG_HOME` are all
important: merely passing `--config` does not isolate per-core options or
implicit playlist paths. The launcher uses X11/glx deliberately, including on
a Wayland desktop, so the keyboard driver and video window use the same
context. A native-Wayland experiment with the X keyboard driver did not accept
Start correctly; a subsequent native-Wayland-driver experiment failed inside
libdecor/GTK at startup. Neither is a ROM fix.

## Controls

| N64 action | Keyboard | SDL2 standard Xbox pad |
|---|---|---|
| Start | Enter | Menu/Start |
| A / confirm | Z | A |
| B / back | X | X |
| L | Q | LB |
| R | E | RB |
| Z trigger | A | LT |
| D-pad / menu navigation | Arrow keys | Left stick |
| C-up/down/left/right | I / K / J / L | D-pad up/down/left/right |

Wait for **PRESS START**, then Enter, Z at Game Pak Check, and **hold Q while
pressing Z** in Menu Select. Keep Q held until the fade completes. Z starts a
run; select three starters and confirm Begin Run; select a route, then Start
Battle in the active-party list. In battle Z opens Battle, then a C direction
chooses its corresponding move. Escape closes the preview.

SDL2's built-in standard GameController fallback is intentional when no
installed autoconfig profile exists; "not configured, using fallback" in the
log does not mean the controller is unbound. Do not paste udev button numbers
into this SDL2 profile. `RL_PREVIEW_PAD=1 scripts/rl-preview.sh` selects another
SDL device index if needed; inspect the input/autoconfig lines in the local log.
The tested host enumerated Xbox first and the Keychron keyboard's joystick
interface second.

The Xbox D-pad supplies C-buttons through SDL2 standard buttons 11/12/13/14
(up/down/left/right). Left-stick axes -1/+1/-0/+0 supply N64 directional/menu
navigation. Ordinary D-pad bindings are disabled to avoid duplicate navigation;
all right-stick C-axis bindings are disabled. This replaces the earlier
right-stick vertical inversion workaround at the user's request. Keyboard
I/K/J/L, arrow keys and A/B/Start/L mappings remain unchanged. These settings
apply when `scripts/rl-preview.sh` next generates its local config; they do not
change the currently running preview or any global controller configuration.

### Physical-controller acceptance checklist

The Xbox device was readable and opened by RetroArch, but automated keyboard
tests do not establish that the user's physical button presses work. With the
game window focused and the RetroArch menu closed:

1. Release all triggers; press Menu/Start at the settled title.
2. Press Xbox A at Game Pak Check; use the left stick to change the Menu Select cursor.
3. Hold LB, press A, and keep LB held through the fade. Verify the RL menu appears.
4. Verify A selects and X backs out; draft three starters and start a battle.
5. Press A to open Battle; verify D-pad up/down/left/right choose the corresponding
   C-button moves. Verify an attack animation and changed HP/PP rather than just a
   cursor. The right stick must not select moves.

If a step fails, check pad selection in `.local/rltest/preview/retroarch.log`.
Observe `evtest /dev/input/event11` **without `--grab`** on the tested host while
pressing the relevant buttons. Other hosts must use their controller's event
device. Missing events indicate a hardware/connection problem; present events
but no game response indicate mapping, focus or frontend routing.

## Renderer and input evidence

Investigated on 2026-10-10 with RetroArch 1.22.2 (`69a4f0ea1e`) and installed
Mupen64Plus-Next 2.8 (`f275caf4`). The installed core SHA256 is
`1e5674de118b8cb3621a16402ecdd3d509591700c35e98943e8bb32906342263`.
The tested RL ROM SHA256 is
`ee1e434022246e52a0e8a05da02a35041381b1360c15a9b89c8e243ee0acf525`
(W7 `66e84cb` source). Its CIC-6103 CRC check passed, its 33,667,008-byte image
was padded to 64 MiB, and vanilla retained MD5
`ed1378bc12115f71209a77844965ba50`.

| ROM / isolated renderer | Observed result |
|---|---|
| Matching vanilla / GLideN64 + HLE RSP | Title correct; Start produces tiled, washed-out title art and gray/white output instead of Game Pak Check/menu |
| Matching vanilla / GLideN64 + Parallel RSP | Same post-Start corruption; changing RSP alone does not fix it |
| Matching vanilla / Angrylion + Parallel RSP | Correct title, Game Pak Check and Menu Select |
| RL / Angrylion + Parallel RSP | Correct title, Game Pak Check, Menu Select, RL menu, starter draft, map, native battle/attack/KO/switch rendering |

Thus the reported black/white corruption is reproducible in **byte-matching
vanilla**, before any roguelike entry. It is in the installed GLideN64
hardware-rendering path, not an RL display list, shifted fragment or ROM
CRC/padding failure. The precise upstream GLideN64/GPU defect is not established.
Angrylion presents a software framebuffer (XRGB8888, no `SET_HW_RENDER`);
GLideN64 requests a GL core hardware context. The launcher's fix selects the
verified software path; no original decomp code is changed.

The previous preview also disabled input autodetection and mixed udev/SDL
numbering: the Xbox udev device has buttons 0-10, so D-pad buttons 11-14 were
invalid (udev represents this D-pad as axes). Those bindings also placed
confirm/back on unexpected physical buttons. Device access and port ordering
were not the problem in that log. Its udev Start binding was valid, so the
earlier report of Start not working is not fully explained by those mappings.

Local-only captures are under:

- `.local/rltest/vanilla-gliden64/{gamepak,menu}.png` and
  `.local/rltest/vanilla-gliden64-parallel/gamepak.png`: reproduced corruption.
- `.local/rltest/vanilla-angrylion/menu-confirmed.png`: correct vanilla menu.
- `.local/rltest/preview/x-{gamepak,menu,rl,map,battle-start,battle-prompt}.png`:
  real visible keyboard navigation, not RAM-forced scene entry.
- `.local/rltest/preview/x-battle-after-command.png` and `x-switched.png`:
  Horsea HP changed from 102 to 34, Doduo fainted, and keyboard C-right selected
  Nidoking. The displayed close-up entrance/attack cameras are native animations.
- `.local/rltest/preview/samples-map/samples.json`: 40 window samples over
  82.9 seconds, no near-black/near-white central viewport.
- `.local/rltest/preview/samples-battle/samples.json`: 40 samples over 61.1
  seconds; one black native KO camera fade (sample 20), zero white frames.
  Samples 19/21 show the normal arena surrounding that fade, not repeated flashing.

These are window samples, **not every emulated frame**. Manual inspection of
the images complements the numeric checks; an isolated legitimate scene fade
is not evidence of the original repeated corruption.

For a stable-screen regression, with ImageMagick and xdotool installed:

```sh
xdotool search --onlyvisible --name RetroArch
python3 -m tools.rltest.preview_capture --window WINDOW_ID \
  --output .local/rltest/preview/samples --count 60
python3 -m unittest tools.rltest.tests.test_preview
```

The sampler flags a central viewport with over 98% nearly black or white
pixels and exits nonzero for inspection. Run it on a settled menu/battle
prompt; transition fades and legitimate battle effects can trigger it.
It cannot independently diagnose corruption or prove physical controller
actuation. Artifacts remain ignored; never commit game-output screenshots.

For controlled comparisons use separate profiles:

```sh
RL_PREVIEW_ROM=build/pokestadium-us.z64 \
RL_PREVIEW_PROFILE=.local/rltest/vanilla-preview scripts/rl-preview.sh
# Diagnostic only: known-bad renderer on the tested host.
RL_PREVIEW_RDP=gliden64 RL_PREVIEW_RSP=hle \
RL_PREVIEW_ROM=build/pokestadium-us.z64 \
RL_PREVIEW_PROFILE=.local/rltest/glide-preview scripts/rl-preview.sh
```
