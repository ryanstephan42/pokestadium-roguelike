#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
out="${1:?Supply an isolated output directory}"
mkdir -p "$out"
flags=(-std=c11 -Wall -Wextra -Werror -pedantic -g)
sources=(host_tests/story_private_rom_loader.c host_tests/test_story_private_rom_loader.c)
gcc "${flags[@]}" "${sources[@]}" -o "$out/private-rom-host"
"$out/private-rom-host" | tee "$out/private-rom-host.log"
gcc "${flags[@]}" -fsanitize=address,undefined -fno-omit-frame-pointer \
    "${sources[@]}" -o "$out/private-rom-san"
UBSAN_OPTIONS=halt_on_error=1 ASAN_OPTIONS=detect_leaks=1 \
    "$out/private-rom-san" | tee "$out/private-rom-san.log"
