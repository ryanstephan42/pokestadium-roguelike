"""Host regression for the fail-closed shadow-slot counter in src/334D0.c.

D_800AF7B0[row] (s16) selects one of three StadiumModelSlot entries in D_800AFFC8[row][3]. It is
incremented on every func_80032F94 call and only reset by func_80032670 on one draw path, so no
source invariant bounds it below 3. Slots are populated only by func_80033B2C (modelSegment =
&slot.unkSegment); a never-populated slot is zero BSS. Under ROGUELIKE, rl_shadow_slot refuses an
out-of-range counter for the writer func_80032BD4, and an out-of-range counter or an unpopulated
slot for the consumers func_80032D28 (func_800361C4), func_80032F94 mode 2/3 (seg 6 + slot DL) and
mode 0/1 case 2/3 (func_800366A4, which also refuses a NULL 0xA0 vertex buffer).

The real functions are extracted from the working tree and compiled -m32 under ASan/UBSan with
ROGUELIKE (fixed) and without (vanilla) against labelled seams:
  - every in-domain call (counter 0..2, populated slot, vertex buffer allocated) produces a trace
    identical to vanilla: Gfx words, slot accesses, allocations and counter updates;
  - out-of-range counters (3, 4, 0x7FFF, -1, -0x8000) and unpopulated slots touch no slot and
    emit no slot Gfx, while func_80032F94 still advances the counter exactly as vanilla;
  - vanilla reproduces UBSan's index-3 bounds fault for D_800AFFC8[0][3] (in-object alias of
    D_800AFFC8[1][0]) and for D_800AFFC8[1][3] (past the array), proving the fixture
    observes what the guard changes.
  - an out-of-range counter latches its row dead (rl_shadow_dead) until the real func_80032670
    reset (extracted from src/32D10.c) clears it: 65533 refusals from 3 wrap the s16 counter back to
    0, where vanilla re-admits the stale self-marked slot 0 but the fixed build keeps refusing; after
    the reset the row repopulates and draws again, and the other row is unaffected. Mutants that
    drop the latch or the reset clear are rejected.
No canonical reachability of an out-of-range counter is asserted.
"""
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / "build" / "host_tests"

text = (ROOT / "src/334D0.c").read_text()


def function(sig):
    start = text.index(sig)
    depth, end = 1, text.index("{", start) + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end] + "\n"


hstart = text.index("#ifdef ROGUELIKE\n/*\n * D_800AF7B0[row]")
helper = text[hstart:text.index("#endif\n", hstart) + len("#endif\n")]
assert "static StadiumModel* rl_shadow_slot(s32 row, s16 counter, s32 requirePopulated)" in helper
assert text.count("rl_shadow_slot(") == 5
assert "rl_shadow_dead[row] = 1;" in helper and "rl_shadow_dead[row & 1] = 0;" in helper
t32 = (ROOT / "src/32D10.c").read_text()
r0 = t32.index("void func_80032670(u32 arg0, u32 arg1) {")
r1 = t32.index("\n}\n", r0) + 3
reset_fn = t32[r0:r1]
assert "rl_shadow_slot_reset(arg0);" in reset_fn and "D_800AF7B0[arg0 & 1] = 0;" in reset_fn
body = (helper + function("void func_80032BD4(void) {") + function("void func_80032D28(void) {")
        + function("Gfx* func_80032F94(Gfx* gfx) {") + reset_fn)

