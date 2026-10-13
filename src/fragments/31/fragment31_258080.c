#include "fragment31.h"
#include "src/fragments/7/fragment7.h"
#include "src/fragments/8/fragment8.h"
#include "src/fragments/50/fragment50.h"
#include "src/6A40.h"
#include "src/12D80.h"
#include "src/32D10.h"

typedef struct {
    /* 0x00 */ u8 rgb0[3];      // start color
    /* 0x03 */ u8 frameStart;   // frame where this keyframe begins
} Keyframe;  // size = 0x04

// .bss
static s32 D_81004B70;
static u8 pad_D_81004B78[0x8];
static s32 D_81004B80;
static u8 pad_D_81004B88[0x8];
static s32 D_81004B90;
static u8 pad_D_81004B98[0x8];
static s32 D_81004BA0;
static u8 pad_D_81004BA8[0x8];
static s32 D_81004BB0;
static char D_81004BB8[0x28];

#ifdef ROGUELIKE
#include "src/memmap.h"

/* Archive 920000 entry 183 (the only argument of the four CI8 callbacks): texture 8ff0f9d8 is
   followed by its 512-byte palette at 8ff32c60, so the strip may use at most these bytes. */
#define RL_CI_STRIP_BYTES 0x23288
#define RL_CI_FRAME_BYTES 0x800

static void rl_ci_fail(void) {
    osSyncPrintf("CI8 strip offset outside fragment-8/texture domain\n");
    for (;;) osStopThread(NULL);
}

/* Fail-closed replacement for the frame address: the fragment-8 distances must be read from the
   currently resident fragment 8 (exact relocation binding, first through last byte of the 16-byte
   table), the palette must sit at the strip end,
   and the whole 4-frame cycle at the selected distance must stay inside the strip. */
static u8* rl_ci_frame(u16* palette, u8* texture, s32 index, s32 counter) {
    u32 travel;

    if ((Memmap_GetLoadedFragmentVaddr((uintptr_t)&D_862082A0[0]) != 0x862082A0U) ||
        (Memmap_GetLoadedFragmentVaddr((uintptr_t)&D_862082A0[3] + 3) != 0x862082AFU) ||
        ((uintptr_t)palette - (uintptr_t)texture != RL_CI_STRIP_BYTES)) {
        rl_ci_fail();
    }
    travel = D_862082A0[index];
    if (travel > (RL_CI_STRIP_BYTES - 4 * RL_CI_FRAME_BYTES) / 4) {
        rl_ci_fail();
    }
    return texture + ((travel * 4) & ~0x1F) + ((counter & 3) << 0xB);
}
#endif

