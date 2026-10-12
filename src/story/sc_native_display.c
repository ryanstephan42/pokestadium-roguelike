#ifdef STORY_NATIVE_UI
#include "sc_native_display.h"
#include "src/1CF30.h"
#include "src/6A40.h"
#include "src/6BC0.h"
#include "src/controller.h"
#include "sc_core.h"

static ScNativeDisplay *sDisplay;
static int error(const char *message) { HAL_Printf(8, 8, "%s", message); return SC_UNAVAILABLE; }

static int spans_valid(const ScNativeFramebufferSpan framebuffer[2]) {
    unsigned i;
    uintptr_t start[2], end[2];
    if (!framebuffer) return 0;
    for (i = 0; i < 2; i++) {
        start[i] = (uintptr_t)framebuffer[i].pixels;
        if (framebuffer[i].bytes != SC_NATIVE_FRAMEBUFFER_BYTES ||
            (start[i] & 63) || start[i] < 0x80400000U ||
            start[i] > 0x807DA7E0U - SC_NATIVE_FRAMEBUFFER_BYTES) return 0;
        end[i] = start[i] + SC_NATIVE_FRAMEBUFFER_BYTES;
        if (end[i] <= start[i]) return 0;
    }
    return end[0] <= start[1] || end[1] <= start[0];
}

int sc_native_display_acquire(ScNativeDisplay *display, const ScNativeFramebufferSpan framebuffer[2]) {
    MainPoolState *before;
    unsigned i;
    if (!display || sDisplay || display->acquired || !spans_valid(framebuffer))
        return error("Story display: acquisition identity/spans unavailable");
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
        func_800062E4(&display->images[i], 0, IMAGE_SIZE_BITS_16b, 640, 480,
                     (u32)framebuffer[i].pixels);
        display->screen->unk_18[i] = &display->images[i];
    }
    guOrtho(&display->projection, 0.0f, 640.0f, 480.0f, 0.0f, -2.0f, 2.0f, 1.0f);
    func_8001E6E8(&display->viewport, 640, 480);
    display->acquired = 1; display->active = display->frame = display->closing = display->retired = 0;
    sDisplay = display;
    return SC_OK;
failed:
    main_pool_pop_state(0);
    display->scope = NULL; display->screen = display->previous = NULL;
    return error("Story display: workspace/framebuffer allocation failed");
}

int sc_native_display_begin(ScNativeDisplay *display) {
    MainPoolState *scope;
    if (!display || sDisplay != display || !display->acquired || display->frame || display->closing)
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

int sc_native_display_retire(ScNativeDisplay *display, const ScNativeCampaignOwner *owner,
                             const ScNativeCampaignBacking *backing) {
    MainPoolState *scope;
    unsigned i;
    int status;
    if (!display || sDisplay != display || !display->acquired || display->frame ||
        !owner || !owner->validate || !owner->retire_display || !backing)
        return error("Story display: retirement identity unavailable");
    status = owner->validate(owner->context, backing);
    if (status != SC_OK) return status;
    if (!spans_valid(backing->framebuffer)) return error("Story display: retirement spans invalid");
    for (i = 0; i < 2; i++)
        if (display->screen->unk_18[i] != &display->images[i] ||
            display->images[i].img_p != backing->framebuffer[i].pixels ||
            display->images[i].fmt != 0 || display->images[i].size != IMAGE_SIZE_BITS_16b ||
            display->images[i].width != 640 || display->images[i].height != 480 ||
            display->images[i].depth_p)
            return error("Story display: retirement descriptors changed");
    for (scope = main_pool_get_pool()->mainState; scope && scope != display->scope; scope = scope->prev) {}
    if (!scope) return error("Story display: retirement scope unavailable");
    if (!display->closing) {
        if (display->active) {
            if (!func_80005E40ActivateChecked()) return error("Story display: retirement workspace unavailable");
            func_8000771C();
            func_800075F8SetStory(display->screen);
            func_800076C0();
            func_8000771C();
            func_800075F8SetStory(display->previous);
            if (!func_80005E40SuspendChecked())
                return error("Story display: retirement workspace restoration failed");
        }
        func_800075F8SetStory(display->previous);
        func_8000648CRestoreStory(display->previous_image, &display->previous_rect);
        display->closing = 1;
    }
    status = owner->retire_display(owner->context, backing, display->images);
    display->retired = status == SC_OK;
    return status;
}

int sc_native_display_release(ScNativeDisplay *display) {
    if (!display || sDisplay != display || !display->acquired || display->frame ||
        !display->retired || main_pool_get_pool()->mainState != display->scope)
        return error("Story display: retirement order invalid");
    main_pool_pop_state(0);
    display->scope = NULL; display->screen = display->previous = NULL;
    display->previous_image = NULL;
    display->acquired = display->active = display->closing = display->retired = 0; sDisplay = NULL;
    return SC_OK;
}
#endif
