#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
out="${1:?Supply an isolated output directory}"
mkdir -p "$out"
if [[ $# == 3 ]]; then
    test "$3" = --native
    python host_tests/generate_story_ai_authority.py "$out" "$2" --native
elif [[ $# == 2 ]]; then
    python host_tests/generate_story_ai_authority.py "$out" "$2"
else
    python host_tests/generate_story_ai_authority.py "$out"
fi
sources=(host_tests/story_real_ai_image.c host_tests/story_private_rom_loader.c host_tests/test_story_real_ai_image.c)
flags=(-std=c11 -O2 -Wall -Wextra -Werror -pedantic -g "-I$out")
gcc "${flags[@]}" "${sources[@]}" -o "$out/real-ai-host"
"$out/real-ai-host" "$out/image.bin" "$out/retail-reference.bin" | tee "$out/real-ai-host.log"
AI_TEST_REVOKE=1 "$out/real-ai-host" "$out/image.bin" "$out/retail-reference.bin" | tee "$out/revoke-host.log"
AI_TEST_INPLACE=1 "$out/real-ai-host" "$out/image.bin" "$out/retail-reference.bin" | tee "$out/inplace-host.log"
AI_TEST_MAP_SWAP=1 "$out/real-ai-host" "$out/image.bin" "$out/retail-reference.bin" | tee "$out/map-swap-host.log"
AI_TEST_SYNC_FAIL=1 "$out/real-ai-host" "$out/image.bin" "$out/retail-reference.bin" | tee "$out/sync-fail-host.log"
AI_TEST_EARLY_REVOKE=1 "$out/real-ai-host" "$out/image.bin" "$out/retail-reference.bin" | tee "$out/early-revoke-host.log"
gcc "${flags[@]}" -fsanitize=address,undefined -fno-omit-frame-pointer \
    "${sources[@]}" -o "$out/real-ai-san"
UBSAN_OPTIONS=halt_on_error=1 ASAN_OPTIONS=detect_leaks=1 \
    "$out/real-ai-san" "$out/image.bin" "$out/retail-reference.bin" | tee "$out/real-ai-san.log"
AI_TEST_REVOKE=1 UBSAN_OPTIONS=halt_on_error=1 ASAN_OPTIONS=detect_leaks=1 \
    "$out/real-ai-san" "$out/image.bin" "$out/retail-reference.bin" | tee "$out/revoke-san.log"
AI_TEST_INPLACE=1 UBSAN_OPTIONS=halt_on_error=1 ASAN_OPTIONS=detect_leaks=1 \
    "$out/real-ai-san" "$out/image.bin" "$out/retail-reference.bin" | tee "$out/inplace-san.log"
AI_TEST_MAP_SWAP=1 UBSAN_OPTIONS=halt_on_error=1 ASAN_OPTIONS=detect_leaks=1 \
    "$out/real-ai-san" "$out/image.bin" "$out/retail-reference.bin" | tee "$out/map-swap-san.log"
AI_TEST_SYNC_FAIL=1 UBSAN_OPTIONS=halt_on_error=1 ASAN_OPTIONS=detect_leaks=1 \
    "$out/real-ai-san" "$out/image.bin" "$out/retail-reference.bin" | tee "$out/sync-fail-san.log"
AI_TEST_EARLY_REVOKE=1 UBSAN_OPTIONS=halt_on_error=1 ASAN_OPTIONS=detect_leaks=1 \
    "$out/real-ai-san" "$out/image.bin" "$out/retail-reference.bin" | tee "$out/early-revoke-san.log"
