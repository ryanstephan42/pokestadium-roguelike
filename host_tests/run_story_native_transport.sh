#!/usr/bin/env bash
set -euo pipefail
out="${1:?fresh output}"
test ! -e "$out"
mkdir -p "$out"
flags=(-m32 -std=gnu89 -D_LANGUAGE_C -D_MIPS_SZLONG=32
    -Iinclude -Ilib/ultralib/include -Ilib/ultralib/include/PR)
for mode in host san; do
    extra=()
    if [[ "$mode" == san ]]; then extra=(-fsanitize=address,undefined -g); fi
    gcc "${flags[@]}" "${extra[@]}" host_tests/story_private_rom_loader.c \
        host_tests/story_private_rom_native.c host_tests/test_story_native_transport.c \
        -o "$out/transport-$mode" > "$out/$mode-build.log" 2>&1
    UBSAN_OPTIONS=halt_on_error=1 ASAN_OPTIONS=detect_leaks=1 \
        "$out/transport-$mode" | tee "$out/$mode.log"
done
tools/ido/linux/7.1/cc -c -O2 -mips2 -G 0 -non_shared -Xcpluscomm -nostdinc \
    -D_LANGUAGE_C -D_MIPS_SZLONG=32 -Ihost_tests/story_ido_include \
    -Iinclude -Ilib/ultralib/include -Ilib/ultralib/include/PR \
    host_tests/story_private_rom_native.c -o "$out/native-transport.o" \
    > "$out/ido-build.log" 2>&1
mips-linux-gnu-nm -u "$out/native-transport.o" > "$out/os-imports.txt"
