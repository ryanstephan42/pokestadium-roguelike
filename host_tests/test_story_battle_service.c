const unsigned char *sc_story_battle_mapping(void);
#define main selector_fixture_main
#include "test_story_ai_selector.c"
#undef main
#include "src/story/sc_battle_service.c"
#include "src/story/sc_service_native_calls.h"
#include "src/memmap.h"
#include "src/19840.h"
#include <stdlib.h>
#include <setjmp.h>

OSPiHandle __CartRomHandle;
u32 osMemSize = 0x800000;
int gScProbeActive = 1;
RlU8 gScProbeSixMapping[6] = {0,1,2,3,4,5};
const RlU8 *sc_story_battle_mapping(void) { return gScProbeSixMapping; }
static f32 *fixture_clock;
static jmp_buf stopped_service;
#define D_84390120 (*fixture_clock)
static s32 D_84390138, D_8439013C;
static unsigned callback_count, move_callbacks, freed_jobs, fault, cache_count;
static unk_D_86002F58_004_000_010 workers[2];
static sp18_func_800198E4 jobs[2][2];
static unsigned ready[2][2];
static OSIoMesg *dma_job;
static unsigned char *rom;
static OSMesgQueue *dma_queue;
int sc_host_read_image(const char *, unsigned char *, unsigned);
void *sc_host_fixed_backing(unsigned, unsigned);
int sc_host_unmap_backing(void *, unsigned);

const f32 *sc_batl_clock_source(void) { return fixture_clock; }
void osSyncPrintf(const char *format, ...) { (void)format; }
void osStopThread(OSThread *thread) {
    (void)thread;
    assert(fault == 8 || fault == 9);
    longjmp(stopped_service, 1);
}
void Util_Free(void *p) { assert(p); ++freed_jobs; }
#include "worker-consumer.inc"

void func_8432B4B0(void) { ++callback_count; }
s32 func_8430D334(unk_D_84390010 *side) { (void)side; return 0; }
void func_843193F4(unk_D_84390010 *side) { (void)side; ++move_callbacks; }
#include "qualified-callers.inc"

void osCreateMesgQueue(OSMesgQueue *q, OSMesg *cell, s32 count) {
    assert(count == 1 && cell);
    dma_queue = q;
}
s32 osEPiStartDma(OSPiHandle *cart, OSIoMesg *job, s32 direction) {
    assert(cart == &__CartRomHandle && direction == OS_READ);
    assert(job->dramAddr == (void *)0x80400000U && job->size == AI_ROM_BYTES &&
           job->devAddr == AI_ROM_START && job->hdr.retQueue == dma_queue);
    dma_job = job;
    memcpy(job->dramAddr, rom, job->size);
    return fault == 1 ? 2 : 0;
}
void osWritebackDCacheAll(void) { ++cache_count; }
void osInvalDCache(void *p, s32 bytes) {
    assert(p == (void *)0x80400000U && bytes == AI_ROM_BYTES);
    ++cache_count;
}
void osWritebackDCache(void *p, s32 bytes) {
    assert((uintptr_t)p >= 0x80400000U &&
           (uintptr_t)p + bytes <= 0x80400000U + AI_RAM);
    ++cache_count;
}
void osInvalICache(void *p, s32 bytes) {
    assert(p == (void *)0x80400000U && bytes == sc_image_spans[0].size);
    ++cache_count;
}
s32 osRecvMesg(OSMesgQueue *q, OSMesg *message, s32 flags) {
    unsigned i, j;
    assert(flags == OS_MESG_NOBLOCK);
    if (q == dma_queue) {
        if (fault == 3) return -1;
        assert(dma_job);
        *message = fault == 2 ? (void *)&fault : (void *)dma_job;
        return 0;
    }
    for (i = 0; i < 2; ++i) for (j = 0; j < 2; ++j)
        if (q == (j ? &workers[i].unk_50 : &workers[i].unk_30)) {
            if (!ready[i][j]) return -1;
            ready[i][j] = 0;
            *message = &jobs[i][j];
            return 0;
        }
    assert(0);
    return -1;
}

static void reset_fixture(void) {
    unsigned i;
    fixture();
    for (i = 0; i < 2; ++i) registrations[i].unk_10 = &workers[i];
    D_8439013C = 0;
}

