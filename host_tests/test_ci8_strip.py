"""Host regression for the fail-closed CI8 strip frame address in fragment 31 (920000:183 callbacks).

func_81002DC0/FC0/31C0/33C0 (called from cmd8 callbacks func_81002F38/03138/03338/03538) address
texture + ((D_862082A0[i] * 4) & ~31) + ((counter & 3) << 11) where D_862082A0 is fragment-8 BSS
written by func_86200EC4. Validator n5-callback-texture-review (sha256 aa9b28de...569c) requires a
positive residency/value/extent proof or checked consumption. Under ROGUELIKE the four sites call
rl_ci_frame, extracted here verbatim and compiled under ASan/UBSan against a modelled fragment map:
  - accepts exactly the vanilla address for every in-domain distance (incl. the 910 cap -> 32760),
    so normal play is unchanged, and the whole 2048-byte frame stays before the palette;
  - stops for non-resident/stale fragment 8, a palette not at the strip end, distances whose
    frame cycle leaves the strip, multiply-wrapping and negative (float->u32) distances.
"""
import pathlib
import re
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / "build" / "host_tests"

text = (ROOT / "src/fragments/31/fragment31_258080.c").read_text()
start = text.index("static u8* rl_ci_frame(")
depth, end = 1, text.index("{", start) + 1
while depth:
    depth += (text[end] == "{") - (text[end] == "}")
    end += 1
helper = text[start:end]
defines = re.findall(r"#define (RL_CI_\w+) (\w+)", text)
assert dict(defines) == {"RL_CI_STRIP_BYTES": "0x23288", "RL_CI_FRAME_BYTES": "0x800"}, defines

SITE = re.compile(r"#ifdef ROGUELIKE\n    gDPLoadTextureTile\(gfx\+\+, rl_ci_frame\(arg1, arg2, (\d), (\w+)\)(, G_IM_FMT_CI.*?\);)\n"
                  r"#else\n    gDPLoadTextureTile\(gfx\+\+, \(arg2 \+ \(\(D_862082A0\[(\d)\] \* 4\) & ~0x1F\) \+ \(\((\w+) & 3\) << 0xB\)\)(, G_IM_FMT_CI.*?\);)\n#endif\n")
sites = SITE.findall(text)
assert [(s[0], s[1]) for s in sites] == [(s[3], s[4]) for s in sites], "fixed site must keep index/counter"
assert [s[0] for s in sites] == ["0", "1", "2", "3"] and all(s[2] == s[5] for s in sites)
assert len(set(s[1] for s in sites)) == 4 and text.count("rl_ci_frame(") == 5

PREFIX = "".join("#define %s %s\n" % d for d in defines) + r"""
#include <assert.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef int s32;
static u32 D_862082A0[4];
static uintptr_t frag8_base;          /* modelled gFragments[0x52]: base of a 0x82b0-byte image */
static u32 frag8_size;
static jmp_buf stop;
static int stops;
static uintptr_t Memmap_GetLoadedFragmentVaddr(uintptr_t addr) {
    if (frag8_base && addr >= frag8_base && addr - frag8_base < frag8_size)
        return 0x86200000U | (u32)(addr - frag8_base);
    return 0;
}
static void rl_ci_fail(void) { ++stops; longjmp(stop, 1); }
"""

SUFFIX = r"""
static u8 strip[RL_CI_STRIP_BYTES + 512];

static int try_frame(u16* palette, s32 index, u32 travel, s32 counter, u8** out) {
    D_862082A0[index] = travel;
    if (setjmp(stop)) return 0;
    *out = rl_ci_frame(palette, strip, index, counter);
    return 1;
}

int main(void) {
    u16* palette = (u16*)(strip + RL_CI_STRIP_BYTES);
    u8* frame;
    u32 travel, max_ok = (RL_CI_STRIP_BYTES - 4 * RL_CI_FRAME_BYTES) / 4;
    s32 i, c, n = 0;
    static const u32 bad[] = { 33955, 0x40000001U, 0x40000000U, 0xFFFFFFF6U, 0x80000000U, 0xFFFFFFFFU };

    /* fragment 8 resident at its relocation binding: D_862082A0 at image offset 0x82a0 */
    frag8_base = (uintptr_t)D_862082A0 - 0x82a0;
    frag8_size = 0x82b0;
    assert(max_ok == 33954);
    for (i = 0; i < 4; i++)
        for (c = -2; c < 6; c++)
            for (travel = 0; travel <= max_ok; travel += (travel < 40 || travel > max_ok - 40) ? 1 : 97, n++) {
                assert(try_frame(palette, i, travel, c, &frame));
                assert(frame == strip + ((travel * 4) & ~0x1F) + ((c & 3) << 0xB));
                assert(frame >= strip && frame + RL_CI_FRAME_BYTES <= (u8*)palette);
            }
    assert(try_frame(palette, 2, 910 * 36, 3, &frame) && frame == strip + 131040 + 6144);
    n++;
    for (i = 0; i < (s32)(sizeof(bad) / sizeof(bad[0])); i++, n++)
        assert(!try_frame(palette, i & 3, bad[i], 0, &frame));
    assert(stops == 6);
    /* palette not at the strip end */
    assert(!try_frame(palette - 1, 0, 0, 0, &frame) && !try_frame((u16*)strip, 0, 0, 0, &frame));
    /* fragment 8 not resident / stale binding / image too short for D_862082A0[3] */
    frag8_base = 0;
    assert(!try_frame(palette, 0, 0, 0, &frame));
    frag8_base = (uintptr_t)D_862082A0 - 0x82a0 + 0x10;
    frag8_size = 0x82b0;
    assert(!try_frame(palette, 0, 0, 0, &frame));
    frag8_base = (uintptr_t)D_862082A0 - 0x82a0;
    frag8_size = 0x82ac;
    assert(!try_frame(palette, 0, 0, 0, &frame));
    /* registered span ends inside D_862082A0[3]: its first byte maps but its 4-byte read would not */
    for (frag8_size = 0x82ad; frag8_size <= 0x82af; frag8_size++, n++)
        assert(!try_frame(palette, 3, 0, 0, &frame));
    frag8_size = 0x82b0;
    assert(try_frame(palette, 3, 0, 0, &frame));
    assert(stops == 14);
    n += 6;
    printf("%d fixtures passed\n", n);
    return 0;
}
"""

OUT.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix="ci8-strip-", dir=str(OUT)) as tmp:
    unit = pathlib.Path(tmp) / "ci8.c"
    unit.write_text(PREFIX + helper + SUFFIX)
    binary = pathlib.Path(tmp) / "ci8"
    subprocess.run(["gcc", "-std=gnu99", "-O1", "-g", "-fsanitize=address,undefined",
                    "-fno-sanitize-recover=all", "-Wall", str(unit), "-o", str(binary)], check=True)
    print("ci8 strip: " + subprocess.run([str(binary)], check=True, capture_output=True,
                                         text=True).stdout.strip())
print("fragment31 CI8 strip frame: OK")
sys.exit(0)
