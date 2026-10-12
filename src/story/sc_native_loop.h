#ifndef SC_NATIVE_LOOP_H
#define SC_NATIVE_LOOP_H
#ifdef STORY_NATIVE_UI
#include "sc_native_display.h"
#include "sc_native_owner.h"
typedef struct ScNativeLoop {
    ScNativeCampaignOwner owner;
    ScNativeCampaignBacking backing;
    ScNativeDisplay *display;
    RlU32 held_operation, held_generation, event_operation, event_serial;
    RlU32 drained_operation;
    unsigned drained_outcome;
    int acquired, prepared, exited, queued, held_presented, frame_status;
    int display_acquired, pak_bound, resources_acquired;
} ScNativeLoop;
/* Zeroed external control/display backing. Owner acquires all large value
   backing; no snapshot/controller/request automatic allocations. */
int sc_native_loop_open(ScNativeLoop *loop, ScNativeDisplay *display,
                        const ScNativeCampaignOwner *owner);
int sc_native_loop_step(ScNativeLoop *loop);
int sc_native_loop_close(ScNativeLoop *loop);
#endif
#endif
