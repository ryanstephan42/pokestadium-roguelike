#define main display_fixture_main
#include "test_story_native_display.c"
#undef main
#include <setjmp.h>
#define _SRC_DP_INTRO_H_
#define _5580_H_
#define OS_MESG_NOBLOCK 0
#include "../build/story/native-retirement-review-v1/seam-header.inc"
static int thread_id = 6, interrupt_depth;
static unsigned submitted, stopped, logs;
static jmp_buf stop_jump;
int osGetThreadId(void *thread) { (void)thread; return thread_id; }
u32 __osDisableInt(void) { return (u32)interrupt_depth++; }
void __osRestoreInt(u32 mask) { interrupt_depth = (int)mask; }
void *osViGetCurrentFramebuffer(void) { return vi_current; }
void *osViGetNextFramebuffer(void) { return vi_next; }
void osStopThread(void *thread) { (void)thread; stopped++; longjmp(stop_jump, 1); }
void osSyncPrintf(const char *format, ...) { assert(format); logs++; }
int osSendMesg(OSMesgQueue *queue, OSMesg message, int mode) {
    assert(!interrupt_depth && mode == OS_MESG_NOBLOCK);
    assert(queue == &D_80083CA0.unk_9EC && !message);
    if (queue->validCount) return -1;
    queue->validCount = 1; D_80083CA0.unk_A8A = 0; submitted++; return 0;
}
int osRecvMesg(OSMesgQueue *queue, OSMesg *message, int mode) {
    (void)message;
    assert(!interrupt_depth && mode == OS_MESG_NOBLOCK && queue == &D_80083CA0.unk_A04);
    if (!queue->validCount) return -1;
    queue->validCount--; return 0;
}
#include "../build/story/native-retirement-review-v1/seam-body.inc"
#include "../build/story/native-retirement-review-v1/admission-body.inc"
#include "../src/story/sc_native_retirement.c"