int main(int argc, char **argv) {
    ScMaArena *arena = &native_arena;
    ScBatPermit nested = {0,0};
    ScBatImports captured;
    ScAiSelection unused;
    ScBatPermit previous_permit;
    AiPublished previous_publication;
    AiOwner previous_owner;
    SprRequest previous_request;
    ScAiRequest previous_decision;
    ScMaLease previous_lease;
    unsigned owner, path, i, j, rng_before;
    void *native, *upper;
    assert(argc == 3);
    fault = atoi(argv[2]);
    native = sc_host_fixed_backing(0x80200000U, AI_NATIVE_END - 0x84300000U);
    upper = sc_host_fixed_backing(0x80400000U, 0x400000);
    assert(native && upper);
    memset((unsigned char *)upper + SC_SERVICE_BACKING_BYTES, 0xa5,
           0x400000 - SC_SERVICE_BACKING_BYTES);
    fixture_clock = native;
    rom = malloc(AI_ROM_BYTES);
    assert(rom && sc_host_read_image(argv[1], rom, AI_ROM_BYTES));
    __CartRomHandle.type = DEVICE_TYPE_CART;
    __CartRomHandle.domain = PI_DOMAIN1;
    __CartRomHandle.baseAddress = 0xb0000000;
    reset_fixture();
    Memmap_SetFragmentMap(0x33, (uintptr_t)native, AI_NATIVE_END - 0x84300000U);
    assert(sc_batl_scope_open());
    osMemSize = 0x400000;
    assert(!sc_service_native_arena());
    assert(!sc_service_bootstrap() && !arena->active && !service.active);
    osMemSize = 0x800000;
    assert(sc_service_native_arena() == arena);
    assert(sc_service_native_arena() == arena && !service.active);
    assert(arena->end == 0x7da7e0U && arena->backing == upper &&
           arena->backing_bytes == SC_SERVICE_BACKING_BYTES);
    if (fault == 1) assert(!sc_service_bootstrap());
    else {
        assert(sc_service_bootstrap());
        assert(arena->end == 0x7da7e0U && arena->backing == upper &&
               arena->backing_bytes == SC_SERVICE_BACKING_BYTES);
        assert(!sc_service_bootstrap());
        assert(!sc_service_start(arena, &__CartRomHandle));
        assert(!sc_batl_scope_borrow(&nested, &captured));
        if (fault == 3) {
            assert(sc_service_poll(0) == 0);
            assert(sc_service_poll(1) == -1);
        } else assert(sc_service_poll(0) == (fault == 2 ? -1 : 1));
    }
    if (fault && fault <= 3) goto retained;
    assert(cache_count == 7 && service.initialized && service.callable);
    previous_permit = service.permit;
    previous_publication = service.published;
    previous_owner = service.owner;
    previous_request = service.loader.request;
    previous_lease = service.lease;
    for (owner = 0; owner < 2; ++owner) for (path = 0; path < 7; ++path) {
        reset_fixture();
        for (i = 1; i < 6; ++i) groups[owner].unk_01C[i].unk_02 = 0;
        for (i = 0; i < 2; ++i)
            sides[i].unk_654.unk_38.unk_5A = sides[i].unk_654.unk_38.unk_1F[0];
        if (path == 0) sides[owner].unk_654.unk_38.unk_4C = 1;
        else if (path == 1) sides[owner].unk_654.unk_38.unk_4C = 0x20;
        else if (path == 2) sides[1 - owner].unk_654.unk_38.unk_4C = 0x20;
        else sides[owner].unk_654.unk_38.unk_15 = path == 6 ? 0x20 : 1 << (path - 3);
        *fixture_clock = 1.0f;
        if (fault != 9)
            func_8430DF60(0, 0, 0, 0, &sides[1 - owner]);
        if (fault == 8) D_843C60BC++;
        *fixture_clock = 30.0f;
        if (fault == 8 || fault == 9) {
            if (!setjmp(stopped_service)) {
                func_8430DBA0(&sides[owner], &sides[1 - owner]);
                assert(0);
            }
            assert(service.failed && gScServiceLifecycle.failed);
            goto retained;
        }
        assert(service.pending == 1);
        func_8430DBA0(&sides[owner], &sides[1 - owner]);
        assert(!service.pending && D_8439013C == 2 &&
               sides[owner].unk_654.unk_2A == 1 &&
               sides[owner].unk_654.unk_10 ==
                   (sides[1 - owner].unk_654.unk_10 == 3 ? 3 : 1));
        *fixture_clock = 90.0f;
        func_8430DBA0(&sides[owner], &sides[1 - owner]);
        assert(!service.pending && D_8439013C == 2 &&
               sides[owner].unk_654.unk_10 == 3 &&
               sides[owner].unk_654.unk_0C == sides[owner].unk_654.unk_14);
    }
    if (fault == 10) {
        assert(sc_service_stop_poll() == 1 && sc_batl_scope_close());
        assert(!service.failed && !service.pending && !service.permit.ticket);
        assert(sc_ma_shutdown(arena));
        Memmap_ClearFragmentMemmap(0x33);
        goto finished;
    }
    for (owner = 0; owner < 2; ++owner) {
        reset_fixture();
        *fixture_clock = 1.0f;
        func_8430DF60(0, 0, 0, 0, &sides[1 - owner]);
        previous_decision = service.request;
        assert(service.pending == 1 && service.request.owner == owner);
        assert(!sc_service_prepare(&sides[owner], SC_AI_DF60,
                                   gScProbeSixMapping, 6, &unused));
        assert(sc_service_stop_poll() == -1 && service.permit.ticket);
        if (fault == 4) {
            D_843C60BC++;
            assert(!sc_service_accept(&sides[owner], D_84390138 >= 4 ? SC_AI_DE20 : SC_AI_DCD4));
            assert(!sc_service_reject(&sides[owner], D_84390138 >= 4 ? SC_AI_DE20 : SC_AI_DCD4));
            goto retained;
        }
        *fixture_clock = 90.0f;
        if (D_84390138 >= 4) func_8430DE20(&sides[owner], &sides[1 - owner]);
        else func_8430DCD4(&sides[owner], &sides[1 - owner]);
        assert(!service.pending && sides[owner].unk_654.unk_10 == 3);
        assert(!sc_service_accept(&sides[owner], SC_AI_DCD4));
        for (path = 0; path < 2; ++path) {
            reset_fixture();
            memset(sides[owner].unk_654.unk_38.unk_32, 0, 4);
            *fixture_clock = 1.0f;
            rng_before = rng_calls;
            func_8430DF60(0, 0, 0, 0, &sides[1 - owner]);
            assert(service.request.selection.kind == SC_AI_STRUGGLE &&
                   D_84390138 == -1 && rng_before == rng_calls);
            *fixture_clock = 90.0f;
            func_8430DCD4(&sides[owner], &sides[1 - owner]);
            assert(service.pending == 2 && sides[owner].unk_654.unk_0C == 4);
            assert(sc_service_stop_poll() == -1 && service.permit.ticket);
            if (path) sides[owner].unk_654.unk_38.unk_4C |= 0x20;
            if (fault == 5) {
                D_843C60BC ^= 1234;
                assert(!sc_service_struggle(&sides[owner]));
                goto retained;
            }
            func_8430EB1C(&sides[owner]);
            assert(!service.pending && rng_before == rng_calls &&
                   sides[owner].unk_654.unk_0C == (path ? 4 : 0));
            if (!path) assert(sides[owner].unk_654.unk_38.unk_5A == 0xA5);
            assert(!sc_service_struggle(&sides[owner]));
        }
    }
    reset_fixture();
    {
    static unk_D_86002F58_004_000_010_024 live_header[2];
    static s16 empty_request[2] = {-1, -1};
    static u32 empty_results[2];
    static u8 fx_span[2][16];
    for (i = 0; i < 2; ++i) {
        workers[i].unk_01 = 3;
        live_header[i].unk_00 = 25;
        workers[i].unk_24 = &live_header[i];
        workers[i].unk_18 = (uintptr_t)fx_span[i];
        for (j = 0; j < 2; ++j) {
            jobs[i][j].unk_00 = j + 1;
            jobs[i][j].unk_04 = (uintptr_t)(j ? &workers[i].unk_50 : &workers[i].unk_30);
            jobs[i][j].unk_14 = j ? 0x18000 : 0x3C000;
        }
        jobs[i][0].unk_02 = 24;
        jobs[i][0].unk_0C = (uintptr_t)&live_header[i];
        jobs[i][0].unk_18 = workers[i].unk_2C;
        jobs[i][1].unk_08 = (uintptr_t)empty_results;
        jobs[i][1].unk_0C = (uintptr_t)empty_request;
        jobs[i][1].unk_10 = workers[i].unk_18;
    }
    }
    assert(sc_service_stop_poll() == 0 && service.permit.ticket &&
           service.lease.ticket && !freed_jobs);
    assert(!sc_batl_worker_admit(&workers[0]));
    for (i = 0; i < 2; ++i) for (j = 0; j < 2; ++j) ready[i][j] = 1;
    if (fault == 6) jobs[0][0].unk_04 = (uintptr_t)&workers[1].unk_30;
    if (fault == 7) registrations[0].unk_10 = &workers[1];
    if (fault >= 6) {
        assert(sc_service_stop_poll() == -1);
        goto retained;
    }
    assert(sc_service_stop_poll() == 1 && freed_jobs == 4 && !service.callable);
    assert(!service.permit.ticket && !service.lease.ticket);
    assert(sc_batl_scope_close());
    Memmap_ClearFragmentMemmap(0x33);
    reset_fixture();
    Memmap_SetFragmentMap(0x33, (uintptr_t)native, AI_NATIVE_END - 0x84300000U);
    assert(sc_batl_scope_open() && sc_service_bootstrap() && sc_service_poll(0) == 1);
    assert(service.permit.generation > previous_permit.generation &&
           service.permit.ticket > previous_permit.ticket &&
           service.owner.generation > previous_owner.generation &&
           service.lease.handle.generation > previous_lease.handle.generation);
    assert(!sc_batl_scope_check(&previous_permit));
    assert(!sc_batl_scope_retire(&previous_permit));
    assert(!ai_owner_retire(previous_owner, &previous_publication));
    assert(!ai_owner_admit(previous_owner, &__CartRomHandle, previous_permit.generation,
                           AI_ROM_START, AI_ROM_END));
    assert(!spr_native_retire(service.transport, &previous_request));
    assert(!spr_retire(&service.loader, previous_request.generation, previous_request.operation));
    assert(sc_ai_entry(SC_AI_ACCEPT, &previous_decision) == SC_AI_INVALID);
    assert(!sc_ma_map(arena, &previous_lease, 0, 4, SC_MA_READ));
    for (owner = 0; owner < 2; ++owner) for (path = 0; path < 2; ++path) {
        unsigned sentinel = 5;
        reset_fixture();
        sides[owner].unk_654.unk_38.unk_0C = 0;
        groups[owner].unk_01C[0].unk_02 = 0;
        sides[owner].unk_654.unk_2D = 0x10;
        if (path) sides[1 - owner].unk_654.unk_2D = 0x10;
        sides[owner].unk_654.unk_18 = sentinel;
        *fixture_clock = 19.0f;
        rng_before = rng_calls;
        assert(func_8430D9EC(&sides[owner], &sides[1 - owner]) == 1 &&
               sides[owner].unk_654.unk_18 == sentinel && !service.pending &&
               rng_before == rng_calls);
        *fixture_clock = 20.0f;
        sides[owner].unk_654.unk_10 = 3;
        assert(func_8430D9EC(&sides[owner], &sides[1 - owner]) == 1 &&
               sides[owner].unk_654.unk_18 == sentinel && !service.pending &&
               rng_before == rng_calls);
        sides[owner].unk_654.unk_10 = 0;
        assert(func_8430D9EC(&sides[owner], &sides[1 - owner]) == 1);
        assert(!service.pending && service.request.selection.kind == SC_AI_SWITCH &&
               sides[owner].unk_654.unk_18 == service.request.selection.native_slot &&
               sides[owner].unk_654.unk_18 != 0 && sides[owner].unk_654.unk_10 == 3);
    }
    assert(sc_service_stop_poll() == 1 && sc_batl_scope_close());
    assert(gScServiceLifecycle.starts == 2 && gScServiceLifecycle.ready == 2 &&
           gScServiceLifecycle.drains == 2 && gScServiceLifecycle.revokes == 2 &&
           gScServiceLifecycle.retires == 2 && !gScServiceLifecycle.pending &&
           !gScServiceLifecycle.active && !gScServiceLifecycle.failed &&
           gScServiceLifecycle.scope_generation == service.imports.generation &&
           gScServiceLifecycle.module_generation == service.owner.generation);
    assert(sc_ma_shutdown(arena));
    Memmap_ClearFragmentMemmap(0x33);
    goto finished;
retained:
    assert(service.permit.ticket && service.lease.ticket);
    assert(!sc_batl_scope_close() && !sc_ma_shutdown(arena));
    assert(!sc_ma_release(arena, service.allocation));
finished:
    /* Process teardown of isolated HOST mappings; not service release authority. */
    free(rom);
    for (i = SC_SERVICE_BACKING_BYTES; i < 0x400000; ++i)
        assert(((unsigned char *)upper)[i] == 0xa5);
    assert(sc_host_unmap_backing(upper, 0x400000));
    assert(sc_host_unmap_backing(native, AI_NATIVE_END - 0x84300000U));
    printf("JOINED SOURCE PASSED mode=%u callbacks=%u consumers=%u jobs=%u; HOST entry, injected OS only; not relocated MIPS\n",
           fault, callback_count, move_callbacks, freed_jobs);
    return 0;
}