void func_81002DC0(Gfx* gfx, u16* arg1, u8* arg2) {
    gDPPipeSync(gfx++);
    gSPTexture(gfx++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
    gDPSetTextureLUT(gfx++, G_TT_RGBA16);
    gDPLoadTLUT_pal256(gfx++, arg1);
#ifdef ROGUELIKE
    gDPLoadTextureTile(gfx++, rl_ci_frame(arg1, arg2, 0, D_81004B70), G_IM_FMT_CI, G_IM_SIZ_8b, 0x20, 0x40, 0, 0, 0x20 - 1, 0x40 - 1, 0, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, 5, 6, G_TX_NOLOD, G_TX_NOLOD);
#else
    gDPLoadTextureTile(gfx++, (arg2 + ((D_862082A0[0] * 4) & ~0x1F) + ((D_81004B70 & 3) << 0xB)), G_IM_FMT_CI, G_IM_SIZ_8b, 0x20, 0x40, 0, 0, 0x20 - 1, 0x40 - 1, 0, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, 5, 6, G_TX_NOLOD, G_TX_NOLOD);
#endif
    gSPEndDisplayList(gfx++);
}

void func_81002F38(s32 arg0, DisplayListState* state) {
    Gfx* gfx;
    DisplayListAddresses* addresses;

    addresses = state->addresses;
    switch (arg0) {
    case 0:
        D_81004B70 = 0;
        return;
    case 2:
        gfx = func_80005F5C(0xA0);
        state->gfx = gfx;
        func_81002DC0(gfx, addresses->segments[0], addresses->segments[1]);
        D_81004B70 += 1;
        return;
    }
}

void func_81002FC0(Gfx* gfx, u16* arg1, u8* arg2) {
    gDPPipeSync(gfx++);
    gSPTexture(gfx++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
    gDPSetTextureLUT(gfx++, G_TT_RGBA16);
    gDPLoadTLUT_pal256(gfx++, arg1);
#ifdef ROGUELIKE
    gDPLoadTextureTile(gfx++, rl_ci_frame(arg1, arg2, 1, D_81004B80), G_IM_FMT_CI, G_IM_SIZ_8b, 0x20, 0x40, 0, 0, 0x20 - 1, 0x40 - 1, 0, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, 5, 6, G_TX_NOLOD, G_TX_NOLOD);
#else
    gDPLoadTextureTile(gfx++, (arg2 + ((D_862082A0[1] * 4) & ~0x1F) + ((D_81004B80 & 3) << 0xB)), G_IM_FMT_CI, G_IM_SIZ_8b, 0x20, 0x40, 0, 0, 0x20 - 1, 0x40 - 1, 0, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, 5, 6, G_TX_NOLOD, G_TX_NOLOD);
#endif
    gSPEndDisplayList(gfx++);
}

void func_81003138(s32 arg0, DisplayListState* state) {
    Gfx* gfx;
    DisplayListAddresses* addresses;

    addresses = state->addresses;
    switch (arg0) {                                 /* irregular */
    case 0:
        D_81004B80 = 0;
        return;
    case 2:
        gfx = func_80005F5C(0xA0);
        state->gfx = gfx;
        func_81002FC0(gfx, addresses->segments[0], addresses->segments[1]);
        D_81004B80 += 1;
        return;
    }
}

void func_810031C0(Gfx* gfx, u16* arg1, u8* arg2) {
    gDPPipeSync(gfx++);
    gSPTexture(gfx++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
    gDPSetTextureLUT(gfx++, G_TT_RGBA16);
    gDPLoadTLUT_pal256(gfx++, arg1);
#ifdef ROGUELIKE
    gDPLoadTextureTile(gfx++, rl_ci_frame(arg1, arg2, 2, D_81004B90), G_IM_FMT_CI, G_IM_SIZ_8b, 0x20, 0x40, 0, 0, 0x20 - 1, 0x40 - 1, 0, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, 5, 6, G_TX_NOLOD, G_TX_NOLOD);
#else
    gDPLoadTextureTile(gfx++, (arg2 + ((D_862082A0[2] * 4) & ~0x1F) + ((D_81004B90 & 3) << 0xB)), G_IM_FMT_CI, G_IM_SIZ_8b, 0x20, 0x40, 0, 0, 0x20 - 1, 0x40 - 1, 0, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, 5, 6, G_TX_NOLOD, G_TX_NOLOD);
#endif
    gSPEndDisplayList(gfx++);
}

void func_81003338(s32 arg0, DisplayListState* state) {
    Gfx* gfx;
    DisplayListAddresses* addresses;

    addresses = state->addresses;
    switch (arg0) {                                 /* irregular */
    case 0:
        D_81004B90 = 0;
        return;
    case 2:
        gfx = func_80005F5C(0xA0);
        state->gfx = gfx;
        func_810031C0(gfx, addresses->segments[0], addresses->segments[1]);
        D_81004B90 += 1;
        return;
    }
}

void func_810033C0(Gfx* gfx, u16* arg1, u8* arg2) {
    gDPPipeSync(gfx++);
    gSPTexture(gfx++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
    gDPSetTextureLUT(gfx++, G_TT_RGBA16);
    gDPLoadTLUT_pal256(gfx++, arg1);
#ifdef ROGUELIKE
    gDPLoadTextureTile(gfx++, rl_ci_frame(arg1, arg2, 3, D_81004BA0), G_IM_FMT_CI, G_IM_SIZ_8b, 0x20, 0x40, 0, 0, 0x20 - 1, 0x40 - 1, 0, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, 5, 6, G_TX_NOLOD, G_TX_NOLOD);
#else
    gDPLoadTextureTile(gfx++, (arg2 + ((D_862082A0[3] * 4) & ~0x1F) + ((D_81004BA0 & 3) << 0xB)), G_IM_FMT_CI, G_IM_SIZ_8b, 0x20, 0x40, 0, 0, 0x20 - 1, 0x40 - 1, 0, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, 5, 6, G_TX_NOLOD, G_TX_NOLOD);
#endif
    gSPEndDisplayList(gfx++);
}

void func_81003538(s32 arg0, DisplayListState* state) {
    Gfx* gfx;
    DisplayListAddresses* addresses;

    addresses = state->addresses;
    switch (arg0) {                                 /* irregular */
    case 0:
        D_81004BA0 = 0;
        return;
    case 2:
        gfx = func_80005F5C(0xA0);
        state->gfx = gfx;
        func_810033C0(gfx, addresses->segments[0], addresses->segments[1]);
        D_81004BA0 += 1;
        return;
    }
}

void func_810035C0(s32 arg0, DisplayListState* state) {
    static u8 D_81004190 = 0;
    s32 temp_a1;
    s32 temp_a2;
    Gfx* gfx;

    if (arg0 == 2) {
        gfx = func_80005F5C(0x10);
        temp_a1 = D_81004190 * 3;
        temp_a2 = D_81004190 * -6;
        state->gfx = gfx;
        {    Gfx *_gfx = (Gfx *)(gfx++); _gfx->words.w0 = (((temp_a1 & 0xFFF) << 0xC) | 0xF2000000 | (temp_a2 & 0xFFF));    _gfx->words.w1 = ((((0x27F << (temp_a1 + 2)) & 0xFFF) << 0xC) | ((0x7F << (temp_a2 + 2)) & 0xFFF));    }
        gSPEndDisplayList(gfx++);
        D_81004190++;
        D_81004190 &= 0x7F;
    }
}

void func_81003678(s32 arg0, DisplayListState* state) {
    static u8 D_81004194 = 0;
    static u8 D_81004198 = 0;
    static Keyframe D_8100419C[6] = {
        { { 0xFF, 0x32, 0x32 }, 0  },
        { { 0xC8, 0xC8, 0x00 }, 10 },
        { { 0x64, 0xFF, 0x00 }, 20 },
        { { 0x00, 0xC8, 0xFF }, 30 },
        { { 0x96, 0x32, 0xC8 }, 40 },
        { { 0xFF, 0x32, 0x32 }, 50 },
    };
    u8 i;
    u8 c;
    Keyframe* kf;
    Gfx* gfx;
    u8 sp48[3];
    
    if (arg0 == 2) {
        gfx = func_80005F5C(sizeof(Gfx) * 4);
        state->gfx = gfx;
        for (i = 0; i < 5; i++) {
            if (D_81004198 == D_8100419C[i].frameStart) {
                for(c = 0; c < 3; c++) {
                    sp48[c] = D_8100419C[i].rgb0[c];
                }
                break;
            }
            if (D_8100419C[i].frameStart < D_81004198 && D_81004198 < D_8100419C[i + 1].frameStart) {
                for(c = 0; c < 3; c++) {
                    sp48[c] = (s32) (((f32) ((D_8100419C[i + 1].rgb0[c] - D_8100419C[i].rgb0[c]) * (D_81004198 - D_8100419C[i].frameStart)) / (f32) (D_8100419C[i + 1].frameStart - D_8100419C[i].frameStart)) + D_8100419C[i].rgb0[c] + 0.5f);
                }
                break;
            }
        }
        gDPSetPrimColor(gfx++, 0, 0x80, sp48[0], sp48[1], sp48[2], 180);
        gDPSetTileSize(gfx++, 0, -2 * D_81004194, 0, (320 - 1) << ((-2 * D_81004194) + 2), (64 - 1) << 2);
        gDPSetTileSize(gfx++, 1, 2 * D_81004194, 0, (320 - 1) << ((2 * D_81004194) + 2), (64 - 1) << 2);
        gSPEndDisplayList(gfx++);
        D_81004194++;
        D_81004194 &= 0x7F;
        D_81004198++;
        D_81004198 &= 0xFF;
        if ((s32) D_81004198 >= 0x32) {
            D_81004198 = 0;
        }
    }
}

void func_810038D0(Gfx* gfx) {
    s32 temp_a3;

    {    Gfx *_gfx = (Gfx *)(gfx++);    _gfx->words.w0 = 0xDE000000;    _gfx->words.w1 = &D_810041D0;    }
    temp_a3 = ((s32) ((D_800AF770[5]) * 0x55) / 255) + 0xAA;
    {    Gfx *_gfx = (Gfx *)(gfx++);    _gfx->words.w0 = ((temp_a3 & 0xFF) | 0xFA000000);    _gfx->words.w1 = -0x3701;    }
    {    Gfx *_gfx = (Gfx *)(gfx++);    _gfx->words.w0 = 0xFB000000;    _gfx->words.w1 = 0x501E0AFF;    }
    {    Gfx *_gfx = (Gfx *)(gfx++);    _gfx->words.w0 = 0xFD700000;    _gfx->words.w1 = Util_ConvertAddrToVirtAddr(&D_3008000);    }
    {    Gfx *_gfx = (Gfx *)(gfx++);    _gfx->words.w0 = 0xF5700000;    _gfx->words.w1 = 0x07094260;    }
    {    Gfx *_gfx = (Gfx *)(gfx++);    _gfx->words.w0 = 0xE6000000;    _gfx->words.w1 = 0;    }
    {    Gfx *_gfx = (Gfx *)(gfx++);    _gfx->words.w0 = 0xF3000000;    _gfx->words.w1 = 0x070FF200;    }
    {    Gfx *_gfx = (Gfx *)(gfx++);    _gfx->words.w0 = 0xE7000000;    _gfx->words.w1 = 0;    }
    {    Gfx *_gfx = (Gfx *)(gfx++);    _gfx->words.w0 = 0xF5680800;    _gfx->words.w1 = 0x94260;    }
    {    Gfx *_gfx = (Gfx *)(gfx++);    _gfx->words.w0 = 0xF2000000;    _gfx->words.w1 = 0x7C03C;    }
    gSPEndDisplayList(gfx++);
    D_81004BB0 = 0;
}

s32 func_81003A24(s8* arg0) {
    s32 var_v1;
    s8 temp_t7;
    s8* var_v0;

    var_v1 = 0;
    var_v0 = arg0;
    if (*arg0 != 0) {
        do {
            var_v1 += 1;
            var_v0 += 1;
        } while (var_v0[0] != 0);
    }
    return var_v1;
}

#ifdef NON_MATCHING
void func_81003A54(Gfx* gfx) {
    u16 temp_v1;
    u8 sp4D;
    s32 pad[3];
    u8 var_v0 = D_8006F09C->unk_0A6 % 5;
    u32 temp_t0;
    char* sp34;
    Mtx* sp30;
    s32 sp2C;

    temp_t0 = D_800AF770[var_v0];
    switch (D_81004BB0) {
    case 0:
        sp4D = (((s32) (temp_t0 + 1) / 100) + 0x10) & 0xFF;
        break;
    case 1:
        sp4D = (((s32) ((s32) (temp_t0 + 1) % 100) / 10) + 0x10) & 0xFF;
        break;
    case 2:
        sp4D = (((s32) (temp_t0 + 1) % 10) + 0x10) & 0xFF;
        break;
    default:
        {
        sp34 = func_8002D7C0(0, 0, D_8267E760, (u32)(u64)(u32)(u64)(u32)(u64)(u32)temp_t0);
        sprintf(D_81004BB8, "%s     ", sp34);
        temp_v1 = D_81004BB0 - 3;
        sp4D = func_8001E730((u8) D_81004BB8[temp_v1]);
        sp2C = 0xA - func_81003A24((s8*)sp34);
        sp30 = func_80005F5C(sizeof(Mtx));
        guTranslate(sp30, (f32) sp2C * 32.0f, 0.0f, 0.0f);
        gSPMatrix(gfx++, sp30, G_MTX_NOPUSH | G_MTX_MUL | G_MTX_MODELVIEW);
        break;
        }
    }
    gDPLoadTextureTile(gfx++, Util_ConvertAddrToVirtAddr(D_3000000 + (((sp4D & 0xF) << 4)) + (((((sp4D & 0xF0) & 0xFF) >> 4) << 0xC))), G_IM_FMT_IA, G_IM_SIZ_8b, 256, 17, 0, 0, 16, 16, 0, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, 5, 5, G_TX_NOLOD, G_TX_NOLOD);
    gSPEndDisplayList(gfx++);
    D_81004BB0++;
}
#else
#pragma GLOBAL_ASM("asm/us/nonmatchings/fragments/31/fragment31_258080/func_81003A54.s")
#endif

void func_81003CF0(s32 arg0, DisplayListState* state) {
    Gfx* gfx;

    if (arg0 == 2) {
        gfx = func_80005F5C(0xF0);
        state->gfx = gfx;
        func_810038D0(gfx);
    }
}

void func_81003D2C(s32 arg0, DisplayListState* state) {
    Gfx* gfx;

    if (arg0 == 2) {
        gfx = func_80005F5C(0xA0);
        state->gfx = gfx;
        func_81003A54(gfx);
    }
}
