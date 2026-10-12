/* Persistent story mapping: actual sc_persist.c + sc_module_arena.c with an
   injected native backend (DMA/relocation/pool/quiescence are native-only). */
#include "src/story/sc_persist.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define BACKING 0x2000U
static union { unsigned char bytes[BACKING]; double align; } backing_store;
static unsigned char *const backing = backing_store.bytes;
static ScMaArena arena;

static ScPersistLayout layout = {0x1000, 0x13a0, 0x100, 0x200, 0x300, 0x3c0};
static int unmapped = 1, load_ok = 1, quiescent = 1, unmap_ok = 1;
static unsigned loads, unmaps, enters, mapped;
static unsigned long pool_func, pool_arg;
static const unsigned char *mapped_image;

int sc_persist_native_layout(ScPersistLayout *l) { *l = layout; return 1; }
int sc_persist_native_unmapped(void) { return unmapped && !mapped; }
int sc_persist_native_load(unsigned char *image, const ScPersistLayout *l, ScMaU32 bytes) {
    ++loads;
    assert(l->ram == 0x3c0 && bytes == 0x3c0);
    if (!load_ok) return 0;
    memset(image, 0xa5, bytes);
    mapped = 1;
    mapped_image = image;
    return 1;
}
int sc_persist_native_quiescent(void) { return quiescent; }
static int inside(unsigned long v, const unsigned char *image, ScMaU32 bytes) {
    unsigned long lo = (unsigned long)image;
    return (v >= lo && v < lo + bytes) || (v >= 0x8f900000UL && v < 0x90000000UL);
}
int sc_persist_native_pool_clear(const unsigned char *image, ScMaU32 bytes) {
    assert(image == mapped_image && mapped);
    return !inside(pool_func, image, bytes) && !inside(pool_arg, image, bytes);
}
int sc_persist_native_unmap(unsigned char *image, ScMaU32 bytes) {
    (void)bytes;
    ++unmaps;
    if (!unmap_ok || image != mapped_image) return 0;
    mapped = 0;
    return 1;
}
static int reentry;
int sc_persist_native_enter(unsigned char *image, int arg0, unsigned arg1) {
    const ScPersist *p = sc_persist_state();
    ++enters;
    assert(image == p->image && p->depth == 1 && mapped);
    if (reentry) {
        /* a callee unpublishing mid-call still cannot release under it */
        assert(sc_persist_unpublish() == 1 && sc_persist_release() == 0 && mapped);
    }
    return arg0 + (int)arg1;
}

static ScPersistFn at(ScMaU32 offset) {
    return (ScPersistFn)((unsigned long)sc_persist_state()->image + offset);
}

static void init_arena(void) {
    memset(&arena, 0, sizeof(arena));
    assert(sc_ma_init(&arena, 0x800000U, backing, BACKING));
}

static const ScMaBlock *block(void) {
    return &arena.blocks[sc_persist_state()->handle.slot];
}

static void acquire_published(unsigned generation) {
    const ScPersist *p = sc_persist_state();
    const ScMaBlock *b;
    assert(sc_persist_acquire(&arena) == 1);
    b = block();
    assert(p->phase == SC_PERSIST_PUBLISHED && p->generation == generation && mapped);
    assert(p->bytes == 0x3c0 && b->live && !b->failed && b->lease_kind == 2);
    assert(b->span_count == 4);
    assert(b->spans[0].offset == 0 && b->spans[0].size == 0x100 &&
           b->spans[0].rights == (SC_MA_READ | SC_MA_EXEC));
    assert(b->spans[1].rights == (SC_MA_READ | SC_MA_WRITE) && b->spans[1].size == 0x100);
    assert(b->spans[2].rights == SC_MA_READ && b->spans[2].size == 0x100);
    assert(b->spans[3].rights == (SC_MA_READ | SC_MA_WRITE) && b->spans[3].offset == 0x300 &&
           b->spans[3].size == 0xc0);
    assert(sc_persist_holds_fragment());
}

static void refusals(void) {
    ScPersistLayout good = layout;
    assert(!sc_persist_acquire(NULL) && !sc_persist_holds_fragment());
    /* 0xe9 already mapped (cold call or other owner) */
    unmapped = 0;
    assert(!sc_persist_acquire(&arena) && !sc_persist_holds_fragment() && !loads);
    unmapped = 1;
    /* unaligned text, reloc table outside ROM, oversized window */
    layout.text = 0xf0;
    assert(!sc_persist_acquire(&arena));
    layout = good; layout.rom_end = layout.rom_start + 0x300;
    assert(!sc_persist_acquire(&arena));
    layout = good; layout.ram = 0x100000;
    assert(!sc_persist_acquire(&arena));
    layout = good;
    assert(!loads && !arena.blocks[0].live && !sc_persist_holds_fragment());
    /* nothing callable before publication */
    assert(!sc_persist_pin(SC_PERSIST_PIN_HOOK) && !sc_persist_unpublish() && !sc_persist_fail());
    assert(sc_persist_release() == 0 && !sc_persist_owns(backing, 1));
}

