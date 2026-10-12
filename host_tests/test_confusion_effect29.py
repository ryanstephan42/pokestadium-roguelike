"""Host regression for the effect-0x1D Substitute-break dispatch in func_843703BC.

Extracts the real func_843703BC, func_8437670C, func_84376768 and func_84374E08 bodies from the
working tree and compiles them (ROGUELIKE, ASan/UBSan) against labelled seams for messages, RNG,
relics and the effect table. Fixture design follows the UI review n5-confusion-disposition
(sha256 4de69498...0648): ordinary/recoil keep D_843C5238 == current actor, the confusion self-hit
(phase 0x14) swaps D_843C5238/523C without flipping the current actor.

A mutated copy that restores the vanilla func_84376768() call must reproduce the defect
(campaign fault for current effect 0, wrong valid handler for current effect 1), so the fixture
proves it can observe the mismatch the fix removes.
"""
import pathlib
import re
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / "build" / "host_tests"


def function(text, name):
    start = text.index("void " + name + "(void) {")
    depth, end = 1, text.index("{", start) + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


damage = function((ROOT / "src/fragments/62/fragment62_359F90.c").read_text(), "func_843703BC")
effects = (ROOT / "src/fragments/62/fragment62_35DF70.c").read_text()
handler = function(effects, "func_84374E08")
dispatch = function(effects, "func_8437670C")
wrapper = function(effects, "func_84376768")
assert re.search(r"D_8438AE54\[\] = \{\s*(\w+,\s*){28}func_84374E08,", effects), "slot 0x1C"
GATED = re.compile(r"#ifdef ROGUELIKE\n(\s*/\*.*?\*/\n)?\s*func_84374E08\(\);\n#else\n(\s*func_84376768\(\);\n)#endif\n", re.S)
assert len(GATED.findall(damage)) == 2, "both effect-0x1D blocks must carry the direct handler"
vanilla_call = GATED.sub(lambda m: m.group(2), damage)
assert "func_84374E08" not in vanilla_call and vanilla_call.count("func_84376768();") == 2

PREFIX = r"""
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define ROGUELIKE 1
#define SC_CAMPAIGN_BATTLE_WIRED 1
#define ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))
typedef unsigned short u16;
typedef int s32;
typedef struct { int unk_01, unk_03; } Effect;
typedef struct { Effect unk_44; int unk_0C, unk_4C, unk_4D, unk_59, unk_54, unk_4F; } unk_D_800FCB18;
typedef struct { struct { unk_D_800FCB18 unk_38; } unk_654; } Actor;
typedef struct { int unk_2C, unk_48; } Scene;
static Actor actors[2], *D_84390010[2] = { &actors[0], &actors[1] };
static unk_D_800FCB18 *D_843C5238, *D_843C523C;
static Scene scene;
static struct { Scene* unk_00; } D_84390240 = { &scene };
static struct { int unk_208; } message, *D_843901A0 = &message;
static int D_84390288, D_843C4DC4, D_843C4DA0, D_8439022C;
static int D_8438AC60[1] = { 1 }, faults, wrong_calls, notified, handler_calls;
#define rl_native_relics_damage(a, b, c, d, e) (c)
#define rl_native_relics_damage_trace(...) ((void)0)
#define func_8002D5D4(...) ((void)0)
#define func_843179F4(...) ((void)0)
static void func_843061EC(Actor* actor, int flag) {
    assert(flag == 0x1000);
    notified = (int)(actor - actors);
}
static int func_8436F6D8(void) { return 1; }
static void sc_campaign_battle_fault(void) { ++faults; }
static void wrong(void) { ++wrong_calls; }
void func_84374E08(void);
void func_84376768(void);
static void counted(void) { ++handler_calls; func_84374E08(); }
static void (*D_8438AE54[87])(void) = { [0] = wrong, [28] = counted };
"""

