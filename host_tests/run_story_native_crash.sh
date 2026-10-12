#!/usr/bin/env bash
set -euo pipefail
out="${1:?fresh output}"
test ! -e "$out"
mkdir -p "$out"
bash host_tests/run_story_native_retirement.sh > "$out/retirement.log" 2>&1
for mode in host san; do
    extra=()
    if [[ "$mode" == san ]]; then
        extra=(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
    fi
    gcc -DSTORY_NATIVE_UI -std=gnu89 -O2 -Wall -Wextra -Werror \
        -Wno-endif-labels -I. -iquote include \
        -include host_tests/story_native_crash_stubs.h "${extra[@]}" \
        src/story/sc_native_crash.c host_tests/test_story_native_crash.c \
        -o "$out/crash-$mode"
    ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
        "$out/crash-$mode" | tee "$out/crash-$mode.log"
done
for mode in host san; do
    extra=()
    if [[ "$mode" == san ]]; then
        extra=(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
    fi
    gcc -DSTORY_NATIVE_UI -DROGUELIKE -std=gnu89 -O2 -Wall -Wextra -Werror \
        -Wno-int-to-pointer-cast -Wno-pointer-to-int-cast -Wno-int-conversion \
        -Wno-unused-variable -Wno-endif-labels -I. -iquote include -iquote src \
        -include host_tests/story_native_stubs.h "${extra[@]}" \
        host_tests/test_story_native_crash_joined.c -o "$out/joined-$mode"
    ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
        "$out/joined-$mode" | tee "$out/joined-$mode.log"
done
target=(-c -G 0 -non_shared -Xcpluscomm -nostdinc -Wab,-r4300_mul
    -DVERSION_US=1 -DSTORY_NATIVE_UI -DROGUELIKE
    -Iinclude -Isrc -Isrc/libnaudio -Iassets/us -I. -Ibuild
    -Ilib/ultralib/include -Ilib/ultralib/include/PR -Ilib/ultralib/include/ido
    -fullwarn -woff 624,649,838,712,516,513,596,564,594
    -mips2 -EB -D_MIPS_SZLONG=32 -DNDEBUG -D_FINALROM -DN_MICRO -DF3DEX_GBI_2
    -DBUILD_VERSION=VERSION_I_P -DLANGUAGE_C -D_LANGUAGE_C -O2)
tools/ido/linux/7.1/cc "${target[@]}" src/story/sc_native_crash.c \
    -o "$out/sc_native_crash.o" > "$out/ido.log" 2>&1
mips-linux-gnu-objdump -dr "$out/sc_native_crash.o" > "$out/disassembly.txt"
mips-linux-gnu-size -A "$out/sc_native_crash.o" > "$out/sizes.txt"
