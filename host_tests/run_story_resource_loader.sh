#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
output=build/story/native-loader-review-v1
mkdir -p "$output"
python3 - <<'PY'
from pathlib import Path
source = Path("src/3FB0.c").read_text()
start = source.index("void* func_80003DC4(")
end = source.index("\nvoid* func_80003F54(", start)
Path("build/story/native-loader-review-v1/loader-body.inc").write_text(source[start:end] + "\n")
PY
flags=(-DSTORY_NATIVE_UI -std=gnu89 -Wall -Wextra -Werror -Wno-multichar)
"${CC:-gcc}" "${flags[@]}" -O2 host_tests/test_story_resource_loader.c -o "$output/test"
"$output/test"
"${CC:-gcc}" "${flags[@]}" -O1 -g -fsanitize=address,undefined \
    -fno-omit-frame-pointer host_tests/test_story_resource_loader.c -o "$output/test-san"
ASAN_OPTIONS=detect_leaks=1 "$output/test-san"
tools/ido/linux/7.1/cc -c -G 0 -non_shared -Xcpluscomm -nostdinc -Wab,-r4300_mul \
    -DVERSION_US=1 -DSTORY_NATIVE_UI -DROGUELIKE -Iinclude -Isrc -Isrc/libnaudio \
    -Iassets/us -I. -Ibuild -Ilib/ultralib/include -Ilib/ultralib/include/PR \
    -Ilib/ultralib/include/ido -fullwarn -woff 624,649,838,712,516,513,596,564,594 \
    -mips2 -EB -D_MIPS_SZLONG=32 -DNDEBUG -D_FINALROM -DN_MICRO -DF3DEX_GBI_2 \
    -DBUILD_VERSION=VERSION_I_P -DLANGUAGE_C -D_LANGUAGE_C -O2 src/3FB0.c -o "$output/3FB0.o"
mips-linux-gnu-objdump -d "$output/3FB0.o" > "$output/loader-disassembly.txt"
