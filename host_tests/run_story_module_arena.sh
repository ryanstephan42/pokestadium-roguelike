#!/usr/bin/env bash
set -euo pipefail
out="${1:?fresh output}"
test ! -e "$out"
mkdir -p "$out"
if [[ $# == 2 ]]; then
    python host_tests/generate_story_module_spans.py "$2" "$out" --native
else
    test $# == 1
    python host_tests/generate_story_module_spans.py \
        .local/rltest/story-two-phase-consumer-v3/selector "$out"
fi
for mode in host san; do
    extra=()
    if [[ "$mode" == san ]]; then extra=(-fsanitize=address,undefined -g); fi
    gcc -std=gnu89 -m32 -O2 -Wall -Wextra -Werror "${extra[@]}" -I. "-I$out" \
        src/story/sc_module_arena.c host_tests/test_story_module_arena.c -o "$out/arena-$mode"
    for scenario in normal fail data-fail; do
        UBSAN_OPTIONS=halt_on_error=1 ASAN_OPTIONS=detect_leaks=1 \
            "$out/arena-$mode" "$scenario" | tee "$out/arena-$mode-$scenario.log"
    done
done
flags=(-c -O2 -mips2 -G 0 -non_shared -Xcpluscomm -nostdinc
    -woff 838,649,624,807 -D_LANGUAGE_C -D_MIPS_SZLONG=32 -Iinclude -Isrc -I.)
tools/ido/linux/7.1/cc "${flags[@]}" src/story/sc_module_arena.c \
    -o "$out/sc_module_arena.o" > "$out/ido.log" 2>&1
mips-linux-gnu-objdump -dr "$out/sc_module_arena.o" > "$out/arena-disassembly.txt"
mips-linux-gnu-size -A "$out/sc_module_arena.o" > "$out/arena-size.txt"
mips-linux-gnu-ld -Ttext 0x8f920000 -e sc_ma_init "$out/sc_module_arena.o" \
    -Map "$out/arena.map" -o "$out/arena.elf"
mips-linux-gnu-objdump -d "$out/arena.elf" > "$out/arena-linked-disassembly.txt"
