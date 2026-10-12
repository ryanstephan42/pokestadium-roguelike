#!/usr/bin/env bash
# Connected loader job/publication regression over verbatim extracted sources (gcc -m32 + ASan/UBSan).
set -euo pipefail
out="${1:?fresh output}"
test ! -e "$out"
mkdir -p "$out"
python - "$out" <<'PY'
import sys
from pathlib import Path
def span(path, first, until=None, last=None):
    s = Path(path).read_text()
    a = s.index(first)
    b = s.index(until, a) if until else s.index("\n}\n", s.index(last, a)) + 3
    return "/* %s: %s */\n" % (path, first.strip()) + s[a:b] + "\n"
text = span("src/19840.c", "void func_80019420(", "\nvoid func_800196DC(")
text += span("src/19840.c", "s32 func_800198E4(", "\nunk_D_86002F30* func_80019D18(")
text += span("src/1C720.c", "void func_8001C07C(", last="void func_8001C2D8(")
text += span("src/fragments/62/fragment62_315D50.c", "void func_8432C830(", last="void func_8432CAA8(")
text += span("src/fragments/62/fragment62_315D50.c", "void func_8432CD70(", last="void func_8432CE78(")
(Path(sys.argv[1]) / "jobs-extracted.inc").write_text(text)
geo = span("src/geo_layout.c", "static u32 sRlGeoImageLo;", "\nvoid rl_geo_layout_check(")
assert geo.count("void rl_geo_image_set(Fragment* frag) {") == 1
# renamed only so non-image fixtures can bypass it; the body is verbatim
(Path(sys.argv[1]) / "geo-image-extracted.inc").write_text(
    geo.replace("void rl_geo_image_set(Fragment* frag) {", "static void geo_image_set(Fragment* frag) {"))
PY
baseline="${2:-.local/rltest/story-two-phase-consumer-v3/selector}"
flags=(-std=gnu89 -m32 -fwrapv -funsigned-char -fno-builtin -Wno-int-conversion
    -ffunction-sections -fdata-sections -DCC_CHECK -DNON_MATCHING -DAVOID_UB
    -D_LANGUAGE_C -D_MIPS_SZLONG=32 -DF3DEX_GBI_2 -DROGUELIKE -DSTORY_SERVICE_CANDIDATE -DSC_SERVICE_HOST
    -Iinclude -Isrc -I. -Ibuild -Iassets/us "-I$baseline" "-I$out"
    -Ilib/ultralib/include -Ilib/ultralib/include/PR -Ilib/ultralib/include/ido
    -include "$baseline/host-stats-views.h" -include "$baseline/host-native-views.h")
cases=(0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30)
for engine in host san; do
    extra=()
    if [[ "$engine" == san ]]; then extra=(-fsanitize=address,undefined -fno-sanitize-recover=all -g); fi
    gcc "${flags[@]}" "${extra[@]}" host_tests/test_story_batl_jobs.c \
        src/story/sc_batl_scope.c src/story/sc_batl_jobs.c src/memmap.c src/roguelike/rl_geo_guard.c \
        -Wl,--gc-sections -lm -o "$out/jobs-$engine" > "$out/$engine-build.log" 2>&1
    for c in "${cases[@]}"; do
        ASAN_OPTIONS=detect_leaks=0 "$out/jobs-$engine" "$c" | tee "$out/$engine-$c.log"
    done
done
echo "STORY BATL JOBS PASS"
