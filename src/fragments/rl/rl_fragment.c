/*
 * Roguelike overlay (fragmentRL, VRAM 0x8F800000). Entry point for
 * STATE_ROGUELIKE. Draws the W4 UI demo when available (RL_HAVE_UI_DEMO),
 * otherwise a native-font placeholder screen. Returns the next game state
 * (0 = let rl_main pick the return state).
 */
#include "global.h"
#include "src/1CF30.h"
#include "src/20330.h"
#include "src/6A40.h"
#include "src/controller.h"
#include "src/memory.h"
#include "src/stage_loader.h"
#ifdef RL_HAVE_UI_DEMO
#include "rl_ui.h"
#endif

s32 rl_fragment_entry(s32 arg0, s32 arg1);

static Vp sRlViewport;
static Mtx sRlProjection;

static void rl_fragment_read_input(void) {
    Cont_StartReadInputs();
    Cont_ReadInputs();
    func_8001F730();
}

/* 640x480 orthographic projection, same as fragment37's func_82202850. */
static void rl_fragment_set_ortho(void) {
    guOrtho(&sRlProjection, 0.0f, 640.0f, 480.0f, 0.0f, -2.0f, 2.0f, 1.0f);
    func_8001E6E8(&sRlViewport, 640, 480);

    gSPClearGeometryMode(gDisplayListHead++, G_ZBUFFER | G_LIGHTING);
    gSPPerspNormalize(gDisplayListHead++, 0xFFFF);
    gSPViewport(gDisplayListHead++, (u32)&sRlViewport & 0x1FFFFFFF);
    gSPMatrix(gDisplayListHead++, (u32)&sRlProjection & 0x1FFFFFFF, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION);
    gSPMatrix(gDisplayListHead++, (u32)&D_8006F010 & 0x1FFFFFFF, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
}

#ifndef RL_HAVE_UI_DEMO
static void rl_fragment_draw_fallback(void) {
    func_8001F3F4();
    func_8001EBE0(2, 0);
    func_8001F324(0xFF, 0xFF, 0xFF, 0xFF);
    func_8001F1E8(0x60, 0x50, "ROGUELIKE MODE");
    func_8001F324(0xC8, 0xC8, 0xFF, 0xFF);
    func_8001F1E8(0x60, 0x70, "PRESS B TO RETURN");
    func_8001F444();
}
#endif

s32 rl_fragment_entry(UNUSED s32 arg0, UNUSED s32 arg1) {
    unk_func_80007444* ctx;
    s32 result = 0;
    s32 running = 1;

    main_pool_push_state('RLMD');

    func_80005E40(0x10000, 0);
    ctx = func_80007444(1, 0, 2, 0, 2, 1);

#ifdef RL_HAVE_UI_DEMO
    if (rl_ui_demo_init() == -3) {
        running = 0;
    }
#else
    func_8001E94C(0x1C, 0);
#endif

    func_80007678(ctx);

    while (running) {
        rl_fragment_read_input();

#ifdef RL_HAVE_UI_DEMO
        {
            s32 r = rl_ui_demo_update(gPlayer1Controller->buttonPressed);

            if ((r == -2) || (r == -3)) {
                running = 0;
            }
        }
#else
        if (BTN_IS_PRESSED(gPlayer1Controller, BTN_B)) {
            running = 0;
        }
#endif

        func_800079C4();
        rl_fragment_set_ortho();
#ifdef RL_HAVE_UI_DEMO
        rl_ui_demo_draw();
#else
        rl_fragment_draw_fallback();
#endif
        func_80007778();
    }

    func_800076C0();
    func_80005EAC();

    main_pool_pop_state('RLMD');

    return result;
}
