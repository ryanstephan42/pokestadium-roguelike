#!/usr/bin/env bash
# Refused-open campaign cleanup through the actual installer/adapter/persist
# loader and actual loop/lifetime/memory/display: HOST normal + strict
# ASan/UBSan, then IDO compile of the changed lifetime and loop units.
set -euo pipefail
cd "$(dirname "$0")/.."
out="${1:?fresh output directory}"
test ! -e "$out"
mkdir -p "$out"
bash host_tests/run_story_native_retirement.sh > "$out/retirement.log" 2>&1
flags=(-m32 -DSTORY_NATIVE_UI -DSTORY_SERVICE_CANDIDATE -DROGUELIKE -std=gnu89
    -Wall -Wextra -Werror -Wno-endif-labels -I. -iquote include -iquote src
    -ffunction-sections -fdata-sections)
for mode in host san; do
    extra=(-O2)
    if [[ "$mode" == san ]]; then
        extra=(-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer)
    fi
    objs=()
    for unit in sc_campaign sc_native_campaign sc_persist; do
        gcc "${flags[@]}" "${extra[@]}" -include host_tests/story_campaign_lifetime_prelude.h -c "src/story/$unit.c" -o "$out/$unit-$mode.o"
        objs+=("$out/$unit-$mode.o")
    done
    gcc "${flags[@]}" "${extra[@]}" -DSC_NATIVE_MEMORY_HOST -DSC_NATIVE_CRASH_HOST \
        -include host_tests/story_native_stubs.h \
        -Wno-int-to-pointer-cast -Wno-pointer-to-int-cast -Wno-int-conversion \
        -Wno-unused-variable -Wno-unused-function \
        host_tests/test_story_campaign_lifetime.c "${objs[@]}" -Wl,--gc-sections \
        -o "$out/campaign-lifetime-$mode"
    UBSAN_OPTIONS=halt_on_error=1 ASAN_OPTIONS=detect_leaks=1 \
        "$out/campaign-lifetime-$mode" | tee "$out/$mode.log"
done
target=(-c -G 0 -non_shared -Xcpluscomm -nostdinc -Wab,-r4300_mul
    -DVERSION_US=1 -DSTORY_NATIVE_UI -DROGUELIKE
    -Iinclude -Isrc -Isrc/libnaudio -Iassets/us -I. -Ibuild
    -Ilib/ultralib/include -Ilib/ultralib/include/PR -Ilib/ultralib/include/ido
    -fullwarn -woff 624,649,838,712,516,513,596,564,594
    -mips2 -EB -D_MIPS_SZLONG=32 -DNDEBUG -D_FINALROM -DN_MICRO -DF3DEX_GBI_2
    -DBUILD_VERSION=VERSION_I_P -DLANGUAGE_C -D_LANGUAGE_C -O2)
for unit in sc_native_lifetime sc_native_loop; do
    tools/ido/linux/7.1/cc "${target[@]}" "src/story/$unit.c" -o "$out/$unit.o" \
        > "$out/$unit-ido.log" 2>&1
    mips-linux-gnu-objdump -dr "$out/$unit.o" > "$out/$unit-disassembly.txt"
done
sha256sum "$out"/*.o > "$out/objects.sha256"
echo "STORY CAMPAIGN LIFETIME RUNNER PASSED"
