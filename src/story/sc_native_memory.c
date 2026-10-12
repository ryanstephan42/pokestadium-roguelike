#ifdef STORY_NATIVE_UI
#include "sc_native_memory.h"

#define SC_MEMORY_BACKING_BYTES 0x3da7e0U

static void clear(void *pointer, unsigned bytes) {
    RlU8 *p = pointer;
    while (bytes--) *p++ = 0;
}

static int error(ScNativeMemory *memory, const char *message) {
    if (memory) memory->failed = 1;
    osSyncPrintf("%s\n", message);
    return SC_UNAVAILABLE;
}

static unsigned controls(void) {
    return (sizeof(ScNativeCampaignStorage) + 63U) & ~63U;
}

static void tuple(ScNativeMemory *memory, ScNativeCampaignBacking *backing) {
    ScNativeCampaignStorage *storage = memory->storage;
    RlU8 *pixels = (RlU8 *)storage + controls();
    backing->ui = &storage->ui;
    backing->request = &storage->request;
    backing->driver = &storage->driver;
    backing->native_ui = &storage->native_ui;
    backing->pak = &storage->pak;
    backing->event = &storage->event;
    backing->catalog = memory->catalog;
    backing->arena = memory->arena;
    backing->seed = memory->seed;
    backing->lease_id = memory->lease.ticket;
    backing->framebuffer[0].pixels = pixels;
    backing->framebuffer[1].pixels = pixels + SC_NATIVE_FRAMEBUFFER_BYTES;
    backing->framebuffer[0].bytes = backing->framebuffer[1].bytes =
        SC_NATIVE_FRAMEBUFFER_BYTES;
}

int sc_native_memory_acquire(ScNativeMemory *memory, ScMaArena *arena,
                             const ScCatalog *catalog, RlU32 seed,
                             ScNativeLoop *loop, ScNativeDisplay *display,
                             ScNativeCampaignBacking *backing) {
    RlU32 address, aligned;
    RlU8 *mapped;
    if (!backing) return SC_INVALID;
    clear(backing, sizeof(*backing));
    if (!memory || memory->live || memory->failed || !arena || !catalog ||
        !loop || !display || loop->acquired || display->acquired ||
        !arena->active || arena->end != 0x7da7e0U ||
        arena->backing_bytes != SC_MEMORY_BACKING_BYTES)
        return SC_INVALID;
#ifndef SC_NATIVE_MEMORY_HOST
    if (osMemSize != 0x800000U || arena->backing != (RlU8 *)0x80400000U)
        return SC_UNAVAILABLE;
#endif
    memory->arena = arena;
    memory->bytes = controls() + 2U * SC_NATIVE_FRAMEBUFFER_BYTES + 64U;
    if (!sc_ma_alloc(arena, memory->bytes, &memory->allocation))
        return SC_FULL;
    if (!sc_ma_data(arena, memory->allocation, &memory->lease)) {
        if (!sc_ma_release(arena, memory->allocation))
            return error(memory, "Story memory: unleased acquisition cannot retire");
        return SC_UNAVAILABLE;
    }
    memory->live = 1;
    address = sc_ma_address(arena, memory->allocation);
    aligned = (address + 63U) & ~63U;
    if (address < 0x80400000U || aligned < address ||
        aligned > 0x807da7e0U - controls() - 2U * SC_NATIVE_FRAMEBUFFER_BYTES)
        return error(memory, "Story memory: admitted physical extent invalid");
    memory->offset = aligned - address;
    mapped = sc_ma_map(arena, &memory->lease, memory->offset,
        controls() + 2U * SC_NATIVE_FRAMEBUFFER_BYTES, SC_MA_READ | SC_MA_WRITE);
    if (!mapped) return error(memory, "Story memory: exclusive backing map refused");
#ifndef SC_NATIVE_MEMORY_HOST
    if ((RlU32)mapped != aligned)
        return error(memory, "Story memory: CPU/physical mapping mismatch");
#endif
    memory->storage = (ScNativeCampaignStorage *)mapped;
    memory->catalog = catalog;
    memory->seed = seed;
    memory->loop = loop;
    memory->display = display;
    clear(mapped, controls());
    tuple(memory, backing);
#ifndef SC_NATIVE_MEMORY_HOST
    /* New exclusive pixels may reuse dirty CPU lines from an older data lease. */
    osWritebackDCache(backing->framebuffer[0].pixels, 2U * SC_NATIVE_FRAMEBUFFER_BYTES);
    osInvalDCache(backing->framebuffer[0].pixels, 2U * SC_NATIVE_FRAMEBUFFER_BYTES);
#endif
    return SC_OK;
}