SUFFIX = r"""
static int run(int direction, int phase, int swapped, int current_effect, int mode, int tested_effect,
               int sub_hp, int damage) {
    unk_D_800FCB18 *current, *other;
    memset(actors, 0, sizeof(actors));
    current = &actors[direction].unk_654.unk_38;
    other = &actors[!direction].unk_654.unk_38;
    scene.unk_2C = direction;
    scene.unk_48 = phase;
    D_8438AC60[0] = mode;
    D_843C5238 = swapped ? other : current;
    D_843C523C = swapped ? current : other;
    current->unk_44.unk_01 = current_effect;
    D_843C5238->unk_44.unk_01 = tested_effect;
    D_843C523C->unk_4D = 0x10;
    D_843C523C->unk_59 = sub_hp;
    D_843C4DC4 = damage;
    faults = wrong_calls = handler_calls = 0;
    notified = -1;
    func_843703BC();
    assert(scene.unk_2C == direction);
    assert(D_843C5238 == (swapped ? other : current));
    return sub_hp < damage;
}

int main(void) {
    int direction, current_effect, phase, n = 0;
    /* 1/2: Substitute breaks; ordinary (0x10) and confusion self-hit (0x14) in both directions */
    for (direction = 0; direction < 2; direction++)
        for (current_effect = 0; current_effect <= 1; current_effect++)
            for (phase = 0; phase < 2; phase++, n++) {
                int swapped = phase;
                run(direction, swapped ? 0x14 : 0x10, swapped, current_effect, 1, 0x1D, 1, 2);
                assert(!(D_843C523C->unk_4D & 0x10));
                assert(notified == (swapped ? direction : !direction));
#ifdef VANILLA_CALL
                if (!swapped) assert(!faults && !wrong_calls && handler_calls == 1 && (D_843C5238->unk_4C & 4));
                else if (!current_effect) assert(faults == 1 && !wrong_calls && !D_843C5238->unk_4C);
                else assert(!faults && wrong_calls == 1 && !D_843C5238->unk_4C);
#else
                assert(!faults && !wrong_calls && handler_calls == 0);
                assert((D_843C5238->unk_4C & 4) && D_843C5238->unk_54 == 3 && D_843C5238->unk_4F == 1);
                assert(D_843C5238->unk_44.unk_01 == 0);
#endif
            }
#ifndef VANILLA_CALL
    /* recoil phase (0x0A context): swapped AND current flipped -> D_843C5238 is the current actor */
    for (direction = 0; direction < 2; direction++, n++) {
        run(!direction, 0x0A, 0, 0x1D, 1, 0x1D, 1, 2);
        assert(!faults && !wrong_calls && (D_843C5238->unk_4C & 4) && D_843C5238->unk_4F == 1);
        assert(notified == direction);
    }
    /* 3: no new callback when the Substitute holds, the mode is not 1 or the effect is not 0x1D */
    for (phase = 0; phase < 2; phase++) {
        int p = phase ? 0x14 : 0x10;
        assert(!run(0, p, phase, 1, 1, 0x1D, 2, 2)); /* damage == sub HP: native strict '<' */
        assert(!faults && !wrong_calls && !D_843C5238->unk_4C && D_843C523C->unk_59 == 0 && notified == -1);
        assert(!run(1, p, phase, 1, 1, 0x1D, 5, 2));
        assert(!faults && !wrong_calls && !D_843C5238->unk_4C && D_843C523C->unk_59 == 3);
        run(0, p, phase, 1, 0, 0x1D, 1, 2);
        assert(!faults && !wrong_calls && !D_843C5238->unk_4C && D_843C5238->unk_4F == 0);
        assert(D_843C5238->unk_44.unk_01 == 0);
        run(1, p, phase, 1, 1, 7, 1, 2);
        assert(!faults && !wrong_calls && !D_843C5238->unk_4C && D_843C5238->unk_44.unk_01 == 7);
        n += 4;
    }
    /* 4: the 8437670C range refusal still holds for its other callers */
    memset(actors, 0, sizeof(actors));
    scene.unk_2C = 0;
    faults = wrong_calls = 0;
    actors[0].unk_654.unk_38.unk_44.unk_01 = 0;
    func_84376768();
    actors[0].unk_654.unk_38.unk_44.unk_01 = 88;
    func_84376768();
    assert(faults == 2 && !wrong_calls);
    actors[0].unk_654.unk_38.unk_44.unk_01 = 1;
    func_84376768();
    assert(faults == 2 && wrong_calls == 1);
    n += 3;
#endif
    printf("%d fixtures passed\n", n);
    return 0;
}
"""


def build_and_run(directory, label, body, extra):
    unit = directory / (label + ".c")
    unit.write_text(PREFIX + handler + dispatch + wrapper + body + SUFFIX)
    binary = directory / label
    subprocess.run(["gcc", "-std=gnu99", "-O1", "-g", "-fsanitize=address,undefined",
                    "-fno-sanitize-recover=all", "-Wall", "-Wno-unused-variable"] + extra +
                   [str(unit), "-o", str(binary)], check=True)
    result = subprocess.run([str(binary)], check=True, capture_output=True, text=True)
    print(label + ": " + result.stdout.strip())


OUT.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix="confusion-effect29-", dir=str(OUT)) as tmp:
    build_and_run(pathlib.Path(tmp), "roguelike", damage, [])
    build_and_run(pathlib.Path(tmp), "vanilla-call-mutant", vanilla_call, ["-DVANILLA_CALL"])
print("confusion effect29: OK")
sys.exit(0)
