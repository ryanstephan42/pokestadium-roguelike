#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
arena_dir=${SC_MA_SOURCE_DIR:-src/story}
test -f "$arena_dir/sc_module_arena.c" || {
    echo "Missing tracked campaign arena; set SC_MA_SOURCE_DIR to its qualified source directory" >&2
    exit 1
}
output=.local/rltest/story-upper-resources
mkdir -p "$output/temp"
export TMPDIR="$PWD/$output/temp"
sources=(host_tests/test_story_upper_resources.c src/story/sc_native_resources.c
    src/story/sc_native_font.c src/story/sc_native_icons.c src/story/sc_resource_arena.c
    "$arena_dir/sc_module_arena.c")
flags=(-m32 -DSTORY_NATIVE_UI -std=gnu89 -Wall -Wextra -Werror
    -Wno-int-conversion -Wno-endif-labels -I. -iquote include -I"$arena_dir"
    -include host_tests/story_upper_resource_stubs.h)
"${CC:-gcc}" "${flags[@]}" -O2 "${sources[@]}" -o "$output/test"
"$output/test"
"${CC:-gcc}" "${flags[@]}" -O1 -g -fsanitize=address,undefined \
    -fno-omit-frame-pointer "${sources[@]}" -o "$output/test-san"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$output/test-san"
"${CC:-gcc}" -DSTORY_NATIVE_UI -std=c89 -Wall -Wextra -Werror -I. -I"$arena_dir" \
    host_tests/test_story_resource_arena.c src/story/sc_resource_arena.c \
    "$arena_dir/sc_module_arena.c" -o "$output/arena-test"
"$output/arena-test"
target_flags=(-c -G 0 -non_shared -Xcpluscomm -nostdinc -Wab,-r4300_mul
    -DVERSION_US=1 -DSTORY_NATIVE_UI -DSTORY_NATIVE_PROBE -DROGUELIKE
    -Iinclude -Isrc -Isrc/libnaudio -Iassets/us -I. -Ibuild -I"$arena_dir"
    -I"$arena_dir/../.." -Ilib/ultralib/include -Ilib/ultralib/include/PR
    -Ilib/ultralib/include/ido -fullwarn -woff 624,649,838,712,516,513,596,564,594
    -mips2 -EB -D_MIPS_SZLONG=32 -DNDEBUG -D_FINALROM -DN_MICRO -DF3DEX_GBI_2
    -DBUILD_VERSION=VERSION_I_P -DLANGUAGE_C -D_LANGUAGE_C -O2)
for unit in sc_resource_arena sc_native_resources sc_native_font sc_native_ui sc_native_icons sc_native_loop; do
    tools/ido/linux/7.1/cc "${target_flags[@]}" "src/story/$unit.c" -o "$output/$unit.o"
done
tools/ido/linux/7.1/cc "${target_flags[@]}" src/fragments/rl/rl_ui.c -o "$output/rl_ui.o"
echo "Upper resource HOST, ASan/UBSan and IDO checks: PASS (no ROM/native execution)"
