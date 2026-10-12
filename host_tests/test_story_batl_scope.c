#define main selector_fixture_main
#include "test_story_ai_selector.c"
#undef main
#include "src/story/sc_batl_scope.h"
#include "src/memmap.h"
#include "src/19840.h"
#include <stdlib.h>
#include <setjmp.h>

static f32 original_clock;
static f32 alternate_clock;
static int changed_clock, expected_stop;
static jmp_buf stop_return;
static unk_D_86002F58_004_000_010 workers[2], alternate_worker;
static sp18_func_800198E4 jobs[2][2];
static unsigned ready[2][2], freed;
void Util_Free(void *p) { assert(p); ++freed; }
#include "worker-consumer.inc"

s32 osRecvMesg(OSMesgQueue *q, OSMesg *message, s32 flags) {
    unsigned i, j;
    assert(flags == OS_MESG_NOBLOCK);
    for (i = 0; i < 2; ++i) for (j = 0; j < 2; ++j)
        if (q == (j ? &workers[i].unk_50 : &workers[i].unk_30)) {
            if (!ready[i][j]) return -1;
            ready[i][j] = 0;
            *message = &jobs[i][j];
            return 0; /* Injected OS completion; real consumer executes below. */
        }
    assert(0);
    return -1;
}
const f32 *sc_batl_clock_source(void) {
    return changed_clock ? &alternate_clock : &original_clock;
}
void osSyncPrintf(const char *format, ...) { (void)format; }
void osStopThread(OSThread *thread) {
    (void)thread;
    if (expected_stop) longjmp(stop_return, 1);
    assert(0);
}

int main(int argc, char **argv) {
    ScBatPermit first = {0,0}, second = {0,0}, copied, wrong;
    ScBatImports imports;
    uintptr_t base = (uintptr_t)&original_clock;
    unsigned failed = argc > 1 && atoi(argv[1]);
    unsigned i;
    unk_D_800AE540_1194 alternate_registration;
    unk_D_800AE540_0004 alternate_group;
    fixture();
    for (i = 0; i < 2; ++i) D_84390010[i]->unk_720->unk_10 = &workers[i];
    assert(!sc_batl_scope_open());
    Memmap_SetFragmentMap(0x33, base, sizeof(original_clock));
    assert(Memmap_GetStoryBattleMapGeneration() == 1);
    assert(sc_batl_scope_open());
    assert(!sc_batl_scope_open());
    assert(sc_batl_scope_borrow(&first, &imports));
    assert(imports.clock == &original_clock && imports.map_generation == 1);
    assert(!sc_batl_scope_borrow(&second, &imports));
    copied = first; wrong = first; wrong.ticket++;
    assert(!sc_batl_scope_retire(&wrong));
    assert(sc_batl_scope_check(&first));
    assert(!sc_batl_scope_close() && !sc_batl_scope_map_change());
    if (failed >= 10) {
        unsigned j;
        for (i = 0; i < 2; ++i) {
            workers[i].unk_01 = 3;
            for (j = 0; j < 2; ++j) {
                jobs[i][j].unk_00 = j + 1;
                jobs[i][j].unk_04 = (uintptr_t)(j ? &workers[i].unk_50 : &workers[i].unk_30);
            }
        }
        assert(sc_batl_scope_drain(&first) == 0);
        assert(!sc_batl_worker_admit(&workers[0]) && first.ticket && !freed);
        for (i = 0; i < 2; ++i) for (j = 0; j < 2; ++j) ready[i][j] = 1;
        if (failed == 11) jobs[0][0].unk_00 = 2;
        if (failed == 12) jobs[0][0].unk_04 = (uintptr_t)&workers[1].unk_30;
        if (failed == 10) {
            assert(sc_batl_scope_drain(&first) == 1 && freed == 4);
            assert(!workers[0].unk_01 && !workers[1].unk_01);
            assert(sc_batl_scope_retire(&first));
            assert(sc_batl_scope_close());
        } else {
            assert(sc_batl_scope_drain(&first) == -1);
            assert(workers[0].unk_01 == 3 && !freed && first.ticket);
            assert(!sc_batl_scope_close() && !sc_batl_scope_retire(&first));
        }
    } else if (failed) {
        if (failed == 2 || failed == 3) {
            expected_stop = 1;
            if (!setjmp(stop_return)) {
                if (failed == 2)
                    Memmap_SetFragmentMap(0x33, base + 4, sizeof(original_clock));
                else Memmap_ClearFragmentMemmap(0x33);
                assert(0);
            }
            assert(Memmap_GetFragmentBaseVaddr(0x33) == base);
            assert(Memmap_GetStoryBattleMapGeneration() == 1);
        } else if (failed == 4) changed_clock = 1;
        else if (failed == 5) {
            alternate_registration = *D_84390010[0]->unk_720;
            D_84390010[0]->unk_720 = &alternate_registration;
        } else if (failed == 6) {
            alternate_group = *D_84390010[0]->unk_724;
            D_84390010[0]->unk_720->unk_08[0] = &alternate_group;
            D_84390010[0]->unk_724 = &alternate_group;
        } else if (failed == 7)
            D_84390010[0]->unk_724->unk_214 = D_84390010[1]->unk_724->unk_214;
        else if (failed == 8) {
            D_84390010[0]->unk_724->unk_002 = 5;
            D_84390010[0]->unk_724->unk_214->unk_002 = 5;
        } else if (failed == 9)
            D_84390010[0]->unk_720->unk_10 = &alternate_worker;
        else D_84390010[0] = D_84390010[1];
        assert(!sc_batl_scope_check(&first));
        assert(!sc_batl_scope_retire(&first));
        assert(first.ticket && !sc_batl_scope_close());
        assert(!sc_batl_scope_map_change());
    } else {
        assert(sc_batl_scope_retire(&first));
        assert(!sc_batl_scope_retire(&copied));
        workers[0].unk_01 = 1;
        assert(!sc_batl_scope_close());
        workers[0].unk_01 = 0;
        workers[1].unk_01 = 2;
        assert(!sc_batl_scope_close());
        workers[1].unk_01 = 0;
        assert(sc_batl_scope_close());
        Memmap_ClearFragmentMemmap(0x33);
        assert(!sc_batl_scope_open());
        Memmap_SetFragmentMap(0x33, base, sizeof(original_clock));
        assert(Memmap_GetStoryBattleMapGeneration() == 2);
        assert(sc_batl_scope_open());
        assert(sc_batl_scope_borrow(&second, &imports));
        assert(second.generation != copied.generation && second.ticket != copied.ticket);
        assert(!sc_batl_scope_check(&copied));
        assert(sc_batl_scope_retire(&second));
        assert(sc_batl_scope_close());
        Memmap_ClearFragmentMemmap(0x33);
    }
    puts("BATL SCOPE SOURCE PASSED: real map generations; HOST roots/clock, not native reachability");
    return 0;
}
