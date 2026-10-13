#include "src/story/sc_native_crash.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <setjmp.h>

CrashScreen gCrashScreen;
volatile u32 sc_crash_active;
u32 osMemSize = 0x800000U;
static ScDpToken admitted;
static ScDpSnapshot snapshot;
static ScNativeCrash control;
static unsigned interrupts, writes, validations, reads;
static int quiescent_status, snapshot_status, validation_status, bad_write, fail_read;
typedef struct OSThread { int unused; } OSThread;
typedef void *OSMesg;
typedef int s32;
#define UNUSED __attribute__((unused))
#define TRUE 1
#define OS_EVENT_CPU_BREAK 10
#define OS_EVENT_FAULT 12
static OSThread faulted;
static jmp_buf draw_jump;
static unsigned receives, stops, waits, draws;
static u16 *captured_pixels;
static u16 fault_pixels[320 * 240];
static int restore(void);
#include "crash-rect-body.inc"
void osSetEventMesg(int event, int *queue, OSMesg message) {
    assert((event == OS_EVENT_CPU_BREAK || event == OS_EVENT_FAULT) &&
        queue == &gCrashScreen.queue && message);
}
int osRecvMesg(int *queue, OSMesg *message, int mode) {
    assert(queue == &gCrashScreen.queue && message && mode == 1);
    ++receives;
    return 0;
}
OSThread *crash_screen_get_faulted_thread(void) {
    return receives > 1 ? &faulted : NULL;
}
void osStopThread(OSThread *thread) {
    assert(thread == &faulted && sc_crash_active);
    ++stops;
}
void crash_screen_wait_for_button_combo(void) {
    assert(sc_crash_active);
    assert(restore() == SC_UNAVAILABLE && !writes);
    ++waits;
}
void crash_screen_draw(OSThread *thread) {
    assert(thread == &faulted && sc_crash_active);
    captured_pixels = gCrashScreen.frameBuf;
    assert(restore() == SC_UNAVAILABLE && !writes);
    crash_screen_draw_rect(0, 0, 1, 1);
    assert(captured_pixels == gCrashScreen.frameBuf && captured_pixels[0] == 1);
    ++draws;
    longjmp(draw_jump, 1);
}
#include "crash-thread-body.inc"

