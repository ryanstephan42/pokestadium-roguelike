#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
output=build/story/native-ui-review-v1
mkdir -p "$output"
python3 tools/story_data.py --out "$output/data.json" --c-out "$output/sc_data.c"
sources=(
    host_tests/test_story_native_resources.c src/story/sc_native_pak.c src/story/sc_native_ui.c
    src/story/sc_pak_probe.c
    src/story/sc_native_font.c src/story/sc_native_resources.c
    src/story/sc_native_icons.c
    src/story/sc_native_loop.c src/story/sc_native_operation.c src/story/sc_native_terminal.c
    src/story/sc_ui_driver.c src/story/sc_ui.c src/story/sc_core.c src/story/sc_combat.c
    src/story/sc_make.c src/story/sc_graph.c src/story/sc_progression.c src/story/sc_save.c
    src/story/sc_crc.c src/story/sc_storage.c src/story/sc_bridge.c
    src/roguelike/rl_ui_model.c src/roguelike/rl_pak.c "$output/sc_data.c"
)
flags=(-DSTORY_NATIVE_UI -DSTORY_NATIVE_PROBE -DSTORY_SERVICE_CANDIDATE -DROGUELIKE -std=gnu89 -Wall -Wextra -Werror
       -Wno-endif-labels -I. -iquote include -include host_tests/story_native_operation_stubs.h)
# Untouched native formatting uses 32-bit pointer arithmetic and decomp artifacts.
text_flags=(-Wno-int-to-pointer-cast -Wno-pointer-to-int-cast -Wno-int-conversion
    -Wno-pointer-sign -Wno-format -Wno-unused-but-set-variable -Wno-uninitialized)
"${CC:-gcc}" "${flags[@]}" "${text_flags[@]}" -O2 -c src/2E110.c -o "$output/text-host.o"
"${CC:-gcc}" "${flags[@]}" -O2 "${sources[@]}" "$output/text-host.o" -o "$output/test"
"$output/test"
"${CC:-gcc}" "${flags[@]}" "${text_flags[@]}" -O1 -g -fsanitize=address,undefined \
    -fno-omit-frame-pointer -c src/2E110.c -o "$output/text-san.o"
"${CC:-gcc}" "${flags[@]}" -O1 -g -fsanitize=address,undefined \
    -fno-omit-frame-pointer "${sources[@]}" "$output/text-san.o" -o "$output/test-san"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$output/test-san"
target_flags=(-c -G 0 -non_shared -Xcpluscomm -nostdinc -Wab,-r4300_mul
    -DVERSION_US=1 -DSTORY_NATIVE_UI -DSTORY_NATIVE_PROBE -DSTORY_SERVICE_CANDIDATE -DROGUELIKE
    -Iinclude -Isrc -Isrc/libnaudio -Iassets/us -I. -Ibuild
    -Ilib/ultralib/include -Ilib/ultralib/include/PR -Ilib/ultralib/include/ido
    -fullwarn -woff 624,649,838,712,516,513,596,564,594
    -mips2 -EB -D_MIPS_SZLONG=32 -DNDEBUG -D_FINALROM -DN_MICRO -DF3DEX_GBI_2
    -DBUILD_VERSION=VERSION_I_P -DLANGUAGE_C -D_LANGUAGE_C -O2)
for unit in sc_native_pak sc_native_ui sc_native_font sc_native_resources sc_native_icons sc_native_loop sc_native_operation sc_native_terminal sc_native_display sc_pak_probe; do
    tools/ido/linux/7.1/cc "${target_flags[@]}" "src/story/$unit.c" -o "$output/$unit.o"
done
tools/ido/linux/7.1/cc "${target_flags[@]}" src/2E110.c -o "$output/2E110.o"
tools/ido/linux/7.1/cc "${target_flags[@]}" src/6A40.c -o "$output/6A40.o"
tools/ido/linux/7.1/cc "${target_flags[@]}" src/6BC0.c -o "$output/6BC0.o"
tools/ido/linux/7.1/cc "${target_flags[@]}" src/stage_loader.c -o "$output/stage_loader.o"
tools/ido/linux/7.1/cc "${target_flags[@]}" src/fragments/rl/rl_ui.c -o "$output/rl_ui.o"
tools/ido/linux/7.1/cc "${target_flags[@]}" host_tests/story_native_sizes.c -o "$output/sizes.o"
mips-linux-gnu-objdump -d "$output/sizes.o" > "$output/sizes.txt"