int sc_native_memory_validate(ScNativeMemory *memory,
                              const ScNativeCampaignBacking *backing) {
    ScNativeCampaignBacking expected;
    unsigned i;
    RlU8 *mapped;
    if (!memory || !memory->live || memory->failed || !backing ||
        !memory->arena || !memory->catalog || !memory->storage ||
        !memory->loop || !memory->display ||
        memory->lease.kind != 3 || !memory->lease.ticket ||
        memory->allocation.slot != memory->lease.handle.slot ||
        memory->allocation.generation != memory->lease.handle.generation ||
        memory->bytes != controls() + 2U * SC_NATIVE_FRAMEBUFFER_BYTES + 64U ||
        memory->offset > 63U)
        return SC_INVALID;
    mapped = sc_ma_map(memory->arena, &memory->lease, memory->offset,
        controls() + 2U * SC_NATIVE_FRAMEBUFFER_BYTES, SC_MA_READ | SC_MA_WRITE);
    if (mapped != (RlU8 *)memory->storage ||
        memory->arena->end != 0x7da7e0U ||
        memory->arena->backing_bytes != SC_MEMORY_BACKING_BYTES)
        return SC_INVALID;
#ifndef SC_NATIVE_MEMORY_HOST
    if (osMemSize != 0x800000U ||
        memory->arena->backing != (RlU8 *)0x80400000U ||
        (RlU32)mapped != sc_ma_address(memory->arena, memory->allocation) + memory->offset ||
        ((RlU32)mapped & 63U))
        return SC_INVALID;
#endif
    tuple(memory, &expected);
    if (backing->ui != expected.ui || backing->request != expected.request ||
        backing->driver != expected.driver || backing->native_ui != expected.native_ui ||
        backing->pak != expected.pak || backing->event != expected.event ||
        backing->catalog != expected.catalog || backing->arena != expected.arena ||
        backing->seed != expected.seed ||
        backing->lease_id != expected.lease_id)
        return SC_INVALID;
    for (i = 0; i < 2; i++)
        if (backing->framebuffer[i].pixels != expected.framebuffer[i].pixels ||
            backing->framebuffer[i].bytes != expected.framebuffer[i].bytes)
            return SC_INVALID;
    return SC_OK;
}

int sc_native_memory_release(ScNativeMemory *memory,
    const ScNativeCampaignBacking *backing,
    int (*retired)(void *, const ScNativeCampaignBacking *), void *context) {
    ScNativeCampaignStorage *storage;
    int status = sc_native_memory_validate(memory, backing);
    if (status != SC_OK || !retired) return SC_INVALID;
    storage = memory->storage;
    if ((memory->loop->prepared && !memory->loop->exited) ||
        memory->loop->held_operation || memory->loop->queued ||
        memory->loop->drained_operation || memory->loop->display_acquired ||
        memory->loop->resources_acquired || memory->loop->pak_bound ||
        memory->display->acquired ||
        storage->driver.bound || storage->native_ui.acquired ||
        storage->native_ui.prepared || storage->pak.bound || storage->pak.session)
        return SC_PENDING;
    status = retired(context, backing);
    if (status != SC_OK) return status;
    if (sc_native_memory_validate(memory, backing) != SC_OK)
        return SC_INVALID;
    if (!sc_ma_end(memory->arena, &memory->lease) ||
        !sc_ma_release(memory->arena, memory->allocation))
        return error(memory, "Story memory: retirement authority failure; backing retained");
    memory->live = 0;
    memory->storage = NULL;
    memory->catalog = NULL;
    return SC_OK;
}
#endif
