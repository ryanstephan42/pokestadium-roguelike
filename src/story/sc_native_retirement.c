#ifdef STORY_NATIVE_UI
#include "sc_native_retirement.h"
#include "sc_core.h"

static int span_contains(const ScNativeRetirement *retirement, const void *pointer) {
    uintptr_t physical = (uintptr_t)pointer & 0x1FFFFFFFU;
    unsigned i;
    for (i = 0; i < 2; i++) {
        uintptr_t start = (uintptr_t)retirement->spans[i].pixels & 0x1FFFFFFFU;
        if (physical >= start && physical - start < retirement->spans[i].bytes) return 1;
    }
    return 0;
}

static int identities(const ScNativeRetirement *retirement) {
    unsigned i;
    uintptr_t first, second;
    int status;
    if (!retirement || !retirement->images || !retirement->fallback || !retirement->validate)
        return SC_INVALID;
    status = retirement->validate(retirement->context, retirement);
    if (status != SC_OK) return status;
    for (i = 0; i < 2; i++) {
        uintptr_t address = (uintptr_t)retirement->spans[i].pixels;
        const unk_D_80068BB0 *image = &retirement->images[i];
        if (retirement->spans[i].bytes != SC_NATIVE_FRAMEBUFFER_BYTES ||
            (address & 63) || address < 0x80400000U ||
            address > 0x807DA7E0U - SC_NATIVE_FRAMEBUFFER_BYTES ||
            image->img_p != retirement->spans[i].pixels || image->fmt ||
            image->size != IMAGE_SIZE_BITS_16b || image->width != 640 || image->height != 480 ||
            image->depth_p) return SC_INVALID;
    }
    first = (uintptr_t)retirement->spans[0].pixels;
    second = (uintptr_t)retirement->spans[1].pixels;
    if (!(first + SC_NATIVE_FRAMEBUFFER_BYTES <= second ||
          second + SC_NATIVE_FRAMEBUFFER_BYTES <= first)) return SC_INVALID;
    if (retirement->fallback == &retirement->images[0] ||
        retirement->fallback == &retirement->images[1] || !retirement->fallback->img_p ||
        retirement->fallback->fmt || retirement->fallback->size != IMAGE_SIZE_BITS_16b ||
        retirement->fallback->width != 640 || retirement->fallback->height != 1 ||
        retirement->fallback->depth_p ||
        ((uintptr_t)retirement->fallback->img_p & 63) ||
        (uintptr_t)retirement->fallback->img_p < 0x80000000U ||
        (uintptr_t)retirement->fallback->img_p > 0x80400000U - 1280U ||
        span_contains(retirement, retirement->fallback->img_p)) return SC_INVALID;
    return SC_OK;
}

int sc_native_retirement_acquire(ScNativeRetirement *retirement,
    const ScNativeFramebufferSpan spans[2], const unk_D_80068BB0 images[2],
    const unk_D_80068BB0 *fallback, void *context,
    int (*validate)(void *, const ScNativeRetirement *)) {
    int status;
    if (!retirement || retirement->acquired || retirement->token.control || !spans) return SC_INVALID;
    retirement->spans[0] = spans[0]; retirement->spans[1] = spans[1];
    retirement->images = images; retirement->fallback = fallback;
    retirement->context = context; retirement->validate = validate;
    retirement->pending = retirement->backing_retired = 0;
    retirement->snapshot_valid = retirement->fallback_completed = 0;
    retirement->retire_backing = NULL;
    status = identities(retirement);
    if (status != SC_OK) return status;
    status = sc_dp_acquire(retirement, &retirement->token);
    if (status == SC_OK) retirement->acquired = 1;
    return status;
}

