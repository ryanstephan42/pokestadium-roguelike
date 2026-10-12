"""Host regression for the fragment-31 cmd8 callback func_81000420 (archive 920000:3).

The geo parser invokes cmd8 callbacks at import (phase 0) before a renderer actor is bound, so the
actor getter func_800325B4 (reads D_8006F09C->unk_0A6 through the segment table) must only run in
the drawing phase 2. Extracts the real func_81000420 from the working tree and compiles it with
ROGUELIKE (fixed) and without (vanilla ordering) under ASan/UBSan against labelled seams.

The fixed build must never call the getter for phases other than 2, and must produce exactly the
vanilla phase-2 call order, actor read and texture-index sequence. The vanilla build must
reproduce the phase-0 actor read the validator report (n5-parser-payload-review, sha256
a776d408...a793) identifies, proving the fixture observes the ordering it changes.
"""
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / "build" / "host_tests"

text = (ROOT / "src/fragments/31/fragment31_2554C0.c").read_text()
start = text.index("void func_81000420(s32 arg0, DisplayListState* state) {")
depth, end = 1, text.index("{", start) + 1
while depth:
    depth += (text[end] == "{") - (text[end] == "}")
    end += 1
body = text[start:end]
assert body.count("func_800325B4()") == 2 and "#ifdef ROGUELIKE" in body and "#ifndef ROGUELIKE" in body

PREFIX = r"""
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef int s32;
typedef unsigned char u8;
typedef struct { int w; } Gfx;
typedef struct { int textures[8]; } DisplayListAddresses;
typedef struct { DisplayListAddresses* addresses; Gfx* gfx; } DisplayListState;
static Gfx pool[0x140];
static DisplayListAddresses addrs;
static s32 D_800AF7B0[2] = { 1, 2 };
static const u8* actor;                  /* NULL = no renderer actor bound (geo import) */
static char log_[64];
static int log_n, unbound_reads, alloc_size, last_index;
static void ev(char c) { assert(log_n < 63); log_[log_n++] = c; }
static u8 func_800325B4(void) { ev('G'); if (!actor) { ++unbound_reads; return 0; } return *actor; }
static Gfx* func_80005F5C(int size) { ev('A'); alloc_size = size; return pool; }
static Gfx* func_81000330(Gfx* gfx, DisplayListAddresses* a, s32 index) {
    ev('D'); assert(gfx == pool && a == &addrs); assert(index >= 0 && index < 8); last_index = index; return gfx;
}
"""

SUFFIX = r"""
int main(void) {
    static const u8 actors[2] = { 0, 1 };
    DisplayListState state;
    int i, phase, n = 0;
    char seq[64] = "";
    for (i = 0; i < 6; i++) {
        /* import-time invocations without a bound actor: phases 0, 1, 3 */
        static const int phases[3] = { 0, 1, 3 };
        for (phase = 0; phase < 3; phase++, n++) {
            actor = 0; log_n = 0; memset(log_, 0, sizeof(log_)); state.addresses = &addrs; state.gfx = 0;
            func_81000420(phases[phase], &state);
            assert(state.gfx == 0);
#ifdef ROGUELIKE
            assert(log_n == 0 && unbound_reads == 0);
#else
            assert(!strcmp(log_, "G"));
#endif
        }
        /* drawing phase with a bound actor */
        actor = &actors[i & 1]; log_n = 0; memset(log_, 0, sizeof(log_));
        func_81000420(2, &state);
        assert(!strcmp(log_, "GAD") && state.gfx == pool && alloc_size == 0x140);
        seq[i] = (char)('0' + last_index);
        n++;
    }
#ifndef ROGUELIKE
    assert(unbound_reads == 18);
#endif
    printf("%d fixtures passed seq=%s unbound_reads=%d\n", n, seq, unbound_reads);
    return 0;
}
"""


def build_and_run(directory, label, extra):
    unit = directory / (label + ".c")
    unit.write_text(PREFIX + body + SUFFIX)
    binary = directory / label
    subprocess.run(["gcc", "-std=gnu99", "-O1", "-g", "-fsanitize=address,undefined",
                    "-fno-sanitize-recover=all", "-Wall"] + extra + [str(unit), "-o", str(binary)],
                   check=True)
    out = subprocess.run([str(binary)], check=True, capture_output=True, text=True).stdout.strip()
    print(label + ": " + out)
    return out.split("seq=")[1].split()[0]


OUT.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix="phase0-getter-", dir=str(OUT)) as tmp:
    fixed = build_and_run(pathlib.Path(tmp), "roguelike", ["-DROGUELIKE"])
    vanilla = build_and_run(pathlib.Path(tmp), "vanilla-order", [])
assert fixed == vanilla == "375173", (fixed, vanilla)
print("phase0 actor getter: OK")
sys.exit(0)