static int owner_status;
static int free_status, defer_token_release;
static unsigned frees, validations;
static ScNativeRetirement *expected_retirement;
static int owner_validate(void *context, const ScNativeRetirement *retirement) {
    assert(context == &owner_status && retirement == expected_retirement);
    validations++;
    return owner_status;
}
static int retire_backing(void *context, const ScNativeRetirement *retirement) {
    assert(context == &owner_status && sScDpControl == retirement);
    assert(!retirement->pending && retirement->acquired);
    assert(sc_native_retirement_check(expected_retirement) == SC_OK);
    if (free_status != SC_OK) return free_status;
    free((void *)retirement->images);
    frees++;
    owner_status = SC_INVALID;
    if (defer_token_release) D_80083CA0.unk_A8A = 0;
    return SC_OK;
}
static void worker_done(int swap) {
    assert(D_80083CA0.unk_9EC.validCount && !D_80083CA0.unk_A04.validCount);
    D_80083CA0.unk_9EC.validCount = 0;
    func_8000152C(NULL); func_800015A8();
    D_80083CA0.unk_A04.validCount = 1; D_80083CA0.unk_A8A = 1;
    if (swap) vi_current = vi_next;
}
int main(void) {
    ScNativeRetirement retirement, copied;
    ScNativeFramebufferSpan spans[2];
    unk_D_80068BB0 images[2], fallback;
    ScDpToken old_token, other_token;
    unk_D_80068BB0 *freed_images;
    unsigned i, before;
    memset(&retirement, 0, sizeof(retirement)); memset(&copied, 0, sizeof(copied));
    memset(&fallback, 0, sizeof(fallback)); memset(&D_80083CA0, 0, sizeof(D_80083CA0));
    memset(&other_token, 0, sizeof(other_token));
    expected_retirement = &retirement;
    spans[0].pixels = (RlU8 *)0x80400000U; spans[1].pixels = (RlU8 *)0x80500000U;
    spans[0].bytes = spans[1].bytes = SC_NATIVE_FRAMEBUFFER_BYTES;
    for (i = 0; i < 2; i++) func_800062E4(&images[i], 0, IMAGE_SIZE_BITS_16b, 640, 480, spans[i].pixels);
    func_800062E4(&fallback, 0, IMAGE_SIZE_BITS_16b, 640, 1, (u32)0x80010000U);
    D_80083CA0.unk_9E0 = &fallback;
    crash_screen_set_draw_info((u16 *)0x807DA800U, 320, 16);
    vi_current = (void *)0xA0400000U; vi_next = spans[1].pixels;
    assert(sc_native_retirement_acquire(&retirement, spans, images, &fallback,
        &owner_status, owner_validate) == SC_PENDING);
    D_80083CA0.unk_A8A = 1; D_80083CA0.unk_A04.validCount = 1;
    assert(sc_native_retirement_acquire(&retirement, spans, images, &fallback,
        &owner_status, owner_validate) == SC_PENDING);
    D_80083CA0.unk_A04.validCount = 0; thread_id = 5;
    assert(sc_native_retirement_acquire(&retirement, spans, images, &fallback,
        &owner_status, owner_validate) == SC_INVALID);
    thread_id = 6;
    D_80083CA0.unk_9EC.validCount = 1;
    assert(sc_native_retirement_acquire(&retirement, spans, images, &fallback,
        &owner_status, owner_validate) == SC_PENDING);
    D_80083CA0.unk_9EC.validCount = 0;
    fallback.height = 480;
    assert(sc_native_retirement_acquire(&retirement, spans, images, &fallback,
        &owner_status, owner_validate) == SC_INVALID);
    fallback.height = 1;
    assert(sc_native_retirement_acquire(&retirement, spans, images, &fallback,
        &owner_status, owner_validate) == SC_OK);
    old_token = retirement.token;
    assert(sc_dp_acquire(&copied, &other_token) == SC_INVALID);
    copied = retirement;
    /* Tokens bind both their generation and caller-retained helper identity. */
    assert(sc_dp_release(&other_token) == SC_INVALID);
    assert(sc_native_retirement_step(&copied) == SC_INVALID);
    other_token = retirement.token;
    assert(sc_dp_snapshot(&other_token, &retirement.snapshot) == SC_INVALID);
    retirement.token.generation++;
    assert(sc_dp_snapshot(&retirement.token, &retirement.snapshot) == SC_INVALID);
    retirement.token.generation--;
    retirement.token.control = &copied;
    assert(sc_dp_snapshot(&retirement.token, &retirement.snapshot) == SC_INVALID);
    retirement.token.control = &retirement;
    if (!setjmp(stop_jump)) { func_80001BA8(NULL); assert(0); }
    if (!setjmp(stop_jump)) { func_80001B7C(); assert(0); }
    assert(stopped == 2 && logs == 2);
    D_80083CA0.unk_A8C.unk_0C = &images[0]; D_80083CA0.unk_A8C.unk_04 = 1;
    D_80083CA0.unk_A9C = D_80083CA0.unk_A8C; D_80083CA0.unk_AAC = D_80083CA0.unk_A8C;
    owner_status = SC_UNAVAILABLE;
    assert(sc_native_retirement_step(&retirement) == SC_UNAVAILABLE && !submitted);
    owner_status = SC_OK;
    assert(sc_native_retirement_step(&retirement) == SC_PENDING && submitted == 1);
    assert(sc_dp_quiescent(&retirement.token) == SC_PENDING);
    before = submitted;
    assert(sc_native_retirement_step(&retirement) == SC_PENDING && submitted == before);
    thread_id = 5;
    assert(sc_native_retirement_step(&retirement) == SC_INVALID && submitted == before);
    thread_id = 6;
    assert(sc_dp_release(&retirement.token) == SC_PENDING);
    for (i = 0; i < 3; i++) {
        worker_done(0);
        assert(sc_native_retirement_step(&retirement) == SC_PENDING);
    }
    assert(vi_next == fallback.img_p && vi_current == (void *)0xA0400000U);
    assert(repeat_line == 1);
    worker_done(1);
    assert(span_contains(&retirement, gCrashScreen.frameBuf));
    assert(sc_native_retirement_step(&retirement) == SC_UNAVAILABLE);
    before = __osDisableInt();
    assert(sc_dp_quiescent(&retirement.token) == SC_OK && interrupt_depth == 1);
    /* Owner authority restores an admitted full-size crash buffer;
       native NULL fallback does not restore this reference. */
    crash_screen_set_draw_info((u16 *)0x807DA800U, 320, 16);
    __osRestoreInt(before);
    assert(sc_native_retirement_step(&retirement) == SC_OK && retirement.acquired);
    D_80083CA0.unk_AAC.unk_0C = &images[1];
    assert(sc_native_retirement_step(&retirement) == SC_PENDING);
    worker_done(1);
    assert(sc_native_retirement_step(&retirement) == SC_OK);
    D_80083CA0.unk_A20.unk_1C = 1; D_80083CA0.unk_A20.unk_1E = 1;
    before = submitted;
    assert(sc_native_retirement_check(&retirement) == SC_PENDING && submitted == before);
    D_80083CA0.unk_A20.unk_1C = 2;
    assert(sc_native_retirement_check(&retirement) == SC_PENDING);
    D_80083CA0.unk_A20.unk_1C = 3;
    assert(sc_native_retirement_check(&retirement) == SC_PENDING);
    D_80083CA0.unk_A20.unk_1E = 2;
    assert(sc_native_retirement_check(&retirement) == SC_OK);
    D_80083CA0.unk_A9C.unk_01 = 2;
    assert(sc_native_retirement_check(&retirement) == SC_INVALID);
    D_80083CA0.unk_A9C.unk_01 = 0;
    D_80083CA0.unk_A8C.unk_0C = (unk_D_80068BB0 *)1;
    assert(sc_native_retirement_step(&retirement) == SC_PENDING);
    worker_done(1);
    assert(sc_native_retirement_step(&retirement) == SC_OK);
    vi_current = NULL;
    assert(sc_native_retirement_check(&retirement) == SC_PENDING);
    assert(sc_native_retirement_step(&retirement) == SC_PENDING);
    worker_done(1);
    assert(sc_native_retirement_step(&retirement) == SC_OK);
    D_80083CA0.unk_1C0.validCount = 1;
    assert(sc_native_retirement_step(&retirement) == SC_PENDING);
    worker_done(1);
    assert(sc_native_retirement_step(&retirement) == SC_OK && !D_80083CA0.unk_1C0.validCount);
    vi_next = (void *)0xA0500000U;
    assert(sc_native_retirement_release(&retirement) == SC_PENDING && retirement.acquired);
    worker_done(1);
    assert(sc_native_retirement_release(&retirement) == SC_OK && !retirement.acquired);
    assert(sc_dp_snapshot(&old_token, &retirement.snapshot) == SC_INVALID);
    assert(sc_native_retirement_acquire(&retirement, spans, images, &fallback,
        &owner_status, owner_validate) == SC_OK);
    assert(sc_dp_snapshot(&old_token, &retirement.snapshot) == SC_INVALID);
    assert(sc_native_retirement_release(&retirement) == SC_PENDING);
    worker_done(1);
    assert(sc_native_retirement_release(&retirement) == SC_OK);
    freed_images = malloc(sizeof(images)); assert(freed_images);
    memcpy(freed_images, images, sizeof(images));
    assert(sc_native_retirement_acquire(&retirement, spans, freed_images, &fallback,
        &owner_status, owner_validate) == SC_OK);
    free_status = SC_PENDING;
    assert(sc_native_retirement_finalize(&retirement, retire_backing) == SC_PENDING && !frees);
    worker_done(1);
    assert(sc_native_retirement_step(&retirement) == SC_OK);
    vi_next = spans[0].pixels;
    assert(sc_native_retirement_finalize(&retirement, retire_backing) == SC_PENDING && !frees);
    worker_done(1);
    assert(sc_native_retirement_finalize(&retirement, retire_backing) == SC_PENDING && !frees);
    free_status = SC_UNAVAILABLE;
    assert(sc_native_retirement_finalize(&retirement, retire_backing) == SC_UNAVAILABLE && !frees);
    free_status = SC_OK; defer_token_release = 1;
    assert(sc_native_retirement_finalize(&retirement, retire_backing) == SC_PENDING && frees == 1);
    assert(retirement.backing_retired && retirement.acquired && sScDpControl == &retirement);
    before = validations;
    assert(sc_native_retirement_step(&retirement) == SC_INVALID);
    assert(sc_native_retirement_finalize(&retirement, owner_validate) == SC_INVALID);
    thread_id = 5;
    assert(sc_native_retirement_finalize(&retirement, retire_backing) == SC_INVALID);
    assert(frees == 1 && validations == before && sScDpControl == &retirement);
    thread_id = 6;
    D_80083CA0.unk_A8A = 1;
    assert(sc_native_retirement_finalize(&retirement, retire_backing) == SC_OK);
    assert(frees == 1 && validations == before && !retirement.acquired && !sScDpControl);
    owner_status = SC_OK;
    sScDpGeneration = 0xFFFFFFFFU;
    assert(sc_native_retirement_acquire(&retirement, spans, images, &fallback,
        &owner_status, owner_validate) == SC_UNAVAILABLE);
    assert(!interrupt_depth && !sScDpControl && !retirement.acquired);
    puts("Production retirement seam/helper: actual NULL scheduler path; delayed DONE/VI, aliases, references, token thread/stale/exhaustion/fail-stop; HOST OS only; OK");
    return 0;
}
