#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
elf="${1:?Supply an audited ELF path}"
out="${2:?Supply an isolated output directory}"
mkdir -p "$out"
bash tools/rltest/compile_story_ai_prototype.sh "$elf" "$out" > "$out/mips-build.log" 2>&1
flags=(-std=gnu89 -m32 -fwrapv -funsigned-char -fno-builtin
    -ffunction-sections -fdata-sections -DCC_CHECK -DNON_MATCHING -DAVOID_UB
    -D_LANGUAGE_C -D_MIPS_SZLONG=32 -DF3DEX_GBI_2
    -Iinclude -Isrc -I. -Ibuild -Iassets/us "-I$out"
    -Ilib/ultralib/include -Ilib/ultralib/include/PR -Ilib/ultralib/include/ido
    -include "$out/host-stats-views.h" -include "$out/host-native-views.h")
gcc "${flags[@]}" host_tests/test_story_ai_selector.c "$out/owned-closure.c" src/story/sc_ai_bank.c src/story/sc_ai_select.c src/story/sc_ai_decision.c src/story/sc_ai_overlay.c \
    -Wl,--gc-sections -lm -o "$out/selector-host" > "$out/selector-build.log" 2>&1
"$out/selector-host" | tee "$out/selector-host.log"
gcc "${flags[@]}" -fsanitize=address,undefined \
    -fno-omit-frame-pointer -g host_tests/test_story_ai_selector.c \
    "$out/owned-closure.c" src/story/sc_ai_bank.c src/story/sc_ai_select.c src/story/sc_ai_decision.c src/story/sc_ai_overlay.c -Wl,--gc-sections -lm \
    -o "$out/selector-san" > "$out/selector-san-build.log" 2>&1
UBSAN_OPTIONS=halt_on_error=1 ASAN_OPTIONS=detect_leaks=1 \
    "$out/selector-san" | tee "$out/selector-san.log"
gcc -std=c89 -pedantic -Wall -Wextra -Werror host_tests/test_story_ai_lease.c \
    src/story/sc_ai_lease.c -o "$out/lease-host"
"$out/lease-host" | tee "$out/lease-host.log"
gcc -std=c89 -pedantic -Wall -Wextra -Werror -fsanitize=address,undefined \
    -fno-omit-frame-pointer -g host_tests/test_story_ai_lease.c \
    src/story/sc_ai_lease.c -o "$out/lease-san"
UBSAN_OPTIONS=halt_on_error=1 ASAN_OPTIONS=detect_leaks=1 \
    "$out/lease-san" | tee "$out/lease-san.log"
gcc -std=gnu89 -m32 -fwrapv -funsigned-char -fno-builtin \
        -ffunction-sections -fdata-sections -DCC_CHECK -DNON_MATCHING \
        -D_LANGUAGE_C -D_MIPS_SZLONG=32 -DF3DEX_GBI_2 -DSTORY_NATIVE_PROBE \
        -I. -iquote include -Ilib/ultralib/include -fsanitize=address,undefined \
        host_tests/test_story_ai_relocation.c src/memmap.c src/story/sc_ai_lease.c \
        -Wl,--gc-sections -o "$out/relocation-host" > "$out/relocation-build.log" 2>&1
UBSAN_OPTIONS=halt_on_error=1 ASAN_OPTIONS=detect_leaks=1 \
    "$out/relocation-host" "$out/ai-image.bin" | tee "$out/relocation-host.log"
