#!/usr/bin/env bash
# HOST: every canonical campaign party through the real private AI selector/decision service.
set -euo pipefail
cd "$(dirname "$0")/.."
out="${1:?fresh output required}"
test ! -e "$out"
mkdir -p "$out"
baseline=.local/rltest/story-six-selector-prototype-stage2-fit-blocked/pokestadium-rl.elf
bash tools/rltest/compile_story_ai_prototype.sh "$baseline" "$out/selector" \
    > "$out/ido-build.log" 2>&1
bash host_tests/run_story_restore_faults.sh "$out/selector" generate
python3 - "$out/selector" <<'PY'
import sys
from pathlib import Path
out = Path(sys.argv[1])
source = Path("src/story/sc_ai_decision.c").read_text()
start = source.index("static unsigned usable(")
end = source.index("\nstatic int initial_route(", start)
(out / "decision-predicate.inc").write_text(
    source[start:end].replace("usable(", "host_decision_usable("))
PY
# Native move-0 reads (no damaging move) land on the six bytes before D_80072B00.
python3 - "$baseline" ${SWEEP_EXTRA_ELFS:-} <<'PY' | tee "$out/move-zero-entry.log"
import subprocess, sys
for elf in sys.argv[1:]:
    symbols = subprocess.run(["mips-linux-gnu-nm", elf], capture_output=True, text=True,
                             check=True).stdout.split()
    address = int(symbols[symbols.index("D_80072B00") - 2], 16)
    dump = subprocess.run(["mips-linux-gnu-objdump", "-s", "--start-address=%#x" % (address - 6),
                           "--stop-address=%#x" % address, elf], capture_output=True, text=True,
                          check=True).stdout.splitlines()[-1].split()
    data = bytes.fromhex("".join(dump[1:-1])[:12])
    assert len(data) == 6 and data == bytes(6), (elf, data)
    print("move-zero entry %s D_80072B00=%#x prefix=%s" % (elf, address, data.hex()))
PY
python3 tools/story_data.py --moon-stones --out "$out/data.json" --c-out "$out/sc_data.c"
gcc -DROGUELIKE -std=c89 -pedantic -Wall -Wextra -Werror -I. \
    host_tests/story_ai_party_table.c src/story/sc_make.c src/story/sc_core.c \
    src/story/sc_combat.c src/story/sc_graph.c src/story/sc_progression.c \
    "$out/sc_data.c" -o "$out/party-table"
"$out/party-table" > "$out/selector/party-table.inc"
flags=(-std=gnu89 -m32 -fwrapv -funsigned-char -fno-builtin
    -ffunction-sections -fdata-sections -DCC_CHECK -DNON_MATCHING -DAVOID_UB
    -D_LANGUAGE_C -D_MIPS_SZLONG=32 -DF3DEX_GBI_2 -DSC_AI_HOST_RESTORE_FAULTS
    -Iinclude -Isrc -I. -Ibuild -Iassets/us "-I$out/selector"
    -Ilib/ultralib/include -Ilib/ultralib/include/PR -Ilib/ultralib/include/ido
    -include "$out/selector/host-stats-views.h"
    -include "$out/selector/host-native-views.h")
for engine in host san; do
    extra=()
    if [[ "$engine" == san ]]; then
        extra=(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
    fi
    gcc "${flags[@]}" "${extra[@]}" host_tests/test_story_ai_party_sweep.c \
        "$out/selector/owned-closure.c" "$out/selector/fault-bank.c" \
        src/story/sc_ai_select.c src/story/sc_ai_decision.c src/story/sc_ai_overlay.c \
        -Wl,--gc-sections -lm -o "$out/sweep-$engine" > "$out/$engine-build.log" 2>&1
    UBSAN_OPTIONS=halt_on_error=1 ASAN_OPTIONS=detect_leaks=1 \
        "$out/sweep-$engine" | tee "$out/sweep-$engine.log"
done