PREFIX = r"""
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef short s16;
typedef int s32;
typedef float f32;
_Static_assert(sizeof(void*) == 4, "N64 pointer width");
typedef union { struct { u32 w0, w1; } words; long long force; } Gfx;
typedef struct { f32 m[4][4]; } MtxF;
typedef struct { MtxF mtx[16]; } MtxBuffer;
typedef struct { s16 ob[3]; u16 flag; s16 tc[2]; u8 cn[4]; } Vtx;
typedef struct ModelSegment {
    u16 type, nodeCount, vertexCount, triangleCount;
    u32 indexSegment, unk_0C, tableSegment, remapSegment, vertexSegment;
    Gfx* displayList;
} ModelSegment;
typedef struct StadiumModel {
    s16 unk_00;
    s16 unk_02;
    u8 transforms[0x12C];
    u32 modelSegment;
    u8 pad134[0x4];
    u32 unkSegment;
    u8 pad13C[0x1C];
    f32 position[3];
    u8 pad164[0xC];
    u8 mvtx[0x94 * 10];
} StadiumModel;
_Static_assert(__builtin_offsetof(StadiumModel, modelSegment) == 0x130, "layout");
_Static_assert(__builtin_offsetof(StadiumModel, unkSegment) == 0x138, "layout");
_Static_assert(__builtin_offsetof(StadiumModel, mvtx) == 0x170, "layout");
typedef struct { StadiumModel model; u8 pad[0x7D0 - sizeof(StadiumModel)]; } StadiumModelSlot;
_Static_assert(sizeof(StadiumModelSlot) == 0x7D0, "slot size");
typedef struct { u8 pad[0x1A]; s16 unk_01A; } Actor;
typedef struct { u8 unk_00[0xA8]; } unk_D_800B2EA8;

static Actor actor, *D_8006F09C = &actor;
s16 D_800AF7B0[2];
s16 D_800AF7C0[4];
static MtxBuffer D_800AF7C8[2];
static StadiumModelSlot D_800AFFC8[2][3];
static unk_D_800B2EA8 D_800B2EA8[1];
s32 D_80076270[16];
Gfx D_800762B0[6];
u32 D_800777A8[8], D_80077950[8];

static void func_810007F8(void) { }
static void* Util_ConvertAddrToVirtAddr(void* f) { return f; }
#ifndef ROGUELIKE
void rl_shadow_slot_reset(u32 row) { (void)row; }
#endif
static u8 g_mode;
static int g_alloc_fail;
static u8 g_vtx_arena[0x1000];
static u32 g_vtx_top;
static u8 func_800325B4(void) { return g_mode; }
static s32 func_800325AC(void) { return 0; }
static void trace(const char* what, StadiumModel* m);
static void func_800328E4(u8 i) { printf("mtx-a %u\n", i); }
static void func_80032990(u8 i) { printf("mtx-b %u\n", i); }
static void func_80032940(u8 i) { printf("mtx-c %u\n", i); }
static void func_80033B2C(StadiumModel* model, void* a1, void* a2, void* tmpl);
static void func_800361C4(StadiumModel* model, void* mtx) { (void)mtx; trace("361C4", model); }
static void* func_80005F5C(s32 size) {
    printf("alloc %d\n", size);
    if (g_alloc_fail) return NULL;
    g_vtx_top += (u32)size;
    assert(g_vtx_top <= sizeof(g_vtx_arena));
    return g_vtx_arena + g_vtx_top - size;
}
static Gfx* func_80032A7C(Gfx* gfx) {
    for (int i = 0; i < 3; i++) { gfx->words.w0 = 0xA7C00000u | i; gfx->words.w1 = 0; gfx++; }
    return gfx;
}
static Gfx* func_80032E00(Gfx* gfx) {
    for (int i = 0; i < 8; i++) { gfx->words.w0 = 0x2E000000u | i; gfx->words.w1 = 0; gfx++; }
    return gfx;
}
static Gfx* func_800366A4(Gfx* gfx, StadiumModel* model, void* vtx) {
    assert(vtx != NULL);
    trace("366A4", model);
    for (int i = 0; i < 4; i++) { gfx->words.w0 = 0x366A0000u | i; gfx->words.w1 = model->unkSegment; gfx++; }
    return gfx;
}
"""

