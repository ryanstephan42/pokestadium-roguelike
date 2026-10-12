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
static unsigned lower_calls;
static u32 lower_sizes[8];
void *main_pool_alloc(u32 size, s32 side) {
    assert(side == 1 && lower_calls < 8);
    lower_sizes[lower_calls++] = size;
    return calloc(1, size);
}
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
static unsigned gfx_polls, gfx_busy;
s32 func_80001C90(void) { ++gfx_polls; if (gfx_busy) { --gfx_busy; return 0; } return 1; }
void osStopThread(OSThread *thread) {
    (void)thread;
    if (expected_stop) longjmp(stop_return, 1);
    assert(0);
}

static void loader_test(uintptr_t base) {
    u8 *upper = aligned_alloc(64, SC_BATL_LOADER_BYTES);
    void *m0, *m1;
    unsigned calls;
    assert(upper && SC_BATL_LOADER_BYTES == 0x58000U);
    /* Unarmed: exact vanilla lower request. */
    calls = lower_calls;
    m0 = sc_batl_loader_alloc(SC_BATL_LOADER_MODEL);
    assert(m0 && lower_calls == calls + 1 && lower_sizes[calls] == 0x3C000U);
    free(m0);
    assert(!sc_batl_loader_arm());
    assert(!sc_batl_loader_bind(NULL, SC_BATL_LOADER_BYTES));
    assert(!sc_batl_loader_bind(upper + 16, SC_BATL_LOADER_BYTES));
    assert(!sc_batl_loader_bind(upper, SC_BATL_LOADER_BYTES + 0x50000U));
    assert(sc_batl_loader_bind(upper, SC_BATL_LOADER_BYTES));
    assert(!sc_batl_loader_bind(upper, SC_BATL_LOADER_BYTES));
    assert(sc_batl_loader_live() && sc_batl_loader_unbind());
    assert(!sc_batl_loader_live() && !sc_batl_loader_unbind());
    assert(sc_batl_loader_bind(upper, SC_BATL_LOADER_BYTES));
    assert(sc_batl_loader_arm() && !sc_batl_loader_arm());
    assert(!sc_batl_loader_unbind());
    calls = lower_calls;
    m0 = sc_batl_loader_alloc(0x3C000U);
    assert(lower_calls == calls + 1 && m0);
    assert(sc_batl_loader_alloc(0x18000U) == upper);
    assert(!sc_batl_loader_disarm(&workers[0], &workers[1]));
    m1 = sc_batl_loader_alloc(0x3C000U);
    assert(lower_calls == calls + 2 && m1 && m1 != m0);
    assert(sc_batl_loader_alloc(0x18000U) == upper + 0x18000U);
    assert(sc_batl_loader_alloc(0x28000U) == upper + 0x30000U);
    assert(lower_calls == calls + 2);
    assert(!sc_batl_loader_disarm(&workers[0], &workers[0]));
    assert(!sc_batl_loader_disarm(&workers[0], NULL));
    assert(sc_batl_loader_disarm(&workers[0], &workers[1]));
    assert(sc_batl_loader_live() && !sc_batl_loader_arm());
    /* Outside the window allocation is vanilla again. */
    free(sc_batl_loader_alloc(0x18000U));
    assert(lower_calls == calls + 3);
    /* Spans stay bound until a scope capturing the same workers drains. */
    assert(!sc_batl_loader_unbind());
    Memmap_SetFragmentMap(0x33, base, sizeof(original_clock));
    D_84390010[1]->unk_720->unk_10 = &alternate_worker;
    assert(!sc_batl_scope_open());
    D_84390010[1]->unk_720->unk_10 = &workers[1];
    assert(sc_batl_scope_open());
    assert(!sc_batl_loader_unbind());
    workers[0].unk_01 = 1;
    assert(!sc_batl_scope_close() && !sc_batl_loader_unbind());
    workers[0].unk_01 = 0;
    assert(sc_batl_scope_close());
    gfx_polls = 0; gfx_busy = 3;
    assert(sc_batl_loader_unbind() && !sc_batl_loader_live());
    assert(gfx_polls == 4 && !gfx_busy);
    /* A second install must not reuse the first scope's drain. */
    assert(sc_batl_loader_bind(upper, SC_BATL_LOADER_BYTES) && sc_batl_loader_arm());
    free(sc_batl_loader_alloc(0x3C000U));
    /* Out-of-order request: fail-stop, spans retained forever. */
    expected_stop = 1;
    if (!setjmp(stop_return)) {
        sc_batl_loader_alloc(0x28000U);
        assert(0);
    }
    expected_stop = 0;
    assert(sc_batl_loader_live() && !sc_batl_loader_unbind() && !sc_batl_loader_arm());
    assert(!sc_batl_scope_open());
    /* The retained failure also pins the BATL map. */
    expected_stop = 1;
    if (!setjmp(stop_return)) {
        Memmap_ClearFragmentMemmap(0x33);
        assert(0);
    }
    expected_stop = 0;
    free(m0);
    free(m1);
    free(upper);
    puts("BATL LOADER SOURCE PASSED: A,A,S upper DATA order/drain/retention; M stays lower");
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
    assert(sc_batl_model_admit(imports.workers[0], 1) && sc_batl_model_admit(imports.workers[1], 151));
    assert(sc_batl_model_admit(imports.workers[0], 0x98) && sc_batl_model_admit(imports.workers[1], 0x99));
    assert(!sc_batl_model_admit(imports.workers[0], 0) && !sc_batl_model_admit(imports.workers[1], 0x9A));
    assert(!sc_batl_model_admit(imports.workers[0], 184) && !sc_batl_model_admit(imports.workers[0], 0xFFFF));
    assert(imports.workers[0] && sc_batl_model_admit((void *)&imports, 184));
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
    if (!failed) loader_test(base);
    puts("BATL SCOPE SOURCE PASSED: real map generations; HOST roots/clock, not native reachability");
    return 0;
}
