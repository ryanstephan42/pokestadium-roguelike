#!/usr/bin/env bash
# Keep all writable RetroArch state in this worktree.
set -euo pipefail
cd "$(dirname "$0")/.."

ROM="${RL_PREVIEW_ROM:-build_rl/pokestadium-rl.z64}"
CORE="${RL_PREVIEW_CORE:-/usr/lib/libretro/mupen64plus_next_libretro.so}"
PROFILE="${RL_PREVIEW_PROFILE:-.local/rltest/preview}"
RDP="${RL_PREVIEW_RDP:-angrylion}"
RSP="${RL_PREVIEW_RSP:-parallel}"
INPUT="${RL_PREVIEW_INPUT:-x}"
if [[ -z "${DISPLAY:-}" ]]; then
    echo "The preview requires X11 or XWayland (DISPLAY is unset)." >&2
    exit 1
fi
ROM="$(realpath "$ROM")"
CORE="$(realpath "$CORE")"
PROFILE="$(realpath -m "$PROFILE")"
if [[ ! -f "$ROM" || ! -f "$CORE" ]]; then
    echo "Missing ROM or Mupen64Plus-Next core; build with make rl or set RL_PREVIEW_CORE." >&2
    exit 1
fi
mkdir -p "$PROFILE"/{config,saves,states,system,screenshots,cache,xdg}

cat > "$PROFILE/core-options.cfg" <<EOF
mupen64plus-rdp-plugin = "$RDP"
mupen64plus-rsp-plugin = "$RSP"
mupen64plus-cpucore = "dynamic_recompiler"
mupen64plus-pak1 = "memory"
mupen64plus-pak2 = "none"
mupen64plus-pak3 = "none"
mupen64plus-pak4 = "none"
mupen64plus-alt-map = "False"
mupen64plus-aspect = "4:3"
mupen64plus-43screensize = "640x480"
mupen64plus-angrylion-vioverlay = "Filtered"
EOF
cat > "$PROFILE/retroarch.cfg" <<EOF
config_save_on_exit = "false"
config_directory = "$PROFILE/config"
core_options_path = "$PROFILE/core-options.cfg"
global_core_options = "true"
game_specific_options = "false"
auto_overrides_enable = "false"
auto_remaps_enable = "false"
auto_shaders_enable = "false"
savefile_directory = "$PROFILE/saves"
savestate_directory = "$PROFILE/states"
system_directory = "$PROFILE/system"
screenshot_directory = "$PROFILE/screenshots"
cache_directory = "$PROFILE/cache"
content_history_path = "$PROFILE/content_history.lpl"
content_favorites_path = "$PROFILE/content_favorites.lpl"
content_image_history_path = "$PROFILE/content_image_history.lpl"
content_music_history_path = "$PROFILE/content_music_history.lpl"
content_video_history_path = "$PROFILE/content_video_history.lpl"
history_list_enable = "false"
video_driver = "gl"
video_context_driver = "glx"
video_fullscreen = "false"
video_window_width = "960"
video_window_height = "720"
video_window_custom_size_enable = "true"
video_window_save_positions = "false"
video_shader_enable = "false"
video_threaded = "false"
pause_nonactive = "false"
input_driver = "$INPUT"
input_joypad_driver = "sdl2"
input_autodetect_enable = "true"
input_player1_joypad_index = "${RL_PREVIEW_PAD:-0}"
input_player1_b = "z"
input_player1_y = "x"
input_player1_start = "enter"
input_player1_l = "q"
input_player1_r = "e"
input_player1_l2 = "a"
input_player1_up = "up"
input_player1_down = "down"
input_player1_left = "left"
input_player1_right = "right"
input_player1_r_y_minus_axis = "+3"
input_player1_r_y_plus_axis = "-3"
input_player1_r_y_minus = "i"
input_player1_r_y_plus = "k"
input_player1_r_x_minus = "j"
input_player1_r_x_plus = "l"
input_exit_emulator = "escape"
input_enable_hotkey = "nul"
input_toggle_fast_forward = "nul"
input_toggle_fullscreen = "nul"
input_pause_toggle = "nul"
input_save_state = "nul"
input_load_state = "nul"
EOF
export XDG_CONFIG_HOME="$PROFILE/xdg"
export SDL_VIDEODRIVER=x11
unset WAYLAND_DISPLAY
exec retroarch --verbose --config "$PROFILE/retroarch.cfg" \
    --log-file "$PROFILE/retroarch.log" -L "$CORE" "$ROM" "$@"