static void lifecycle(void) {
    const ScPersist *p = sc_persist_state();
    unsigned char *image;
    int result = 0;
    ScMaU32 slot;
    acquire_published(1);
    image = p->image;
    slot = p->handle.slot;
    /* second owner refused while held */
    assert(!sc_persist_acquire(&arena) && loads == 1);
    /* code: only aligned addresses inside the sealed text span */
    assert(sc_persist_code(at(0x40)) && sc_persist_code(at(0)));
    assert(!sc_persist_code(at(0x42)) && !sc_persist_code(at(0x100)) && !sc_persist_code(NULL));
    assert(sc_persist_owns(image + 0x200, 0x10) && sc_persist_owns(image, 0x3c0));
    assert(!sc_persist_owns(image + 0x3b0, 0x20) && !sc_persist_owns(image - 1, 1));
    assert(!sc_persist_owns(image, 0));
    assert(sc_persist_call(5, 7, &result) == 1 && result == 12 && !p->depth && enters == 1);
    /* hook + retirement owners pin the mapping */
    assert(sc_persist_pin(SC_PERSIST_PIN_HOOK) && sc_persist_pin(SC_PERSIST_PIN_RETIREMENT));
    assert(!sc_persist_pin(SC_PERSIST_PIN_KINDS));
    /* release requires unpublication first */
    assert(sc_persist_release() == 0 && mapped && !unmaps);
    assert(sc_persist_unpublish() == 1 && !sc_persist_unpublish());
    /* unpublished: no new entries, pins or callback admission */
    assert(!sc_persist_code(at(0x40)) && !sc_persist_owns(image, 1));
    assert(!sc_persist_pin(SC_PERSIST_PIN_SERVICE) && !sc_persist_call(1, 1, &result));
    assert(enters == 1);
    /* pinned retirement entry: only an existing pin of that kind may enter */
    assert(!sc_persist_call_pinned(SC_PERSIST_PIN_SERVICE, 1, 1, &result) && enters == 1);
    assert(!sc_persist_call_pinned(SC_PERSIST_PIN_KINDS, 1, 1, &result));
    assert(!sc_persist_call_pinned(SC_PERSIST_PIN_HOOK, 1, 1, NULL) && enters == 1);
    assert(sc_persist_call_pinned(SC_PERSIST_PIN_HOOK, 2, 3, &result) == 1 &&
           result == 5 && enters == 2 && !p->depth);
    assert(sc_persist_release() == 0 && mapped && !unmaps);
    /* hook uninstall; the DP retirement token is still outstanding */
    assert(sc_persist_unpin(SC_PERSIST_PIN_HOOK) && !sc_persist_unpin(SC_PERSIST_PIN_HOOK));
    quiescent = 1; pool_func = pool_arg = 0;
    assert(sc_persist_release() == 0 && mapped && !unmaps);
    /* token retries run while still mapped; the token's final release unpins */
    assert(sc_persist_unpin(SC_PERSIST_PIN_RETIREMENT));
    /* battle/service/hook/resource scope still live */
    quiescent = 0;
    assert(sc_persist_release() == 0 && mapped && !unmaps);
    quiescent = 1;
    /* a pool scope still holds a callback or argument into the image */
    pool_func = (unsigned long)(image + 0x80);
    assert(sc_persist_release() == 0 && mapped && !unmaps);
    pool_func = 0; pool_arg = (unsigned long)(image + 0x310);
    assert(sc_persist_release() == 0 && mapped && !unmaps);
    pool_arg = 0x8f900400UL;
    assert(sc_persist_release() == 0 && mapped && !unmaps);
    pool_arg = 0;
    /* pending retries never changed state */
    assert(p->phase == SC_PERSIST_DRAINING && p->image == image && arena.blocks[slot].live);
    assert(sc_persist_release() == 1 && !mapped && unmaps == 1);
    assert(p->phase == SC_PERSIST_IDLE && !p->image && !arena.blocks[slot].live);
    assert(!sc_persist_holds_fragment());
    /* post-release: nothing resolves into the freed image */
    assert(!sc_persist_code((ScPersistFn)((unsigned long)image + 0x40)));
    assert(!sc_persist_owns(image, 1) && !sc_persist_pin(SC_PERSIST_PIN_RETIREMENT));
    assert(!sc_persist_unpin(SC_PERSIST_PIN_RETIREMENT) && !sc_persist_call(1, 1, &result));
    assert(!sc_persist_call_pinned(SC_PERSIST_PIN_RETIREMENT, 1, 1, &result));
    assert(sc_persist_release() == 0 && enters == 2 && unmaps == 1);
    /* reacquire: new generation, fresh block lease */
    acquire_published(2);
    assert(loads == 2);
    /* a callee cannot release the image beneath its own frame */
    reentry = 1;
    assert(sc_persist_call(1, 2, &result) == 1 && result == 3 && enters == 3);
    reentry = 0;
    assert(p->phase == SC_PERSIST_DRAINING && mapped && !p->depth);
    assert(sc_persist_release() == 1 && !mapped && unmaps == 2);
}

