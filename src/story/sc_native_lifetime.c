#ifdef STORY_NATIVE_UI
#include "sc_native_lifetime.h"
#include "src/util.h"
#include "src/crash_screen.h"

extern UnkStruct80083CA0_2 D_80083CA0;

static int binding_matches(const ScNativeLifetime *owner,
                            const ScNativeCampaignBacking *backing) {
    const ScNativeCampaignBacking *bound = &owner->binding;
    unsigned i;
    if (!backing || backing != &owner->loop.backing ||
        backing->ui != bound->ui || backing->request != bound->request ||
        backing->driver != bound->driver || backing->native_ui != bound->native_ui ||
        backing->pak != bound->pak || backing->event != bound->event ||
        backing->catalog != bound->catalog || backing->seed != bound->seed ||
        backing->lease_id != bound->lease_id)
        return 0;
    for (i = 0; i < 2; i++)
        if (backing->framebuffer[i].pixels != bound->framebuffer[i].pixels ||
            backing->framebuffer[i].bytes != bound->framebuffer[i].bytes)
            return 0;
    return 1;
}

static int fallback_valid(const ScNativeLifetime *owner) {
    const unk_D_80068BB0 *fallback = owner->fallback;
    const unk_D_80068BB0 *value = &owner->fallback_value;
    uintptr_t address;
    if (!fallback || D_80083CA0.unk_9E0 != fallback ||
        fallback->fmt != value->fmt || fallback->size != value->size ||
        fallback->width != value->width || fallback->height != value->height ||
        fallback->img_p != value->img_p || fallback->depth_p != value->depth_p)
        return 0;
    address = (uintptr_t)fallback->img_p;
    return !fallback->fmt && fallback->size == IMAGE_SIZE_BITS_16b &&
        fallback->width == 640 && fallback->height == 1 && !fallback->depth_p &&
        !(address & 63U) && address >= 0x80000000U &&
        address <= 0x80400000U - 1280U;
}

int sc_native_lifetime_validate(void *context, const ScNativeCampaignBacking *backing) {
    ScNativeLifetime *owner = context;
    if (!owner || !owner->live || !binding_matches(owner, backing))
        return SC_INVALID;
    if (owner->retirement.backing_retired)
        return !owner->memory.live && !owner->memory.storage &&
            owner->retirement.acquired && owner->retirement.token.control == &owner->retirement &&
            !owner->loop.display_acquired && !owner->loop.resources_acquired &&
            !owner->loop.pak_bound ? SC_OK : SC_INVALID;
    if (osMemSize != 0x800000U || gExpansionRAMStart || sc_crash_active ||
        !fallback_valid(owner))
        return SC_UNAVAILABLE;
    return sc_native_memory_validate(&owner->memory, backing);
}

static int retirement_validate(void *context, const ScNativeRetirement *retirement) {
    ScNativeLifetime *owner = context;
    unsigned i;
    int status;
    if (!owner || retirement != &owner->retirement ||
        retirement->images != owner->display.images ||
        retirement->fallback != owner->fallback)
        return SC_INVALID;
    status = sc_native_lifetime_validate(owner, &owner->loop.backing);
    if (status != SC_OK) return status;
    for (i = 0; i < 2; i++)
        if (retirement->spans[i].pixels != owner->binding.framebuffer[i].pixels ||
            retirement->spans[i].bytes != owner->binding.framebuffer[i].bytes)
            return SC_INVALID;
    if (retirement->snapshot_valid && retirement->snapshot.fallback != owner->fallback)
        return SC_INVALID;
    return SC_OK;
}

