#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
bash host_tests/run_story_native_display.sh
output=build/story/native-retirement-review-v1
mkdir -p "$output"
python3 - <<'PY'
from pathlib import Path
source = Path("src/dp_intro.c").read_text()
start = source.index("static void *sScDpControl;")
end = source.index("\n#endif", start)
Path("build/story/native-retirement-review-v1/seam-body.inc").write_text(source[start:end])
start = source.index("void func_80001B7C(void)")
end = source.index("void func_80001BD4", start)
Path("build/story/native-retirement-review-v1/admission-body.inc").write_text(source[start:end])
header = Path("src/dp_intro.h").read_text()
start = header.index("#ifdef STORY_NATIVE_UI")
end = header.index("\n#endif", start)
Path("build/story/native-retirement-review-v1/seam-header.inc").write_text(header[start:end] + "\n#endif\n")
Path("build/story/native-retirement-review-v1/layout.c").write_text(
    '#include "src/story/sc_native_retirement.h"\n'
    'char sc_retirement_bytes[sizeof(ScNativeRetirement)];\n'
    'char sc_snapshot_bytes[sizeof(ScDpSnapshot)];\n'
    'char sc_token_bytes[sizeof(ScDpToken)];\n')
PY
flags=(-DSTORY_NATIVE_UI -DROGUELIKE -std=gnu89 -Wall -Wextra -Werror
    -Wno-int-to-pointer-cast -Wno-pointer-to-int-cast -Wno-int-conversion
    -Wno-unused-variable -Wno-endif-labels -I. -iquote include -iquote src
    -include host_tests/story_native_stubs.h)
"${CC:-gcc}" "${flags[@]}" -O2 host_tests/test_story_native_retirement.c -o "$output/test"
"$output/test"
"${CC:-gcc}" "${flags[@]}" -O1 -g -fsanitize=address,undefined \
    -fno-omit-frame-pointer host_tests/test_story_native_retirement.c -o "$output/test-san"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$output/test-san"
target_flags=(-c -G 0 -non_shared -Xcpluscomm -nostdinc -Wab,-r4300_mul
    -DVERSION_US=1 -DSTORY_NATIVE_UI -DROGUELIKE -Iinclude -Isrc -Isrc/libnaudio
    -Iassets/us -I. -Ibuild -Ilib/ultralib/include -Ilib/ultralib/include/PR
    -Ilib/ultralib/include/ido -fullwarn -woff 624,649,838,712,516,513,596,564,594
    -mips2 -EB -D_MIPS_SZLONG=32 -DNDEBUG -D_FINALROM -DN_MICRO -DF3DEX_GBI_2
    -DBUILD_VERSION=VERSION_I_P -DLANGUAGE_C -D_LANGUAGE_C -O2)
tools/ido/linux/7.1/cc "${target_flags[@]}" src/dp_intro.c -o "$output/dp_intro.o"
tools/ido/linux/7.1/cc "${target_flags[@]}" src/story/sc_native_retirement.c -o "$output/sc_native_retirement.o"
tools/ido/linux/7.1/cc "${target_flags[@]}" "$output/layout.c" -o "$output/layout.o"
mips-linux-gnu-nm -S --size-sort "$output/layout.o" > "$output/layout.txt"
mips-linux-gnu-objdump -dr "$output/dp_intro.o" "$output/sc_native_retirement.o" > "$output/disassembly.txt"