typedef void (*Scenario)(void);

static void load_failure(void) {
    const ScPersist *p;
    load_ok = 0;
    assert(sc_persist_acquire(&arena) == 0);
    p = sc_persist_state();
    /* load may have mapped 0xe9: retain the block and refuse everything */
    assert(p->phase == SC_PERSIST_FAILED && sc_persist_holds_fragment());
    assert(block()->live && block()->failed && sc_persist_release() == -1);
    assert(!sc_persist_pin(SC_PERSIST_PIN_HOOK) && !sc_persist_acquire(&arena) && !unmaps);
    load_ok = 1;
    assert(!sc_persist_acquire(&arena) && loads == 1);
}

static void crash_latch(void) {
    const ScPersist *p = sc_persist_state();
    acquire_published(1);
    assert(sc_persist_pin(SC_PERSIST_PIN_SERVICE) && sc_persist_unpublish());
    assert(sc_persist_unpin(SC_PERSIST_PIN_SERVICE));
    quiescent = -1;
    assert(sc_persist_release() == -1 && p->phase == SC_PERSIST_FAILED);
    assert(mapped && !unmaps && block()->live && block()->failed);
    quiescent = 1;
    assert(sc_persist_release() == -1 && mapped && !unmaps && sc_persist_holds_fragment());
}

static void unmap_failure(void) {
    const ScPersist *p = sc_persist_state();
    acquire_published(1);
    assert(sc_persist_unpublish());
    unmap_ok = 0;
    assert(sc_persist_release() == -1 && p->phase == SC_PERSIST_FAILED);
    assert(block()->live && block()->failed && sc_persist_holds_fragment());
    unmap_ok = 1;
    assert(sc_persist_release() == -1 && unmaps == 1);
}

static void explicit_failure(void) {
    const ScPersist *p = sc_persist_state();
    acquire_published(1);
    assert(sc_persist_pin(SC_PERSIST_PIN_LIFETIME));
    assert(sc_persist_fail() == 1 && p->phase == SC_PERSIST_FAILED);
    assert(!sc_persist_code(at(0x40)) && !sc_persist_unpin(SC_PERSIST_PIN_LIFETIME));
    assert(sc_persist_release() == -1 && mapped && !unmaps && block()->failed);
}

static void exhausted_arena(void) {
    ScMaHandle h;
    /* the arena cannot hold the image: nothing is mapped or held */
    while (sc_ma_alloc(&arena, 0x400, &h)) {}
    assert(!sc_persist_acquire(&arena) && !loads && !sc_persist_holds_fragment());
}

static void isolated(const char *name, Scenario s) {
    int status;
    pid_t pid;
    fflush(stdout);
    pid = fork();
    assert(pid >= 0);
    if (!pid) {
        init_arena();
        s();
        _exit(0);
    }
    assert(waitpid(pid, &status, 0) == pid && WIFEXITED(status) && !WEXITSTATUS(status));
    printf("  %s: retained/refused\n", name);
}

int main(void) {
    /* each permanent-retention case forks from the pristine singleton */
    isolated("load failure", load_failure);
    isolated("crash latch", crash_latch);
    isolated("unmap failure", unmap_failure);
    isolated("explicit failure", explicit_failure);
    isolated("exhausted arena", exhausted_arena);
    init_arena();
    refusals();
    lifecycle();
    puts("STORY PERSIST HOST PASSED: sealed R|X/RW/R/RW spans, pins/depth/quiescence/pool-scope "
         "release gating, unpublish-before-release, post-release refusal, reacquire, "
         "fail/crash retention; native backend injected");
    return 0;
}