int sc_native_lifetime_acquire(ScNativeLifetime *owner, ScMaArena *arena,
                               const ScCatalog *catalog, RlU32 seed,
                               ScNativeCampaignBacking *backing) {
    u32 interrupts;
    int status;
    if (!owner || !backing || backing != &owner->loop.backing || owner->live ||
        owner->retirement.acquired || owner->retirement.token.control)
        return SC_INVALID;
    if (osMemSize != 0x800000U || gExpansionRAMStart) return SC_UNAVAILABLE;
    interrupts = __osDisableInt();
    owner->fallback = D_80083CA0.unk_9E0;
    if (owner->fallback) owner->fallback_value = *owner->fallback;
    status = fallback_valid(owner) ? SC_OK : SC_UNAVAILABLE;
    __osRestoreInt(interrupts);
    if (status != SC_OK) return status;
    status = sc_native_memory_acquire(&owner->memory, arena, catalog, seed,
        &owner->loop, &owner->display, backing);
    if (status == SC_OK) {
        owner->binding = *backing;
        owner->live = 1;
    }
    return status;
}

static int crash_validate(void *context, const ScDpSnapshot *snapshot) {
    ScNativeLifetime *owner = context;
    const ScNativeRetirement *retirement = &owner->retirement;
    uintptr_t pixels;
    int status = retirement_validate(owner, retirement);
    if (status != SC_OK) return status;
    if (!retirement->fallback_completed || retirement->pending ||
        snapshot->fallback != owner->fallback || !snapshot->vi_current || !snapshot->vi_next)
        return SC_PENDING;
    pixels = (uintptr_t)owner->fallback_value.img_p & 0x1fffffffU;
    return ((uintptr_t)snapshot->vi_current & 0x1fffffffU) == pixels &&
        ((uintptr_t)snapshot->vi_next & 0x1fffffffU) == pixels ? SC_OK : SC_PENDING;
}

int sc_native_lifetime_retire(void *context, const ScNativeCampaignBacking *backing,
                              const unk_D_80068BB0 *images) {
    ScNativeLifetime *owner = context;
    int status = sc_native_lifetime_validate(owner, backing);
    if (status != SC_OK) return status;
    if (owner->retirement.backing_retired || images != owner->display.images ||
        !owner->display.acquired || !owner->display.closing || owner->display.frame ||
        backing->pak->session)
        return SC_INVALID;
    if (!owner->retirement.acquired) {
        status = sc_native_retirement_acquire(&owner->retirement, backing->framebuffer,
            images, owner->fallback, owner, retirement_validate);
        if (status != SC_OK) return status;
    }
    status = sc_native_retirement_step(&owner->retirement);
    if (status == SC_UNAVAILABLE && owner->retirement.snapshot_valid) {
        status = sc_native_crash_restore(&owner->crash, &owner->retirement.token,
            owner, crash_validate);
        if (status == SC_OK) status = sc_native_retirement_check(&owner->retirement);
    }
    return status;
}

static int retired(void *context, const ScNativeCampaignBacking *backing) {
    ScNativeLifetime *owner = context;
    if (!binding_matches(owner, backing)) return SC_INVALID;
    if (sc_crash_active) return SC_UNAVAILABLE;
    return sc_native_retirement_check(&owner->retirement);
}

static int retire_backing(void *context, const ScNativeRetirement *retirement) {
    ScNativeLifetime *owner = context;
    u32 interrupts;
    int status;
    if (retirement != &owner->retirement) return SC_INVALID;
    interrupts = __osDisableInt();
    status = sc_crash_active ? SC_UNAVAILABLE :
        sc_native_memory_release(&owner->memory, &owner->loop.backing, retired, owner);
    __osRestoreInt(interrupts);
    return status;
}

int sc_native_lifetime_release(void *context, const ScNativeCampaignBacking *backing) {
    ScNativeLifetime *owner = context;
    int status = sc_native_lifetime_validate(owner, backing);
    if (status != SC_OK) return status;
    if (owner->loop.display_acquired || owner->loop.resources_acquired ||
        owner->loop.pak_bound || !owner->retirement.acquired)
        return SC_PENDING;
    status = sc_native_retirement_finalize(&owner->retirement, retire_backing);
    if (status == SC_OK) owner->live = 0;
    return status;
}
#endif
