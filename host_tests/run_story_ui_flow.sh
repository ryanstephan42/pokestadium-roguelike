#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
output=build/story/ui-flow-review-v1
mkdir -p "$output"
python3 tools/story_data.py --out "$output/data.json" --c-out "$output/sc_data.c"
sources=(
    host_tests/test_story_ui_flow.c src/story/sc_ui.c src/story/sc_core.c
    src/story/sc_combat.c src/story/sc_make.c src/story/sc_graph.c
    src/story/sc_progression.c src/story/sc_save.c src/story/sc_crc.c
    src/story/sc_storage.c src/story/sc_bridge.c src/roguelike/rl_ui_model.c
    src/story/sc_scene.c src/fragments/rl/rl_ui_mon.c "$output/sc_data.c"
)
flags=(-DROGUELIKE -std=c89 -pedantic -Wall -Wextra -Werror -I.
       -iquote include -Wno-endif-labels -include host_tests/scene_nodes_stubs.h)
"${CC:-gcc}" "${flags[@]}" -O2 "${sources[@]}" -o "$output/test"
"$output/test"
"${CC:-gcc}" "${flags[@]}" -O1 -g -fsanitize=address,undefined \
    -fno-omit-frame-pointer "${sources[@]}" -o "$output/test-san"
ASAN_OPTIONS=detect_leaks=1 "$output/test-san"
