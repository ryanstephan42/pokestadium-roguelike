#define SC_RETIREMENT_FIXTURE_MAIN retirement_fixture_main
#include "test_story_native_retirement.c"
#define _CRASH_SCREEN_H_
u32 osMemSize = 0x800000U, gExpansionRAMStart;
volatile u32 sc_crash_active;
#include <sys/mman.h>
#include "../src/story/sc_module_arena.c"
static int defer_release;
static unsigned data_frees;
static int release_data(ScMaArena *arena, ScMaHandle handle) {
    int result = sc_ma_release(arena, handle);
    if (result && defer_release) {
        assert(interrupt_depth);
        ++data_frees;
        D_80083CA0.unk_A8A = 0;
        assert(mprotect(arena->backing, 0xc000U, PROT_NONE) == 0);
    }
    return result;
}
#define sc_ma_release release_data
#define error memory_error
#include "../src/story/sc_native_memory.c"
#undef error
#undef sc_ma_release
#include "../src/story/sc_native_crash.c"
#define _UTIL_H_
#define UnkStruct80083CA0_2 __typeof__(D_80083CA0)
#define retire_backing lifetime_retire_backing
#include "../src/story/sc_native_lifetime.c"
#undef retire_backing
#undef UnkStruct80083CA0_2
#include "../src/story/sc_native_loop.c"

int sc_native_ui_release(ScNativeUi *ui, const ScUiDriver *driver, int fenced) {
    (void)ui; (void)driver; (void)fenced;
    assert(!"Resource cleanup must not replay after lower retirement");
    return SC_INVALID;
}
int sc_native_pak_release(ScNativePak *pak) {
    (void)pak;
    assert(!"Pak cleanup must not replay after lower retirement");
    return SC_SAVE_IO;
}

static int no_work(void *context, const ScNativeCampaignBacking *backing,
                    const ScUiBattleRequest *request) {
    (void)context; (void)backing; (void)request; return SC_INVALID;
}
int main(void) {
    static ScNativeLifetime owner;
    ScMaArena arena = {0};
    ScCatalog catalog;
    ScDpToken copied;
    unk_D_80068BB0 fallback;
    RlU8 *pixels;
    unsigned i, before;
    pixels = mmap((void *)0x80400000U, 0x3da7e0U, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    assert(pixels == (void *)0x80400000U);
    memset(&catalog, 0, sizeof(catalog));
    memset(&D_80083CA0, 0, sizeof(D_80083CA0));
    func_800062E4(&fallback, 0, IMAGE_SIZE_BITS_16b, 640, 1, (u32)0x80010000U);
    D_80083CA0.unk_9E0 = &fallback;
    D_80083CA0.unk_A8A = 1;
    assert(sc_ma_init(&arena, osMemSize, pixels, 0x3da7e0U));
    assert(sc_native_lifetime_acquire(&owner, &arena, &catalog, 42,
        &owner.loop.backing) == SC_OK);
    assert(sc_native_lifetime_validate(&owner, &owner.loop.backing) == SC_OK);
    for (i = 0; i < 2; i++)
        func_800062E4(&owner.display.images[i], 0, IMAGE_SIZE_BITS_16b, 640, 480,
            owner.loop.backing.framebuffer[i].pixels);
    owner.loop.acquired = owner.loop.prepared = owner.loop.exited = 1;
    owner.loop.owner.context = &owner;
    owner.loop.owner.validate = sc_native_lifetime_validate;
    owner.loop.owner.release = sc_native_lifetime_release;
    owner.loop.owner.submit = no_work;
    owner.display.acquired = owner.display.closing = 1;
    vi_current = owner.binding.framebuffer[0].pixels;
    vi_next = owner.binding.framebuffer[1].pixels;
    crash_screen_set_draw_info((u16 *)vi_current, 640, 16);
    assert(sc_native_lifetime_retire(&owner, &owner.loop.backing,
        owner.display.images) == SC_PENDING);
    copied = owner.retirement.token;
    assert(sc_dp_quiescent(&copied) == SC_INVALID);
    worker_done(1);
    before = submitted;
    assert(sc_native_lifetime_retire(&owner, &owner.loop.backing,
        owner.display.images) == SC_OK);
    assert(submitted == before && gCrashScreen.frameBuf == (void *)0xa07da800U);
    owner.display.acquired = owner.display.closing = 0;
    sc_crash_active = 1;
    assert(sc_native_loop_close(&owner.loop) == SC_UNAVAILABLE);
    assert(owner.memory.live && owner.memory.lease.ticket && !data_frees);
    /* A fresh process never observed a fault; resetting only the test latch
       lets the independent no-fault cleanup scenario continue. */
    sc_crash_active = 0;
    defer_release = 1;
    assert(sc_native_loop_close(&owner.loop) == SC_PENDING);
    assert(owner.retirement.backing_retired && !owner.memory.storage &&
        !owner.memory.live && data_frees == 1 && owner.loop.acquired);
    assert(sc_native_loop_close(&owner.loop) == SC_PENDING && data_frees == 1);
    D_80083CA0.unk_A8A = 1;
    assert(sc_native_loop_close(&owner.loop) == SC_OK && data_frees == 1);
    assert(!owner.live && !owner.loop.acquired && !owner.retirement.acquired);
    assert(sc_native_loop_close(&owner.loop) == SC_INVALID);
    assert(mprotect(pixels, 0xc000U, PROT_READ | PROT_WRITE) == 0);
    assert(sc_ma_shutdown(&arena));
    assert(munmap(pixels, 0x3da7e0U) == 0);
    puts("NATIVE LIFETIME JOINED HOST PASSED: actual owner/memory/DP/crash/loop cleanup; protected freed controls; simulated OS/VI");
    return 0;
}
