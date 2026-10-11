#ifdef STORY_NATIVE_UI
#include "sc_native_display.h"
#include "src/1CF30.h"
#include "src/6A40.h"
#include "src/6BC0.h"
#include "src/controller.h"
#include "sc_core.h"

static ScNativeDisplay *sDisplay;
static int error(const char *message) { HAL_Printf(8, 8, "%s", message); return SC_UNAVAILABLE; }

int sc_native_display_acquire(ScNativeDisplay *display) {
    MainPoolState *before;
    unsigned i;
    if (!display || sDisplay || display->acquired) return error("Story display: acquisition identity unavailable");
    before = main_pool_get_pool()->mainState;
    main_pool_push_state(0);
    display->scope = main_pool_get_pool()->mainState;
    if (display->scope == before) { display->scope = NULL; return error("Story display: scope allocation failed"); }
    display->previous = func_800075F8();
    display->previous_image = func_8000648C();
    display->previous_rect = D_800A7440;
    if (!func_80005E40Checked(0x10000)) goto failed;
    display->screen = func_80007444(1, 0, 2, 0, 2, 0);
    if (!display->screen) goto failed;
    for (i = 0; i < 2; i++) {
        display->screen->unk_18[i] = func_80006314(0, IMAGE_SIZE_BITS_16b, 640, 480, MEMORY_POOL_RIGHT);
        if (!display->screen->unk_18[i]) goto failed;
    }
    guOrtho(&display->projection, 0.0f, 640.0f, 480.0f, 0.0f, -2.0f, 2.0f, 1.0f);
    func_8001E6E8(&display->viewport, 640, 480);
    display->acquired = 1; display->active = display->frame = 0;
    sDisplay = display;
    return SC_OK;
failed:
    main_pool_pop_state(0);
    display->scope = NULL; display->screen = display->previous = NULL;
    return error("Story display: workspace/framebuffer allocation failed");
}

int sc_native_display_begin(ScNativeDisplay *display) {
    MainPoolState *scope;
    if (!display || sDisplay != display || !display->acquired || display->frame)
        return error("Story display: frame identity invalid");
    for (scope = main_pool_get_pool()->mainState; scope && scope != display->scope; scope = scope->prev) {}
    if (!scope) return error("Story display: retained scope unavailable");
    if (!func_80005E40ActivateChecked()) return error("Story display: workspace activation failed");
    display->frame_previous = func_800075F8();
    display->frame_image = func_8000648C();
    display->frame_rect = D_800A7440;
    if (!display->active) {
        func_80007678(display->screen);
        func_80006C6C(0xA);
        display->active = 1;
    } else func_800075F8SetStory(display->screen);
    Cont_StartReadInputs(); Cont_ReadInputs(); func_8001F730();
    func_800079C4();
    gSPClearGeometryMode(gDisplayListHead++, G_ZBUFFER | G_LIGHTING);
    gSPPerspNormalize(gDisplayListHead++, 0xFFFF);
    gSPViewport(gDisplayListHead++, (u32)&display->viewport & 0x1FFFFFFF);
    gSPMatrix(gDisplayListHead++, (u32)&display->projection & 0x1FFFFFFF,
              G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION);
    gSPMatrix(gDisplayListHead++, (u32)&D_8006F010 & 0x1FFFFFFF,
              G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    display->frame = 1;
    return SC_OK;
}

int sc_native_display_end(ScNativeDisplay *display) {
    if (!display || sDisplay != display || !display->frame) return error("Story display: no open frame");
    func_80007778();
    func_8000771C();
    func_800075F8SetStory(display->frame_previous);
    display->frame_previous = NULL;
    if (!func_80005E40SuspendChecked()) return error("Story display: workspace suspension failed");
    func_8000648CRestoreStory(display->frame_image, &display->frame_rect);
    display->frame_image = NULL;
    display->frame = 0;
    return SC_OK;
}

int sc_native_display_release(ScNativeDisplay *display) {
    if (!display || sDisplay != display || !display->acquired || display->frame ||
        main_pool_get_pool()->mainState != display->scope)
        return error("Story display: retirement order invalid");
    if (display->active) {
        if (!func_80005E40ActivateChecked()) return error("Story display: retirement workspace unavailable");
        func_8000771C();
        func_800075F8SetStory(display->screen);
        func_800076C0();
        if (display->previous) func_80007678(display->previous);
        func_8000771C();
        if (!func_80005E40SuspendChecked()) return error("Story display: retirement workspace restoration failed");
    }
    func_800075F8SetStory(display->previous);
    func_8000648CRestoreStory(display->previous_image, &display->previous_rect);
    main_pool_pop_state(0);
    display->scope = NULL; display->screen = display->previous = NULL;
    display->previous_image = NULL;
    display->acquired = display->active = 0; sDisplay = NULL;
    return SC_OK;
}
#endif