SUFFIX = r"""
static u32 sym(u32 w) {
    if (w == (u32)D_800762B0) return 0x762B0;
    if (w == (u32)D_80076270) return 0x76270;
    return w;
}
static void trace(const char* what, StadiumModel* m) {
    uintptr_t off = (uintptr_t)m - (uintptr_t)D_800AFFC8;
    /* consumers may only see a populated, in-bounds slot */
    assert(off % sizeof(StadiumModelSlot) == 0);
    printf("%s slot %u populated %d\n", what, (unsigned)(off / sizeof(StadiumModelSlot)),
           m->modelSegment == (u32)&m->unkSegment);
    if (m->modelSegment != (u32)&m->unkSegment) abort();
}
static void func_80033B2C(StadiumModel* model, void* a1, void* a2, void* tmpl) {
    ModelSegment* segment = (ModelSegment*)&model->unkSegment;
    (void)a1; (void)a2;
    printf("copy %s\n", tmpl == D_80077950 ? "950" : "7A8");
    {
        uintptr_t off = (uintptr_t)model - (uintptr_t)D_800AFFC8;
        printf("  into slot %u\n", (unsigned)(off / sizeof(StadiumModelSlot)));
    }
    memcpy(segment, tmpl, 4 * 4);   /* header only: vertexSegment/displayList stay per-template below */
    segment->vertexSegment = 0x0E000000u | (tmpl == D_80077950 ? 0x950 : 0x7A8);
    segment->displayList = (Gfx*)(tmpl == D_80077950 ? 0x80093e50u : 0x80093ca8u);
    model->unk_00 = 1;
    model->unk_02 = 0;
    model->modelSegment = (u32)segment;
}
static Gfx out[64];
static void call(const char* tag) {
    Gfx* end;
    s16 before = D_800AF7B0[g_mode & 1];
    memset(out, 0, sizeof(out));
    g_vtx_top = 0;
    end = func_80032F94(out);
    printf("%s mode %u C0 %d B0 %d->%d gfx %d\n", tag, g_mode, D_800AF7C0[g_mode & 1], before,
           D_800AF7B0[g_mode & 1], (int)(end - out));
    for (Gfx* g = out; g < end; g++) printf("  %08x %08x\n", g->words.w0, sym(g->words.w1));
}
static void reset(void) {
    memset(D_800AFFC8, 0, sizeof(D_800AFFC8));
    func_80032670(0, 0);
    func_80032670(1, 0);
    assert(D_800AF7B0[0] == 0 && D_800AF7B0[1] == 0);
    memset(D_800AF7C0, 0, sizeof(D_800AF7C0));
    g_vtx_top = 0;
    g_alloc_fail = 0;
}

int main(int argc, char** argv) {
    const char* which = argc > 1 ? argv[1] : "domain";
    D_800777A8[0] = 0x000a0000; D_800777A8[1] = 0x000a0008;
    D_80077950[0] = 0x000b0000; D_80077950[1] = 0x000a0008;
    if (!strcmp(which, "domain")) {
        /* vanilla lifecycle per row: state 1 populates k slots; states 2/3 consume them */
        for (int row = 0; row < 2; row++)
            for (int k = 1; k <= 3; k++)
                for (int kind = 0; kind < 2; kind++) {
                    reset();
                    actor.unk_01A = kind ? 0x92 : 0x10;
                    for (int state = 0; state <= 3; state++) {
                        D_800AF7C0[row] = state;
                        D_800AF7B0[row] = 0;
                        for (int i = 0; i < k; i++) {
                            g_mode = row;
                            func_80032D28();
                            call("draw");
                        }
                        /* the mode 2/3 alias of the same row consumes the same slots */
                        D_800AF7B0[row] = 0;
                        for (int i = 0; i < k; i++) { g_mode = row + 2; call("alias"); }
                    }
                }
        /* untouched modes: FF/FE draw 32E00, >= 4 nothing, mode 2/3 below state 2 nothing */
        reset();
        g_mode = 0xFF; call("ff"); g_mode = 0xFE; call("fe");
        g_mode = 4; call("m4"); g_mode = 2; D_800AF7C0[0] = 1; call("m2-low");
        D_800AF7C0[0] = -3; g_mode = 0; call("default");
        return 0;
    }
    if (!strcmp(which, "refuse")) {
        static const s16 bad[] = { 3, 4, 0x7FFF, -1, -0x8000 };
        /* every slot populated so only the counter domain decides */
        for (int row = 0; row < 2; row++)
            for (int s = 0; s < 3; s++)
                func_80033B2C(&D_800AFFC8[row][s].model, 0, 0, D_800777A8);
        for (int row = 0; row < 2; row++)
            for (unsigned b = 0; b < sizeof(bad) / sizeof(bad[0]); b++)
                for (int state = 1; state <= 3; state++) {
                    D_800AF7C0[row] = state;
                    D_800AF7B0[row] = bad[b]; g_mode = row; func_80032D28(); call("bad");
                    assert(D_800AF7B0[row] == (s16)(bad[b] + 1));
                    D_800AF7B0[row] = bad[b]; g_mode = row + 2; call("bad-alias");
                    assert(D_800AF7B0[row] == (s16)(state >= 2 ? bad[b] + 1 : bad[b]));
                }
        /* in-range but never-populated slot: writer populates, consumers refuse */
        reset();
        for (int row = 0; row < 2; row++) {
            D_800AF7C0[row] = 2;
            g_mode = row; func_80032D28(); call("empty");
            g_mode = row + 2; D_800AF7B0[row] = 0; call("empty-alias");
            D_800AF7C0[row] = 1; D_800AF7B0[row] = 0; g_mode = row; call("populate");
            D_800AF7C0[row] = 2; D_800AF7B0[row] = 0; g_mode = row; func_80032D28(); call("now-full");
        }
        /* vertex buffer exhausted */
        g_alloc_fail = 1;
        D_800AF7C0[0] = 3; D_800AF7B0[0] = 0; g_mode = 0; call("no-vtx");
        return 0;
    }
    if (!strcmp(which, "wrap")) {
        /* populate row r's three slots, then refuse from counter 3 until the s16 wraps to 0 */
        for (int r = 0; r < 2; r++)
            for (int path = 0; path < 2; path++) {
                reset();
                D_800AF7C0[r] = 1;
                for (int i = 0; i < 3; i++) { g_mode = r; call("fill"); }
                assert(D_800AF7B0[r] == 3);
                D_800AF7C0[r] = 2;
#ifndef ROGUELIKE
                /* vanilla faults on the first out-of-range index (see alias/overflow), so model
                   the post-wrap state directly: same slots, counter back at 0 */
                D_800AF7B0[r] = 0;
#else
                for (long i = 0; i < 65533; i++) {
                    g_mode = path ? r + 2 : r;
                    g_vtx_top = 0;
                    Gfx* end = func_80032F94(out);
                    assert(end == out);
                }
#endif
                assert(D_800AF7B0[r] == 0);
                g_mode = path ? r + 2 : r;
                func_80032D28();
                call("after-wrap");
                /* the other row is never latched */
                D_800AF7C0[r ^ 1] = 1; D_800AF7B0[r ^ 1] = 0; g_mode = r ^ 1; call("other-fill");
                D_800AF7C0[r ^ 1] = 2; D_800AF7B0[r ^ 1] = 0; g_mode = r ^ 1; func_80032D28(); call("other-draw");
                /* vanilla reset re-arms the row: repopulate slot 0, then draw it */
                func_80032670(r, 0);
                D_800AF7C0[r] = 1; g_mode = r; call("refill");
                func_80032670(r, 0);
                D_800AF7C0[r] = 2; g_mode = path ? r + 2 : r; func_80032D28(); call("reacquire");
            }
        return 0;
    }
    if (!strcmp(which, "alias")) {
        reset();
        D_800AF7C0[0] = 1; D_800AF7B0[0] = 3; g_mode = 0; call("alias03");
        return 0;
    }
    if (!strcmp(which, "overflow")) {
        reset();
        D_800AF7C0[1] = 1; D_800AF7B0[1] = 3; g_mode = 1; call("overflow13");
        return 0;
    }
    return 2;
}
"""

