#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
mkdir -p build
gcc -std=gnu89 -m32 -funsigned-char -fno-builtin \
    -DCC_CHECK -DNON_MATCHING -DAVOID_UB -D_LANGUAGE_C -DLANGUAGE_C \
    -D_MIPS_SZLONG=32 -DF3DEX_GBI_2 \
    -Iinclude -Isrc -I. -Ibuild -Iassets/us \
    -Ilib/ultralib/include -Ilib/ultralib/include/PR -Ilib/ultralib/include/ido \
    tools/rltest/battle_layout.c -o build/rl-battle-layout
build/rl-battle-layout
echo "Battle layout/accessors: OK"
