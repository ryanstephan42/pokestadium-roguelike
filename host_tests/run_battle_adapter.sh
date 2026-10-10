#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/host_tests
python3 tools/rl_extract_rosters.py
flags=(-std=gnu89 -m32 -funsigned-char -fno-builtin -ffunction-sections -fdata-sections \
    -DCC_CHECK -DNON_MATCHING -DAVOID_UB -D_LANGUAGE_C -DLANGUAGE_C \
    -D_MIPS_SZLONG=32 -DF3DEX_GBI_2 \
    -Iinclude -Isrc -I. -Ibuild -Iassets/us \
    -Ilib/ultralib/include -Ilib/ultralib/include/PR -Ilib/ultralib/include/ido)
gcc "${flags[@]}" -Dfunc_80022338=rl_native_calculate -Dfunc_800224B8=rl_native_exp \
    -c src/22630.c -o build/host_tests/native-stat-dvs.o
gcc "${flags[@]}" host_tests/test_battle_adapter.c build/host_tests/native-stat-dvs.o \
    src/roguelike/rl_content.c src/roguelike/rl_enemy.c \
    src/roguelike/rl_party.c src/roguelike/rl_relics.c \
    src/roguelike/rl_core.c src/roguelike/rl_map.c \
    -Wl,--gc-sections -o build/host_tests/battle-adapter
build/host_tests/battle-adapter
echo "Battle adapter: OK"
