#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
flags=(-std=gnu89 -m32 -funsigned-char -fno-builtin -ffunction-sections -fdata-sections
    -Werror=incompatible-pointer-types
    -DSTORY_NATIVE_PROBE -DCC_CHECK -DNON_MATCHING -DAVOID_UB -D_LANGUAGE_C -DLANGUAGE_C
    -D_MIPS_SZLONG=32 -DF3DEX_GBI_2
    -Iinclude -Isrc -I. -Ibuild -Iassets/us
    -Ilib/ultralib/include -Ilib/ultralib/include/PR -Ilib/ultralib/include/ido)
gcc "${flags[@]}" host_tests/test_story_native.c src/story/sc_native_probe.c \
    src/story/sc_pak_probe.c src/roguelike/rl_pak.c \
    src/story/sc_progression.c src/story/sc_bridge.c src/story/sc_core.c \
    src/story/sc_graph.c src/story/sc_save.c src/story/sc_storage.c build/story/sc_data.c \
    -Wl,--gc-sections -o build/story/test_story_native
build/story/test_story_native
gcc "${flags[@]}" host_tests/test_story_runtime.c src/story/sc_native_probe.c \
    src/story/sc_progression.c src/story/sc_bridge.c src/story/sc_core.c \
    src/story/sc_graph.c src/story/sc_save.c src/story/sc_storage.c build/story/sc_data.c \
    -Wl,--gc-sections -o build/story/test_story_runtime
build/story/test_story_runtime
python3 -m unittest host_tests.test_story_probe
tools/ido/linux/7.1/cc -c -O2 -mips2 -G 0 -non_shared -Xcpluscomm -nostdinc \
    -woff 838,649,624,807 -DSTORY_NATIVE_PROBE -D_LANGUAGE_C -DF3DEX_GBI_2 \
    -D_MIPS_SZLONG=32 -Iinclude -Isrc -I. -Ibuild -Iassets/us \
    -Ilib/ultralib/include -Ilib/ultralib/include/PR -Ilib/ultralib/include/ido \
    src/story/sc_native_probe.c -o build/story/sc_native_probe.o
tools/ido/linux/7.1/cc -c -O2 -mips2 -G 0 -non_shared -Xcpluscomm -nostdinc \
    -woff 838,649,624,807 -DSTORY_NATIVE_PROBE -D_LANGUAGE_C -DF3DEX_GBI_2 \
    -D_MIPS_SZLONG=32 -Iinclude -Isrc -I. -Ibuild -Iassets/us \
    -Ilib/ultralib/include -Ilib/ultralib/include/PR -Ilib/ultralib/include/ido \
    src/story/sc_pak_probe.c -o build/story/sc_pak_probe.o
