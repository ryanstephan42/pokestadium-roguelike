#!/usr/bin/env bash
set -euo pipefail
out="${1:?fresh output directory}"
test ! -e "$out"
mkdir -p "$out"
python tools/story_data.py --moon-stones --out "$out/data.json" --c-out "$out/sc_data.c"
sources=(src/story/sc_core.c src/story/sc_combat.c src/story/sc_make.c
    src/story/sc_graph.c src/story/sc_progression.c src/story/sc_save.c
    src/story/sc_crc.c src/story/sc_storage.c tools/rltest/story_six_fixture.c
    "$out/sc_data.c")
flags=(-std=c89 -pedantic -Wall -Wextra -Werror -O2 -I.)
gcc "${flags[@]}" host_tests/test_story_ember.c tools/rltest/story_ember_fixture.c \
    "${sources[@]}" -o "$out/codec"
gcc "${flags[@]}" host_tests/test_story_six.c "${sources[@]}" -o "$out/six-proof"
"$out/six-proof" "$out/checkpoint.sc01" > "$out/host-proof.jsonl"
gcc "${flags[@]}" host_tests/test_story_current_reload_fixture.c "${sources[@]}" -o "$out/fixture"
"$out/fixture" "$out/note.bin" "$out/state.bin" "$out/precommit.bin"
"$out/codec" --validate-current-reload "$out/note.bin" "$out/state.bin" "$out/precommit.bin"
gcc "${flags[@]}" -fsanitize=address,undefined -fno-omit-frame-pointer -g \
    host_tests/test_story_ember.c tools/rltest/story_ember_fixture.c \
    "${sources[@]}" -o "$out/codec-san"
UBSAN_OPTIONS=halt_on_error=1 ASAN_OPTIONS=detect_leaks=1 "$out/codec-san" \
    --validate-current-reload "$out/note.bin" "$out/state.bin" "$out/precommit.bin"
python - "$out" <<'PY'
from pathlib import Path
import json
import subprocess
import sys

out = Path(sys.argv[1])
command = [str(out / "codec"), "--validate-current-reload",
           str(out / "note.bin"), str(out / "state.bin"), str(out / "precommit.bin")]
refusals = []
for name in ("state.bin", "precommit.bin"):
    original = (out / name).read_bytes()
    assert len(original) == 648
    for label, altered in [("historical644", original[:644]), ("truncated", original[:-1]),
                           ("oversized", original + b"\0")] + [
                               (f"byte{offset}", original[:offset] +
                                bytes([original[offset] ^ 1]) + original[offset + 1:])
                               for offset in (4, 644, 645, 646, 647)]:
        path = out / "refused.bin"
        path.write_bytes(altered)
        checked = command.copy()
        checked[3 if name == "state.bin" else 4] = str(path)
        result = subprocess.run(checked, capture_output=True, text=True)
        assert result.returncode != 0, (name, label, result.stdout)
        refusals.append(f"{name}:{label}")
historical = subprocess.run([str(out / "codec"), "--validate-reload",
                             str(out / "note.bin"), str(out / "state.bin")],
                            capture_output=True, text=True)
assert historical.returncode != 0
(out / "refused.bin").unlink()
(out / "codec-refusals.json").write_text(json.dumps(refusals, indent=2) + "\n")
print(f"CURRENT RELOAD PASSED: full648/v2, {len(refusals)} malformed refusals; historical mode rejects648")
PY