CFLAGS = ["gcc", "-m32", "-std=gnu99", "-O1", "-g", "-fsanitize=address,undefined",
          "-fno-sanitize-recover=all", "-fpermissive", "-Wno-int-conversion",
          "-Wno-unused-label", "-Wno-pointer-to-int-cast", "-Wno-int-to-pointer-cast", "-Wno-incompatible-pointer-types"]
ENV = {"ASAN_OPTIONS": "detect_leaks=0", "UBSAN_OPTIONS": "print_stacktrace=1"}


def run(binary, mode, check=True):
    return subprocess.run([str(binary), mode], capture_output=True, text=True, check=check, env=ENV)


OUT.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix="shadow-slot-", dir=str(OUT)) as tmp:
    unit = pathlib.Path(tmp) / "slot.c"
    unit.write_text(PREFIX + body + SUFFIX)
    fixed, vanilla = pathlib.Path(tmp) / "fixed", pathlib.Path(tmp) / "vanilla"
    subprocess.run(CFLAGS + ["-DROGUELIKE", str(unit), "-o", str(fixed)], check=True)
    subprocess.run(CFLAGS + [str(unit), "-o", str(vanilla)], check=True)

    fdom, vdom = run(fixed, "domain").stdout, run(vanilla, "domain").stdout
    assert fdom == vdom, "in-domain trace differs from vanilla"
    assert fdom.count("361C4") == 2 * 2 * 2 * 6 and fdom.count("366A4") == 2 * 2 * 2 * 6
    assert fdom.count("into slot") == 2 * 2 * 6

    ref = run(fixed, "refuse").stdout
    assert "361C4" not in ref.split("empty")[0] and "366A4" not in ref.split("empty")[0]
    assert ref.count("into slot") == 6 + 2
    bad_lines = [l for l in ref.splitlines() if l.startswith("bad")]
    assert len(bad_lines) == 2 * 5 * 3 * 2 and all(l.endswith("gfx 0") for l in bad_lines), bad_lines
    for tag in ("empty ", "empty-alias "):
        lines = [l for l in ref.splitlines() if l.startswith(tag)]
        assert len(lines) == 2 and all(l.endswith("gfx 0") for l in lines), lines
    full = [l for l in ref.splitlines() if l.startswith("now-full")]
    assert len(full) == 2 and all(l.endswith("gfx 10") for l in full), full
    assert ref.count("366A4 slot") == 2 and ref.count("361C4 slot") == 2
    nov = [l for l in ref.splitlines() if l.startswith("no-vtx")]
    assert nov == ["no-vtx mode 0 C0 3 B0 0->1 gfx 0"], nov

    # vanilla observes what the guard changes: [0][3] (alias of [1][0]) and [1][3] (past the array)
    alias = run(vanilla, "alias", check=False)
    assert alias.returncode != 0 and "index 3 out of bounds" in alias.stderr, alias.stderr[-2000:]
    assert "into slot" not in run(fixed, "alias").stdout
    ovf = run(vanilla, "overflow", check=False)
    assert ovf.returncode != 0 and "index 3 out of bounds" in ovf.stderr, ovf.stderr[-2000:]
    assert run(fixed, "overflow").returncode == 0

    # sticky refusal across the s16 wrap, row independence, reset re-arm
    wrap = run(fixed, "wrap").stdout.splitlines()
    aw = [l for l in wrap if l.startswith("after-wrap")]
    assert len(aw) == 4 and all(l.endswith("gfx 0") for l in aw), aw
    od = [l for l in wrap if l.startswith("other-draw")]
    assert len(od) == 4 and all(not l.endswith("gfx 0") for l in od), od
    rq = [l for l in wrap if l.startswith("reacquire")]
    assert len(rq) == 4 and all(not l.endswith("gfx 0") for l in rq), rq
    assert rq == [l for l in run(vanilla, "wrap").stdout.splitlines() if l.startswith("reacquire")]
    vaw = [l for l in run(vanilla, "wrap").stdout.splitlines() if l.startswith("after-wrap")]
    assert len(vaw) == 4 and all(not l.endswith("gfx 0") for l in vaw), vaw

    for name, a, b in (("no-latch", "rl_shadow_dead[row] = 1;", ";"),
                       ("no-reset-clear", "rl_shadow_dead[row & 1] = 0;", ";")):
        assert a in helper
        mbin = pathlib.Path(tmp) / name
        (pathlib.Path(tmp) / (name + ".c")).write_text(PREFIX + body.replace(a, b) + SUFFIX)
        subprocess.run(CFLAGS + ["-DROGUELIKE", str(pathlib.Path(tmp) / (name + ".c")), "-o", str(mbin)], check=True)
        mw = run(mbin, "wrap", check=False).stdout.splitlines()
        maw = [l for l in mw if l.startswith("after-wrap")]
        mrq = [l for l in mw if l.startswith("reacquire")]
        caught = any(not l.endswith("gfx 0") for l in maw) or any(l.endswith("gfx 0") for l in mrq) or len(mrq) != 4
        assert caught, "mutant %s not rejected" % name

    n = len(fdom.splitlines())
    print("shadow slot: in-domain trace %d lines identical; %d refusals; vanilla alias+overflow reproduced"
          % (n, len(bad_lines) + 6))
    print("shadow slot: 4 wrap histories refused after 65533 refusals (vanilla re-admits), reset re-arms,"
          " other row unaffected; 2 latch mutants rejected")
print("334D0 shadow slot counter: OK")
sys.exit(0)
