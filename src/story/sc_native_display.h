#ifndef SC_NATIVE_DISPLAY_H
#define SC_NATIVE_DISPLAY_H
#ifdef STORY_NATIVE_UI
#include "global.h"
#include "src/memory.h"
#include "src/stage_loader.h"
typedef struct ScNativeDisplay {
    MainPoolState *scope;
    unk_func_80007444 *screen, *previous, *frame_previous;
    unk_D_80068BB0 *previous_image, *frame_image;
    unk_D_800A7440 previous_rect, frame_rect;
    Vp viewport;
    Mtx projection;
    int acquired, active, frame;
} ScNativeDisplay;
int sc_native_display_acquire(ScNativeDisplay *display);
int sc_native_display_begin(ScNativeDisplay *display);
int sc_native_display_end(ScNativeDisplay *display);
int sc_native_display_release(ScNativeDisplay *display);
#endif
#endif
