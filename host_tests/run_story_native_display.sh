#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
output=build/story/native-display-review-v1
mkdir -p "$output"
python3 - <<'PY'
from pathlib import Path
out = Path("build/story/native-display-review-v1")
source = Path("src/6A40.c").read_text()
start = source.index("typedef struct UnkStruct800A7428")
end = source.index("\nvoid func_80005E40(u32", start)
(out / "workspace-body.inc").write_text(source[start:end])
source = Path("src/6BC0.c").read_text()
start = source.index("void func_800062E4(")
end = source.index("\nvoid func_80006414(", start)
(out / "framebuffer-body.inc").write_text(source[start:end])
start = source.index("void func_8000648CRestoreStory(")
end = source.index("\n#endif", start)
(out / "image-restore-body.inc").write_text(source[start:end])
source = Path("src/stage_loader.c").read_text()
start = source.index("unk_func_80007444* func_80007444(")
end = source.index("\nunk_func_80007444* func_800075F8(", start)
(out / "context-body.inc").write_text(source[start:end])
start = source.index("void func_800075F8SetStory(")
end = source.index("\n#endif", start)
(out / "context-restore-body.inc").write_text(source[start:end])
PY
flags=(-DSTORY_NATIVE_UI -DROGUELIKE -std=gnu89 -Wall -Wextra -Werror
    -Wno-int-to-pointer-cast -Wno-pointer-to-int-cast -Wno-int-conversion
    -Wno-unused-variable -Wno-endif-labels -I. -iquote include
    -include host_tests/story_native_stubs.h)
"${CC:-gcc}" "${flags[@]}" -O2 host_tests/test_story_native_display.c -o "$output/test"
"$output/test"
"${CC:-gcc}" "${flags[@]}" -O1 -g -fsanitize=address,undefined \
    -fno-omit-frame-pointer host_tests/test_story_native_display.c -o "$output/test-san"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$output/test-san"