static int inspect(ScNativeRetirement *retirement, int *needs_swap) {
    unsigned i;
    int status, clean = 1;
    ScDpSnapshot *snapshot;
    *needs_swap = 0;
    if (!retirement || !retirement->acquired || retirement->backing_retired ||
        retirement->token.control != retirement) return SC_INVALID;
    status = identities(retirement);
    if (status != SC_OK) return status;
    if (retirement->pending) return SC_PENDING;
    snapshot = &retirement->snapshot;
    status = sc_dp_snapshot(&retirement->token, snapshot);
    if (status != SC_OK) return status;
    retirement->snapshot_valid = 1;
    if (snapshot->fallback != retirement->fallback) return SC_INVALID;
    status = retirement->validate(retirement->context, retirement);
    if (status != SC_OK) return status;
    if (!snapshot->waiting || snapshot->requests || snapshot->completions)
        return SC_PENDING;
    if (snapshot->rsp_state > 3 || snapshot->rdp_state > 2) return SC_INVALID;
    if (snapshot->events || !retirement->fallback_completed) clean = 0;
    if (snapshot->rsp_state == 1 || snapshot->rsp_state == 2 ||
        snapshot->rdp_state == 1 || snapshot->task_completions) clean = 0;
    for (i = 0; i < 3; i++) {
        const UnkArray4 *frame = &snapshot->frames[i];
        if (frame->unk_01 > 1 || frame->unk_02 > 1) return SC_INVALID;
        if (frame->unk_04 || frame->unk_08) clean = 0;
        if (frame->unk_0C) clean = 0;
    }
    if (!snapshot->vi_current || !snapshot->vi_next ||
        ((uintptr_t)snapshot->vi_current & 0x1FFFFFFFU) !=
            ((uintptr_t)retirement->fallback->img_p & 0x1FFFFFFFU) ||
        ((uintptr_t)snapshot->vi_next & 0x1FFFFFFFU) !=
            ((uintptr_t)retirement->fallback->img_p & 0x1FFFFFFFU)) clean = 0;
    if (clean) {
        if (!snapshot->crash_frame || span_contains(retirement, snapshot->crash_frame))
            return SC_UNAVAILABLE;
        return SC_OK;
    }
    *needs_swap = 1;
    return SC_PENDING;
}

int sc_native_retirement_check(ScNativeRetirement *retirement) {
    int needs_swap;
    return inspect(retirement, &needs_swap);
}

int sc_native_retirement_step(ScNativeRetirement *retirement) {
    int status, needs_swap;
    if (!retirement || !retirement->acquired || retirement->backing_retired ||
        retirement->token.control != retirement) return SC_INVALID;
    if (retirement->pending) {
        status = identities(retirement);
        if (status != SC_OK) return status;
        status = sc_dp_poll_done(&retirement->token);
        if (status != SC_OK) return status;
        retirement->pending = 0;
        retirement->fallback_completed = 1;
    }
    status = inspect(retirement, &needs_swap);
    if (status != SC_PENDING || !needs_swap) return status;
    status = sc_dp_enqueue_fallback(&retirement->token);
    if (status == SC_OK) { retirement->pending = 1; return SC_PENDING; }
    return status;
}

int sc_native_retirement_release(ScNativeRetirement *retirement) {
    int status;
    if (!retirement || !retirement->acquired) return SC_INVALID;
    status = sc_native_retirement_step(retirement);
    if (status != SC_OK) return status;
    status = sc_dp_release(&retirement->token);
    if (status == SC_OK) retirement->acquired = 0;
    return status;
}

int sc_native_retirement_finalize(ScNativeRetirement *retirement,
    int (*retire_backing)(void *, const ScNativeRetirement *)) {
    int status;
    if (!retirement || !retirement->acquired || retirement->token.control != retirement ||
        !retire_backing) return SC_INVALID;
    if (retirement->retire_backing && retirement->retire_backing != retire_backing)
        return SC_INVALID;
    if (!retirement->backing_retired) {
        status = sc_native_retirement_step(retirement);
        if (status != SC_OK) return status;
        retirement->retire_backing = retire_backing;
        status = retire_backing(retirement->context, retirement);
        if (status != SC_OK) return status;
        retirement->backing_retired = 1;
    }
    /* Upper lease may now be freed. Only the external retained token/control
       can be touched; never repeat the backing callback or inspect images. */
    status = sc_dp_release(&retirement->token);
    if (status == SC_OK) retirement->acquired = 0;
    return status;
}
#endif
