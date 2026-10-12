#ifdef STORY_NATIVE_UI
#include "sc_native_crash.h"
#include "src/crash_screen.h"
#ifndef SC_NATIVE_CRASH_HOST
#include <PR/os_internal_reg.h>
#endif

#define SC_CRASH_FRAME ((u16 *)0xa07da800U)

static int idle(const ScDpSnapshot *snapshot) {
    unsigned i;
    if ((snapshot->rsp_state != 0 && snapshot->rsp_state != 3) ||
        (snapshot->rdp_state != 0 && snapshot->rdp_state != 2))
        return SC_UNAVAILABLE;
    if (snapshot->waiting != 1 || snapshot->requests || snapshot->completions ||
        snapshot->task_completions)
        return SC_PENDING;
    for (i = 0; i < 3; i++) {
        if (snapshot->frames[i].unk_01 > 1 || snapshot->frames[i].unk_02 > 1)
            return SC_UNAVAILABLE;
        if (snapshot->frames[i].unk_04 || snapshot->frames[i].unk_08 ||
            snapshot->frames[i].unk_0C)
            return SC_PENDING;
    }
    return SC_OK;
}

int sc_native_crash_restore(ScNativeCrash *control, const ScDpToken *token,
                            void *context,
                            int (*validate)(void *, const ScDpSnapshot *)) {
    u32 interrupts;
    int status;
    if (!control || !token || !validate) return SC_INVALID;
    if (osMemSize != 0x800000U) return SC_UNAVAILABLE;
    interrupts = __osDisableInt();
    status = sc_crash_active ? SC_UNAVAILABLE : sc_dp_quiescent(token);
    if (status == SC_OK) status = sc_dp_snapshot(token, &control->snapshot);
    if (status == SC_OK) status = idle(&control->snapshot);
    if (status == SC_OK) status = validate(context, &control->snapshot);
    if (status == SC_OK) {
        /* The 320x240 reserved tail is excluded from every shared-arena lease. */
        crash_screen_set_draw_info(SC_CRASH_FRAME, 320, 16);
        status = sc_dp_snapshot(token, &control->snapshot);
        if (status == SC_OK &&
            (control->snapshot.crash_frame != (void *)SC_CRASH_FRAME ||
             gCrashScreen.frameBuf != SC_CRASH_FRAME ||
             gCrashScreen.width != 320 || gCrashScreen.height != 16))
            status = SC_UNAVAILABLE;
    }
    __osRestoreInt(interrupts);
    return status;
}
#endif
