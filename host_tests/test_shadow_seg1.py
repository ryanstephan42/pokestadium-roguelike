#!/usr/bin/env python3
"""HOST test of the ROGUELIKE segment-1 shadow-node admission in func_80014D70 (src/12D80.c).

The real rl_shadow_node_ok helper, the real func_80014D70, Memmap_SetSegmentMap/
Memmap_GetSegmentBaseVaddr/Memmap_GetSegmentSize/Memmap_GetSegmentVaddr and
Memmap_ClearSegmentMemmap (src/memmap.c) are extracted and built with -m32 ASan/UBSan, ROGUELIKE
and vanilla. The rest of the engine is stubbed. A modelled RSP consumer follows the emitted
G_BRANCH_LIST into segment 1 and reads exactly what the D_1002480/D_1002508 shadow DLs read: the DL
words [0x2480,0x2508)/[0x2508,0x2590), the IA16 texture [0x2000,0x2200)/[0x2200,0x2400) and 4
vertices [0x2400,0x2440). Segment 1 is a heap block of exactly the registered size, so an
out-of-extent read is an ASan error.

Checks:
  - resident segment 1 with 0x3F20 (installed asset) or 0x2590 bytes: both branches draw, and the
    trace is identical to vanilla;
  - unmapped segment 1, a 0x258F-byte segment, a cleared segment, a NULL base with a nonzero
    size, or a NULL func_80005F5C(16)
    result: ROGUELIKE refuses with exactly the flag-off trace (no unk_10A0 change, no DL words, no
    child traversal); vanilla faults (unresolved segment, ASan overflow, or NULL write);
  - mutants that drop the NULL check, the base check, or lower the bound to 0x258F are rejected.
The 0x2590 bound is uniform for both branches; branch 0 alone needs 0x2508. Reachability of a
short/missing segment 1 is not asserted.
"""
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / "build" / "host_tests"

text = (ROOT / "src/12D80.c").read_text()
mm = (ROOT / "src/memmap.c").read_text()


def function(src, sig):
    start = src.index(sig)
    depth, end = 1, src.index("{", start) + 1
    while depth:
        depth += (src[end] == "{") - (src[end] == "}")
        end += 1
    return src[start:end] + "\n"


hstart = text.index("#ifdef ROGUELIKE\n/*\n * func_80014D70 branches")
helper = text[hstart:text.index("#endif\n", hstart) + len("#endif\n")]
assert "static s32 rl_shadow_node_ok(Gfx* dl)" in helper and "#define RL_SHADOW_SEG1_END 0x2590" in helper
n0 = text.index("void func_80014D70(GraphNode* arg0) {")
node = text[n0:text.index("\n}\n", n0) + 3]
assert node.count("rl_shadow_node_ok(temp_s1)") == 1
memmap = "".join(function(mm, sig) for sig in (
    "void Memmap_SetSegmentMap(u32 id, uintptr_t vaddr, size_t size) {",
    "uintptr_t Memmap_GetSegmentBaseVaddr(u32 id) {",
    "size_t Memmap_GetSegmentSize(u32 id) {",
    "uintptr_t Memmap_GetSegmentVaddr(u32 mask) {",
    "void Memmap_ClearSegmentMemmap(u32 id) {"))
memmap = memmap.replace("size_t Memmap_GetSegmentSize", "#ifdef ROGUELIKE\nsize_t Memmap_GetSegmentSize", 1)
memmap = memmap.replace("}\nuintptr_t Memmap_GetSegmentVaddr", "}\n#endif\nuintptr_t Memmap_GetSegmentVaddr", 1)

