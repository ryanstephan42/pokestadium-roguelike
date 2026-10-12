#!/usr/bin/env bash
set -euo pipefail
out="${1:?fresh output directory}"
test ! -e "$out"
mkdir -p "$out"
python tools/story_data.py --moon-stones --out "$out/data.json" --c-out "$out/sc_data.c"
sources=(host_tests/test_story_native_operation.c src/story/sc_native_operation.c
    src/story/sc_ui.c src/story/sc_ui_driver.c src/story/sc_core.c src/story/sc_combat.c
    src/story/sc_make.c src/story/sc_graph.c src/story/sc_progression.c src/story/sc_save.c
    src/story/sc_crc.c src/story/sc_storage.c src/story/sc_bridge.c
    src/roguelike/rl_ui_model.c tools/rltest/story_six_fixture.c "$out/sc_data.c")
flags=(-std=c89 -pedantic -Wall -Wextra -Werror -Wno-endif-labels -I. -iquote include
    -DROGUELIKE -DSTORY_NATIVE_UI
    -include host_tests/story_native_operation_stubs.h)
for mode in host san; do
    extra=(-O2)
    if [[ "$mode" == san ]]; then
        extra=(-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer)
    fi
    gcc "${flags[@]}" "${extra[@]}" "${sources[@]}" -o "$out/operation-$mode"
    UBSAN_OPTIONS=halt_on_error=1 ASAN_OPTIONS=detect_leaks=1 \
        "$out/operation-$mode" | tee "$out/$mode.log"
done
tools/ido/linux/7.1/cc -c -O2 -mips2 -G 0 -non_shared -Xcpluscomm -nostdinc \
    -DSTORY_NATIVE_UI -D_LANGUAGE_C -D_MIPS_SZLONG=32 -DF3DEX_GBI_2 \
    -Iinclude -Isrc -I. -Ibuild -Iassets/us -Ilib/ultralib/include \
    -Ilib/ultralib/include/PR -Ilib/ultralib/include/ido src/story/sc_native_operation.c \
    -o "$out/sc_native_operation.o" > "$out/ido.log" 2>&1
mips-linux-gnu-objdump -dr "$out/sc_native_operation.o" > "$out/operation-disassembly.txt"
