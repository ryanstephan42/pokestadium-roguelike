#!/usr/bin/env bash
set -euo pipefail
out="${1:?fresh output}"
test ! -e "$out"
mkdir -p "$out"
flags=(-DSTORY_NATIVE_UI -DSC_NATIVE_MEMORY_HOST -DROGUELIKE -std=gnu89
    -Wall -Wextra -Werror -Wno-endif-labels -I. -iquote include
    -include host_tests/story_native_memory_stubs.h)
for mode in host san; do
    extra=()
    if [[ "$mode" == san ]]; then
        extra=(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
    fi
    gcc "${flags[@]}" "${extra[@]}" -O2 src/story/sc_native_memory.c \
        src/story/sc_module_arena.c host_tests/test_story_native_memory.c \
        -o "$out/memory-$mode"
    ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
        "$out/memory-$mode" | tee "$out/memory-$mode.log"
done
target=(-c -G 0 -non_shared -Xcpluscomm -nostdinc -Wab,-r4300_mul
    -DVERSION_US=1 -DSTORY_NATIVE_UI -DROGUELIKE
    -Iinclude -Isrc -Isrc/libnaudio -Iassets/us -I. -Ibuild
    -Ilib/ultralib/include -Ilib/ultralib/include/PR -Ilib/ultralib/include/ido
    -fullwarn -woff 624,649,838,712,516,513,596,564,594
    -mips2 -EB -D_MIPS_SZLONG=32 -DNDEBUG -D_FINALROM -DN_MICRO -DF3DEX_GBI_2
    -DBUILD_VERSION=VERSION_I_P -DLANGUAGE_C -D_LANGUAGE_C -O2)
tools/ido/linux/7.1/cc "${target[@]}" src/story/sc_native_memory.c \
    -o "$out/sc_native_memory.o" > "$out/ido.log" 2>&1
mips-linux-gnu-objdump -dr "$out/sc_native_memory.o" > "$out/disassembly.txt"
mips-linux-gnu-size -A "$out/sc_native_memory.o" > "$out/sizes.txt"