PREFIX = r"""
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef short s16;
typedef signed char s8;
typedef int s32;
typedef float f32;
_Static_assert(sizeof(void*) == 4, "N64 pointer width");
#define NULL_ ((void*)0)
typedef union { struct { u32 w0, w1; } words; long long force; } Gfx;
typedef struct { f32 mf[4][4]; } MtxF;
typedef struct { f32 x, y, z; } Vec3f;
typedef struct { s16 x, y, z; } Vec3s;
typedef struct { u8 pad[0x18]; } GraphNode;
typedef struct { GraphNode unk_00; s16 unk_18, unk_1A, unk_1C, unk_1E; } unk_D_86002F34_alt12;
typedef struct { struct { u8 unk_00, unk_01, unk_02; } unk_000; u8 unk_01D; Vec3s unk_01E; } Actor;
typedef struct {
    MtxF unk_0000[64];
    MtxF* unk_1000[32];
    s8 unk_1080[32];
    s32 unk_10A0;
} unk_D_800AA8C8;
struct MemoryMap { uintptr_t vaddr; size_t size; };
struct MemoryMap gSegments[16];
static unk_D_800AA8C8 D_800AA8C8;
static Actor actor, *D_8006F09C = &actor;
static Gfx dl_buf[64], *gDisplayListHead = dl_buf;
#define D_1002480 ((Gfx*)0x01002480)
#define D_1002508 ((Gfx*)0x01002508)
#define gSPBranchList(p, a) do { Gfx* _g = (p); _g->words.w0 = 0xDE010000u; _g->words.w1 = (u32)(a); } while (0)
#define gDPSetEnvColor(p, r, g, b, a) do { Gfx* _g = (p); _g->words.w0 = 0xFB000000u; _g->words.w1 = (u32)(a) & 0xFF; } while (0)
#define gDPSetFogColor(p, r, g, b, a) do { Gfx* _g = (p); _g->words.w0 = 0xF8000000u; _g->words.w1 = 0xFFFFFF00u; } while (0)
#define gDPSetPrimColor(p, m, l, r, g, b, a) do { Gfx* _g = (p); _g->words.w0 = 0xFA000000u | (l); _g->words.w1 = 0xFFFFFFFFu; } while (0)

static int g_alloc_fail, g_children;
static Gfx g_alloc[2];
static Gfx* func_80005F5C(s32 size) {
    if (size != 16) abort();
    return g_alloc_fail ? NULL : g_alloc;
}
static s32 func_80015390(Actor* a, s32 id, void* p) { (void)a; (void)id; (void)p; return 1; }
static void func_80014CB8(s32 id) { (void)id; }
static void func_8000E88C(Vec3f* v, f32 x, f32 y, f32 z) { v->x = x; v->y = y; v->z = z; }
static void func_8000FF04(MtxF* m, Vec3f* v) { (void)m; v->x = v->y = v->z = 1.0f; }
static void func_8000FA94(MtxF* m, Vec3f* a, Vec3f* b, s16 r) { (void)a; (void)b; (void)r; memset(m, 0, sizeof(*m)); }
static void func_8000FDE4(MtxF* d, MtxF* s, Vec3f* v) { (void)v; *d = *s; }
static void func_800121C8(void) { }
static void func_800160E0(s32 l, MtxF* m) { (void)m; printf("mtx-layer %d\n", l); }
static void func_80013330(GraphNode* n) { (void)n; g_children++; printf("children 10A0=%d\n", D_800AA8C8.unk_10A0); }
size_t Memmap_GetSegmentSize(u32 id);
uintptr_t Memmap_GetSegmentBaseVaddr(u32 id);
uintptr_t Memmap_GetSegmentVaddr(u32 mask);
static uintptr_t Util_ConvertAddrToVirtAddr(uintptr_t addr) { return Memmap_GetSegmentVaddr(addr); }

/* RSP model: follow the branch and read the DL, its IA16 texture and its 4 vertices from segment 1 */
static volatile u32 g_sum;
static void rsp_read(uintptr_t base, u32 lo, u32 hi) {
    for (u32 o = lo; o < hi; o++) g_sum += ((volatile u8*)base)[o];
}
static void func_80016274(u32 phys, s32 layer) {
    if (phys != ((u32)g_alloc & 0x1FFFFFFF)) { printf("FAULT emitted DL is not the node allocation\n"); exit(4); }
    Gfx* dl = g_alloc;
    printf("emit layer %d env %u\n", layer, dl[0].words.w1);
    u32 t = dl[1].words.w1;
    uintptr_t base = gSegments[1].vaddr;
    if (t >> 24 == 0x01) { printf("FAULT unresolved segment-1 branch %08x\n", t); exit(3); }
    u32 off = t - (u32)base;
    u32 tex = off == 0x2480 ? 0x2000 : 0x2200;
    rsp_read(base, off, off + 0x88);
    rsp_read(base, tex, tex + 0x200);
    rsp_read(base, 0x2400, 0x2440);
    printf("drawn dl %04x tex %04x\n", off, tex);
}
"""

SUFFIX = r"""
static void call(const char* what, s16 which) {
    unk_D_86002F34_alt12 node;
    memset(&node, 0, sizeof(node));
    node.unk_18 = which; node.unk_1A = 100; node.unk_1C = 100;
    D_800AA8C8.unk_10A0 = 0;
    gDisplayListHead = dl_buf;
    g_children = 0;
    memset(g_alloc, 0, sizeof(g_alloc));
    printf("== %s branch %d\n", what, which);
    func_80014D70((GraphNode*)&node);
    printf("after 10A0=%d dl=%d children=%d\n", D_800AA8C8.unk_10A0, (int)(gDisplayListHead - dl_buf), g_children);
}

static void map1(size_t size) {
    void* p = malloc(size);
    memset(p, 0x5A, size);
    Memmap_SetSegmentMap(1, (uintptr_t)p, size);
}

int main(int argc, char** argv) {
    const char* c = argv[1];
    for (int i = 0; i < 64; i++) D_800AA8C8.unk_1000[i & 31] = &D_800AA8C8.unk_0000[i];
    actor.unk_000.unk_02 = 0x40;
    if (!strcmp(c, "installed")) { map1(0x3F20); call(c, 0); call(c, 1); return 0; }
    if (!strcmp(c, "exact")) { map1(0x2590); call(c, 0); call(c, 1); return 0; }
    if (!strcmp(c, "flagoff")) { map1(0x3F20); actor.unk_000.unk_02 = 0; call(c, 0); call(c, 1); return 0; }
    if (!strcmp(c, "unmapped")) { call(c, 0); return 0; }
    if (!strcmp(c, "short")) { map1(0x258F); call(c, 1); return 0; }
    if (!strcmp(c, "cleared")) { map1(0x3F20); Memmap_ClearSegmentMemmap(1); call(c, 0); return 0; }
    if (!strcmp(c, "nullbase")) { Memmap_SetSegmentMap(1, 0, 0x3F20); call(c, 0); return 0; }
    if (!strcmp(c, "nullalloc")) { map1(0x3F20); g_alloc_fail = 1; call(c, 0); return 0; }
    if (!strcmp(c, "reacquire")) {
        map1(0x258F); call("short", 1);
        map1(0x3F20); call("remapped", 1);
        return 0;
    }
    return 2;
}
"""

