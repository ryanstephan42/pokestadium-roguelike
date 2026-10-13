#include "story_private_rom_loader.h"

static bool contains(uint32_t low, uint32_t end, uint32_t base, uint32_t size) {
    return size && low <= base && base < end && size <= end - base;
}

bool spr_preflight(const SprRequest *r) {
    uint32_t end, rounded;
    if (!r || r->installed_bytes != 0x800000 || !r->cold_route ||
        !r->exclusive_staging || !r->unlocked || !r->cart_handle ||
        !r->generation || !r->operation || (r->ram & 7) ||
        (r->rom_offset & 1) || (r->bytes & 1) || !r->bytes ||
        r->bytes > 0x1000000 || r->owner_base < 0x400000 ||
        r->owner_end >= 0x7da800 ||
        !contains(r->owner_base, r->owner_end, r->ram, r->bytes) ||
        r->rom_end > 0x10000000 ||
        !contains(r->rom_low, r->rom_end, r->rom_offset, r->bytes))
        return false;
    end = r->ram + r->bytes;
    if (end > UINT32_MAX - 15) return false;
    rounded = (end + 15) & ~15u;
    return contains(r->owner_base, r->owner_end, r->ram & ~15u,
                    rounded - (r->ram & ~15u));
}

static void fail(SprLoader *l, SprError error, bool retained) {
    l->error = error;
    l->retained = retained;
    l->state = retained ? SPR_FAIL_STOP : SPR_REJECTED;
    l->ops.refusal(l->ctx, error, retained);
}

bool spr_start(SprLoader *l, const SprRequest *r, const SprOps *ops, void *ctx) {
    int result;
    if (!l || !ops || !ops->refusal) return false;
    if (l->state != SPR_IDLE) {
        ops->refusal(ctx, SPR_INPUT, l->retained);
        return false;
    }
    l->ops = *ops;
    l->ctx = ctx;
    if (!spr_preflight(r) || !ops->enqueue || !ops->receive ||
        !ops->prepare_cache || !ops->digest || !ops->container ||
        !ops->imports || !ops->relocate || !ops->publish_cache) {
        fail(l, SPR_INPUT, false);
        return false;
    }
    l->request = *r;
    l->retained = true;
    l->state = SPR_PREPARING;
    if (!ops->prepare_cache(ctx, &l->request)) {
        fail(l, SPR_CACHE, true);
        return false;
    }
    result = ops->enqueue(ctx, &l->request, &l->expected);
    if (result == -1) {
        fail(l, SPR_ENQUEUE, false);
        return false;
    }
    if (result != 0 || !l->expected) {
        fail(l, SPR_ENQUEUE, true);
        return false;
    }
    l->state = SPR_QUEUED;
    return true;
}

SprState spr_poll(SprLoader *l, bool deadline_expired) {
    const void *message = 0;
    int result;
    const SprOps *ops;
    if (l->state != SPR_QUEUED) return l->state;
    if (deadline_expired) {
        fail(l, SPR_TIMEOUT, true);
        return l->state;
    }

    result = l->ops.receive(l->ctx, &message);
    if (result == 0) {
        return l->state;
    }
    if (result != 1 || message != l->expected) {
        fail(l, SPR_COMPLETION, true);
        return l->state;
    }
    l->state = SPR_VALIDATING;
    ops = &l->ops;
    if (!ops->digest(l->ctx, &l->request)) fail(l, SPR_DIGEST, true);
    else if (!ops->container(l->ctx, &l->request)) fail(l, SPR_CONTAINER, true);
    else if (!ops->imports(l->ctx, &l->request)) fail(l, SPR_IMPORTS, true);
    else if (!ops->relocate(l->ctx, &l->request)) fail(l, SPR_RELOCATION, true);
    else if (!ops->publish_cache(l->ctx, &l->request)) fail(l, SPR_CACHE, true);
    else l->state = SPR_PUBLISHED;
    return l->state;
}

bool spr_retire(SprLoader *l, uint32_t generation, uint32_t operation) {
    if (!l || l->state != SPR_PUBLISHED || !l->retained || !l->expected ||
        !generation || !operation || l->request.generation != generation ||
        l->request.operation != operation) return false;
    l->retained = false;
    l->expected = 0;
    l->request.generation = l->request.operation = 0;
    l->ctx = 0;
    l->state = SPR_IDLE;
    return true;
}
