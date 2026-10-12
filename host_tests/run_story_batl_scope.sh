#!/usr/bin/env bash
set -euo pipefail
out="${1:?fresh output}"
test ! -e "$out"
mkdir -p "$out"
python - "$out" <<'PY'
import sys
from pathlib import Path
source = Path("src/19840.c").read_text()
start = source.index("s32 func_80019B48(")
end = source.index("\ns32 func_80019C08(", start)
(Path(sys.argv[1]) / "worker-consumer.inc").write_text(source[start:end])
PY
baseline="${2:-.local/rltest/story-two-phase-consumer-v3/selector}"
flags=(-std=gnu89 -m32 -fwrapv -funsigned-char -fno-builtin
    -ffunction-sections -fdata-sections -DCC_CHECK -DNON_MATCHING -DAVOID_UB
    -D_LANGUAGE_C -D_MIPS_SZLONG=32 -DF3DEX_GBI_2 -DSTORY_SERVICE_CANDIDATE -DSC_SERVICE_HOST
    -Iinclude -Isrc -I. -Ibuild -Iassets/us "-I$baseline" "-I$out"
    -Ilib/ultralib/include -Ilib/ultralib/include/PR -Ilib/ultralib/include/ido
    -include "$baseline/host-stats-views.h" -include "$baseline/host-native-views.h")
for engine in host san; do
    extra=()
    if [[ "$engine" == san ]]; then extra=(-fsanitize=address,undefined -g); fi
    gcc "${flags[@]}" "${extra[@]}" host_tests/test_story_batl_scope.c \
        src/story/sc_batl_scope.c src/memmap.c \
        -Wl,--gc-sections -lm -o "$out/scope-$engine" > "$out/$engine-build.log" 2>&1
    for failure in 0 1 2 3 4 5 6 7 8 9 10 11 12; do
        UBSAN_OPTIONS=halt_on_error=1 ASAN_OPTIONS=detect_leaks=1 \
            "$out/scope-$engine" "$failure" | tee "$out/$engine-$failure.log"
    done
done
ido=(-c -O2 -mips2 -G 0 -non_shared -Xcpluscomm -nostdinc -DSTORY_SERVICE_CANDIDATE
    -D_LANGUAGE_C -D_MIPS_SZLONG=32 -DF3DEX_GBI_2 -Iinclude -Isrc -I.
    -Ibuild -Iassets/us -Ilib/ultralib/include -Ilib/ultralib/include/PR
    -Ilib/ultralib/include/ido)
for source in src/story/sc_batl_scope.c src/memmap.c src/19840.c src/fragments/62/fragment62_2F74E0.c; do
    name="$(basename "${source%.c}")"
    tools/ido/linux/7.1/cc "${ido[@]}" "$source" -o "$out/$name.o" \
        > "$out/$name-ido.log" 2>&1
    python tools/asm-processor/asm_processor.py -O2 --force --convert-statics local \
        --post-process "$out/$name.o" --assembler "mips-linux-gnu-as -march=vr4300 -32" "$source" \
        >> "$out/$name-ido.log" 2>&1
done
mips-linux-gnu-objdump -dr "$out/fragment62_2F74E0.o" > "$out/clock-source-disassembly.txt"
