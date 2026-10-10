#!/usr/bin/env bash
# Integration gate. Every workstream branch must pass this before merge.
#   1. Vanilla build still matches the original ROM md5 (ROGUELIKE off).
#   2. Roguelike build compiles (once W0 lands the ROGUELIKE flag/target).
#   3. Host unit tests (once host_tests/ exists).
#   4. Emulator smoke tests (once tools/rltest exists).
set -euo pipefail
cd "$(dirname "$0")/.."
J="-j$(nproc)"

step() { printf '\n=== %s ===\n' "$1"; }

step "vanilla matching build"
make $J 2>&1 | tail -2

if grep -q '^rl:' Makefile 2>/dev/null; then
    step "roguelike build (make rl)"
    make rl $J 2>&1 | tail -3
else
    echo "(skip) no 'rl' make target yet"
fi

if [ -f host_tests/Makefile ]; then
    step "host unit tests"
    make -C host_tests test
else
    echo "(skip) no host_tests yet"
fi

if [ -x tools/rltest/run_smoke.sh ]; then
    step "emulator smoke tests"
    tools/rltest/run_smoke.sh
else
    echo "(skip) no emulator harness yet"
fi

printf '\nGATE PASSED\n'
