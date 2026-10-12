#define SC_RETIREMENT_FIXTURE_MAIN retirement_fixture_main
#include "test_story_native_retirement.c"
#define _CRASH_SCREEN_H_
u32 osMemSize = 0x800000U;
volatile u32 sc_crash_active;
#include "../src/story/sc_native_crash.c"

static int crash_validate(void *context, const ScDpSnapshot *snapshot) {
    ScNativeRetirement *retirement = context;
    assert(interrupt_depth && retirement == expected_retirement);
    assert(snapshot->fallback == retirement->fallback);
    assert(retirement->fallback_completed && !retirement->pending);
    assert(vi_current == retirement->fallback->img_p && vi_next == vi_current);
    return owner_status;
}

int main(void) {
    ScNativeRetirement retirement = {0};
    ScNativeCrash crash = {0};
    ScNativeFramebufferSpan spans[2];
    unk_D_80068BB0 images[2], fallback;
    ScDpToken copied;
    unsigned i, before;
    expected_retirement = &retirement;
    spans[0].pixels = (RlU8 *)0x80400000U;
    spans[1].pixels = (RlU8 *)0x80500000U;
    spans[0].bytes = spans[1].bytes = SC_NATIVE_FRAMEBUFFER_BYTES;
    for (i = 0; i < 2; i++)
        func_800062E4(&images[i], 0, IMAGE_SIZE_BITS_16b, 640, 480, spans[i].pixels);
    func_800062E4(&fallback, 0, IMAGE_SIZE_BITS_16b, 640, 1, (u32)0x80010000U);
    memset(&D_80083CA0, 0, sizeof(D_80083CA0));
    D_80083CA0.unk_9E0 = &fallback;
    D_80083CA0.unk_A8A = 1;
    vi_current = spans[0].pixels; vi_next = spans[1].pixels;
    crash_screen_set_draw_info((u16 *)spans[0].pixels, 640, 16);
    assert(sc_native_retirement_acquire(&retirement, spans, images, &fallback,
        &owner_status, owner_validate) == SC_OK);
    assert(sc_native_retirement_step(&retirement) == SC_PENDING);
    assert(sc_native_crash_restore(&crash, &retirement.token, &retirement,
        crash_validate) == SC_PENDING);
    worker_done(1);
    assert(sc_native_retirement_step(&retirement) == SC_UNAVAILABLE);
    copied = retirement.token;
    assert(sc_native_crash_restore(&crash, &copied, &retirement,
        crash_validate) == SC_INVALID);
    assert(span_contains(&retirement, gCrashScreen.frameBuf));
    owner_status = SC_PENDING;
    assert(sc_native_crash_restore(&crash, &retirement.token, &retirement,
        crash_validate) == SC_PENDING);
    assert(span_contains(&retirement, gCrashScreen.frameBuf));
    owner_status = SC_OK;
    before = submitted;
    assert(sc_native_crash_restore(&crash, &retirement.token, &retirement,
        crash_validate) == SC_OK);
    assert(!interrupt_depth && submitted == before);
    assert(gCrashScreen.frameBuf == (void *)0xa07da800U &&
        gCrashScreen.width == 320 && gCrashScreen.height == 16);
    assert(sc_native_retirement_check(&retirement) == SC_OK);
    assert(sc_native_retirement_release(&retirement) == SC_OK);
    assert(sc_native_crash_restore(&crash, &retirement.token, &retirement,
        crash_validate) == SC_INVALID);
    puts("JOINED CRASH/RETIREMENT HOST PASSED: actual DP token/scheduler slices; simulated OS/VI, not native proof");
    return 0;
}
