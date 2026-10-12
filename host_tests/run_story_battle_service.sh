#!/usr/bin/env bash
set -euo pipefail
out="${1:?fresh output}"
test ! -e "$out"
mkdir -p "$out"
baseline=.local/rltest/story-two-phase-consumer-v3
authority="${2:-$baseline/image}"
image="${3:-$baseline/image/image.bin}"
selector="${5:-$baseline/selector}"
if [[ $# -ge 4 ]]; then
    test $# == 5
    bash host_tests/run_story_module_arena.sh "$out/arena" "$4"
else
    bash host_tests/run_story_module_arena.sh "$out/arena"
fi
spans="$out/arena"
python - "$out" <<'PY'
from pathlib import Path
import sys
out = Path(sys.argv[1])
source = Path("src/19840.c").read_text()
start = source.index("s32 func_80019B48(")
end = source.index("\ns32 func_80019C08(", start)
(out / "worker-consumer.inc").write_text(source[start:end])
source = Path("src/fragments/62/fragment62_2F74E0.c").read_text()
pieces = []
for name in ("func_8430D8C4", "func_8430D9EC", "func_8430DBA0", "func_8430DCD4",
             "func_8430DE20", "func_8430DF60", "func_8430EB1C"):
    marker = next(marker for marker in ("s32 ", "void ") if marker + name + "(" in source)
    start = source.index(marker + name + "(")
    brace = source.index("{", start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    pieces.append(source[start:end])
(out / "qualified-callers.inc").write_text("\n\n".join(pieces) + "\n")
PY
flags=(-std=gnu89 -m32 -fwrapv -funsigned-char -fno-builtin
    -ffunction-sections -fdata-sections -DCC_CHECK -DNON_MATCHING -DAVOID_UB
    -D_LANGUAGE_C -D_MIPS_SZLONG=32 -DF3DEX_GBI_2
    -DSTORY_SERVICE_CANDIDATE -DSTORY_NATIVE_PROBE -DSC_SERVICE_HOST
    -Iinclude -Isrc -I. -Ibuild -Iassets/us "-I$spans" "-I$out"
    "-I$selector" "-I$authority"
    -Ilib/ultralib/include -Ilib/ultralib/include/PR -Ilib/ultralib/include/ido
    -include "$selector/host-stats-views.h"
    -include "$selector/host-native-views.h")
faults=(0 1 2 3 4 5 6 7 8 9 10)
if [[ "${SC_SERVICE_FORCED_ONLY:-0}" == 1 ]]; then
    faults=(10 8 9)
fi
for engine in host san; do
    extra=()
    if [[ "$engine" == san ]]; then
        extra=(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
    fi
    for source in story_real_ai_image story_private_rom_loader story_module_host_io; do
        gcc -std=gnu99 -m32 "${extra[@]}" "-I$authority" \
            -c "host_tests/$source.c" -o "$out/$source-$engine.o"
    done
    gcc "${flags[@]}" "${extra[@]}" host_tests/test_story_battle_service.c \
        "$selector/owned-closure.c" src/story/sc_ai_bank.c \
        src/story/sc_ai_select.c src/story/sc_ai_decision.c src/story/sc_ai_overlay.c \
        src/story/sc_module_arena.c src/story/sc_batl_scope.c src/story/sc_batl_jobs.c src/memmap.c \
        host_tests/story_private_rom_native.c "$out/story_real_ai_image-$engine.o" \
        "$out/story_private_rom_loader-$engine.o" "$out/story_module_host_io-$engine.o" \
        -Wl,--gc-sections -lm -o "$out/service-$engine" > "$out/$engine-build.log" 2>&1
    for fault in "${faults[@]}"; do
        UBSAN_OPTIONS=halt_on_error=1 ASAN_OPTIONS=detect_leaks=1 \
            "$out/service-$engine" "$image" "$fault" \
            | tee "$out/service-$engine-$fault.log"
    done
done
