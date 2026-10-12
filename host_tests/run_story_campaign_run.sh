#!/usr/bin/env bash
# Connected campaign runner: actual sc_campaign_run.c + installer/adapter/
# persist/arena with labelled substituted seams (see test header); HOST normal +
# strict ASan/UBSan, then IDO compile of the actual runner.
set -euo pipefail
cd "$(dirname "$0")/.."
out="${1:?fresh output directory}"
test ! -e "$out"
mkdir -p "$out"
sources=(host_tests/test_story_campaign_run.c src/story/sc_campaign_run.c src/story/sc_campaign.c
    src/story/sc_native_campaign.c src/story/sc_persist.c src/story/sc_module_arena.c
    src/story/sc_combat.c src/story/sc_progression.c src/story/sc_core.c
    src/story/sc_make.c src/story/sc_graph.c src/story/sc_bridge.c
    src/story/sc_storage.c src/story/sc_save.c src/story/sc_crc.c)
flags=(-DSTORY_NATIVE_UI -DSTORY_NATIVE_PROBE -DSTORY_SERVICE_CANDIDATE -DROGUELIKE
    -std=gnu89
    -Wall -Wextra -Werror -Wno-endif-labels -I. -iquote include -iquote src
    -include host_tests/story_campaign_run_seams.h)
for mode in host san; do
    extra=(-O2)
    if [[ "$mode" == san ]]; then
        extra=(-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer)
    fi
    "${CC:-gcc}" "${flags[@]}" "${extra[@]}" "${sources[@]}" -o "$out/campaign-run-$mode"
    UBSAN_OPTIONS=halt_on_error=1 ASAN_OPTIONS=detect_leaks=1 \
        "$out/campaign-run-$mode" | tee "$out/$mode.log"
done
target_flags=(-c -G 0 -non_shared -Xcpluscomm -nostdinc -Wab,-r4300_mul
    -DVERSION_US=1 -DSTORY_NATIVE_UI -DSTORY_NATIVE_PROBE -DSTORY_SERVICE_CANDIDATE -DROGUELIKE
    -Ihost_tests/story_ido_include -Iinclude -Isrc -Isrc/libnaudio -Iassets/us -I. -Ibuild
    -Ilib/ultralib/include -Ilib/ultralib/include/PR -Ilib/ultralib/include/ido
    -fullwarn -woff 624,649,838,712,516,513,596,564,594
    -mips2 -EB -D_MIPS_SZLONG=32 -DNDEBUG -D_FINALROM -DN_MICRO -DF3DEX_GBI_2
    -DBUILD_VERSION=VERSION_I_P -DLANGUAGE_C -D_LANGUAGE_C -O2)
tools/ido/linux/7.1/cc "${target_flags[@]}" src/story/sc_campaign_run.c -o "$out/sc_campaign_run.o" \
    > "$out/sc_campaign_run-ido.log" 2>&1
mips-linux-gnu-objdump -dr "$out/sc_campaign_run.o" > "$out/sc_campaign_run-disassembly.txt"
sha256sum "$out"/*.o > "$out/ido-objects.sha256"
echo "STORY CAMPAIGN RUN RUNNER PASSED"