u32 __osDisableInt(void) {
    unsigned previous = interrupts;
    interrupts = 0;
    return previous;
}
void __osRestoreInt(u32 previous) { interrupts = previous; }
int sc_dp_quiescent(const ScDpToken *token) {
    assert(!interrupts);
    return token == &admitted && token->control == &control && token->generation == 1 ?
        quiescent_status : SC_INVALID;
}
int sc_dp_snapshot(const ScDpToken *token, ScDpSnapshot *out) {
    assert(!interrupts && token == &admitted);
    ++reads;
    if (snapshot_status != SC_OK) return snapshot_status;
    if (fail_read && reads == 2) return SC_UNAVAILABLE;
    *out = snapshot;
    out->crash_frame = gCrashScreen.frameBuf;
    return SC_OK;
}
void crash_screen_set_draw_info(u16 *pixels, u16 width, u16 height) {
    assert(!interrupts && pixels == (u16 *)0xa07da800U && width == 320 && height == 16);
    ++writes;
    gCrashScreen.frameBuf = bad_write ? (u16 *)0xa0400000U : pixels;
    gCrashScreen.width = width;
    gCrashScreen.height = height;
}
static int validate(void *context, const ScDpSnapshot *view) {
    assert(!interrupts && context == &control && view == &control.snapshot);
    ++validations;
    return validation_status;
}
static void reset(void) {
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.waiting = 1;
    snapshot.rsp_state = 3;
    snapshot.rdp_state = 2;
    admitted.control = &control;
    admitted.generation = 1;
    gCrashScreen.frameBuf = (u16 *)0xa0400000U;
    writes = validations = reads = 0;
    quiescent_status = snapshot_status = validation_status = SC_OK;
    bad_write = fail_read = 0;
    interrupts = 1;
    osMemSize = 0x800000U;
    sc_crash_active = 0;
}
static int restore(void) {
    int status = sc_native_crash_restore(&control, &admitted, &control, validate);
    assert(interrupts == 1);
    return status;
}
int main(void) {
    ScDpToken copied;
    unsigned i;
    reset();
    assert(restore() == SC_OK && writes == 1 && validations == 1 && reads == 2);
    assert(gCrashScreen.frameBuf == (u16 *)0xa07da800U);
    reset();
    interrupts = 0;
    assert(sc_native_crash_restore(&control, &admitted, &control, validate) == SC_OK);
    assert(!interrupts);
    reset();
    assert(sc_native_crash_restore(NULL, &admitted, &control, validate) == SC_INVALID);
    assert(sc_native_crash_restore(&control, NULL, &control, validate) == SC_INVALID);
    assert(sc_native_crash_restore(&control, &admitted, &control, NULL) == SC_INVALID);
    osMemSize = 0x400000U;
    assert(restore() == SC_UNAVAILABLE && !writes && !reads);
    reset();
    copied = admitted;
    assert(sc_native_crash_restore(&control, &copied, &control, validate) == SC_INVALID);
    assert(!writes && interrupts == 1);
    reset();
    quiescent_status = SC_PENDING;
    assert(restore() == SC_PENDING && !writes && !reads);
    reset();
    snapshot_status = SC_UNAVAILABLE;
    assert(restore() == SC_UNAVAILABLE && !writes && !validations);
    for (i = 0; i < 3; i++) {
        reset(); snapshot.frames[i].unk_04 = 1;
        assert(restore() == SC_PENDING && !writes && !validations);
        reset(); snapshot.frames[i].unk_08 = 1;
        assert(restore() == SC_PENDING && !writes && !validations);
        reset(); snapshot.frames[i].unk_0C = (void *)1;
        assert(restore() == SC_PENDING && !writes && !validations);
        reset(); snapshot.frames[i].unk_01 = 2;
        assert(restore() == SC_UNAVAILABLE && !writes);
        reset(); snapshot.frames[i].unk_02 = 2;
        assert(restore() == SC_UNAVAILABLE && !writes);
    }
    reset(); snapshot.rsp_state = 1;
    assert(restore() == SC_UNAVAILABLE && !writes);
    reset(); snapshot.rdp_state = 3;
    assert(restore() == SC_UNAVAILABLE && !writes);
    reset(); snapshot.task_completions = 1;
    assert(restore() == SC_PENDING && !writes);
    reset(); snapshot.waiting = 0;
    assert(restore() == SC_PENDING && !writes);
    reset(); snapshot.requests = 1;
    assert(restore() == SC_PENDING && !writes);
    reset(); snapshot.completions = 1;
    assert(restore() == SC_PENDING && !writes);
    reset(); validation_status = SC_INVALID;
    assert(restore() == SC_INVALID && !writes && validations == 1);
    reset(); validation_status = SC_PENDING;
    assert(restore() == SC_PENDING && !writes);
    reset(); bad_write = 1;
    assert(restore() == SC_UNAVAILABLE && writes == 1);
    reset(); fail_read = 1;
    assert(restore() == SC_UNAVAILABLE && writes == 1);
    reset(); snapshot.events = 1;
    assert(restore() == SC_OK && writes == 1);
    reset(); sc_crash_active = 1;
    assert(restore() == SC_UNAVAILABLE && !writes && !reads);
    assert(gCrashScreen.frameBuf == (u16 *)0xa0400000U && sc_crash_active);
    reset();
    gCrashScreen.frameBuf = fault_pixels;
    gCrashScreen.width = 320;
    fault_pixels[0] = 0;
    if (!setjmp(draw_jump)) crash_screen_thread_entry(NULL);
    assert(receives == 2 && stops == 1 && waits == 1 && draws == 1 && sc_crash_active);
    assert(captured_pixels == fault_pixels);
    assert(restore() == SC_UNAVAILABLE && !writes && !reads);
    assert(captured_pixels == gCrashScreen.frameBuf && sc_crash_active);
    puts("NATIVE CRASH HOST PASSED: real restore logic, simulated token/snapshot/IRQ; not native authority");
    return 0;
}