CFLAGS = ["gcc", "-m32", "-std=gnu99", "-O1", "-g", "-fsanitize=address,undefined",
          "-fno-sanitize-recover=all", "-Wno-int-conversion", "-Wno-pointer-to-int-cast",
          "-Wno-int-to-pointer-cast", "-Wno-incompatible-pointer-types", "-w"]
ENV = {"ASAN_OPTIONS": "detect_leaks=0:handle_segv=1", "UBSAN_OPTIONS": "print_stacktrace=1"}
SAFE = ["installed", "exact", "flagoff"]
UNSAFE = ["unmapped", "short", "cleared", "nullbase", "nullalloc"]


def build(tmp, name, source, roguelike):
    unit = pathlib.Path(tmp) / f"{name}.c"
    unit.write_text(PREFIX + memmap + source + SUFFIX)
    exe = pathlib.Path(tmp) / name
    subprocess.run(CFLAGS + (["-DROGUELIKE"] if roguelike else []) + [str(unit), "-o", str(exe)], check=True)
    return exe


def run(exe, case):
    return subprocess.run([str(exe), case], capture_output=True, text=True, env=ENV)


def flagoff_shape(out, case):
    return [l.replace("flagoff", case) for l in out.splitlines()]


OUT.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix="shadow-seg1-", dir=str(OUT)) as tmp:
    fixed = build(tmp, "fixed", helper + node, True)
    vanilla = build(tmp, "vanilla", helper + node, False)

    for case in SAFE:
        f, v = run(fixed, case), run(vanilla, case)
        assert f.returncode == 0 and v.returncode == 0, (case, f.stderr[-800:], v.stderr[-800:])
        assert f.stdout == v.stdout, case
    drawn = run(fixed, "installed").stdout
    assert "drawn dl 2480 tex 2000" in drawn and "drawn dl 2508 tex 2200" in drawn
    assert "children 10A0=1" in drawn and drawn.count("after 10A0=0 dl=2 children=1") == 2
    assert "drawn dl 2508 tex 2200" in run(fixed, "exact").stdout

    off = run(fixed, "flagoff").stdout.splitlines()
    assert all(l.startswith(("==", "after 10A0=0 dl=0 children=0")) for l in off)
    for case in UNSAFE:
        f, v = run(fixed, case), run(vanilla, case)
        assert f.returncode == 0, (case, f.stderr[-800:])
        which = 1 if case == "short" else 0
        assert f.stdout.splitlines() == [f"== {case} branch {which}", "after 10A0=0 dl=0 children=0"], (case, f.stdout)
        assert v.returncode != 0, (case, "vanilla must fault", v.stdout)
        assert ("FAULT unresolved" in v.stdout or "AddressSanitizer" in v.stderr), (case, v.stderr[-400:])
    assert "heap-buffer-overflow" in run(vanilla, "short").stderr
    assert "SEGV" in run(vanilla, "nullalloc").stderr
    re_out = run(fixed, "reacquire").stdout
    assert "after 10A0=0 dl=0 children=0" in re_out and "drawn dl 2508 tex 2200" in re_out

    mutants = {
        "no-null-check": ("(dl != NULL) && ", ""),
        "no-base-check": ("(Memmap_GetSegmentBaseVaddr(1) != 0) && ", ""),
        "bound-258f": ("#define RL_SHADOW_SEG1_END 0x2590", "#define RL_SHADOW_SEG1_END 0x258F"),
    }
    for name, (a, b) in mutants.items():
        assert a in helper, name
        exe = build(tmp, name, helper.replace(a, b) + node, True)
        caught = [c for c in UNSAFE if run(exe, c).returncode != 0]
        assert caught, f"mutant {name} not rejected"
        print(f"mutant {name}: rejected by {caught}")

print(f"shadow-seg1 OK: safe {SAFE} identical to vanilla; refused {UNSAFE} with flag-off trace; vanilla faults")
