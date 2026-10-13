#include "334D0.h"
#include "src/12D80.h"
#include "src/30640.h"
#include "src/32D10.h"
#include "src/33FE0.h"
#include "src/6A40.h"
#include "src/F420.h"

typedef struct StadiumModelSlot {
    /* 0x000 */ StadiumModel model;
    /* 0x204 */ char pad204[0x5CC];
} StadiumModelSlot; // size = 0x7D0

typedef struct unk_func_80032F94_a0 {
    /* 0x00 */ u8 pad00[0x18];
    /* 0x18 */ s32 unk_18;
    /* 0x1C */ s32 unk_1C;
} unk_func_80032F94_a0;  // size = 0x20

typedef struct unk_D_800B2EA8 {
    u8 unk_00[0xA8];
} unk_D_800B2EA8; // size = 0xA8

typedef struct {
    MtxF mtx[16];   // 16 (0x10) × 0x40 = 0x400
} MtxBuffer; // size = 0x400

extern s32 D_80076270[16];
extern Gfx D_800762B0[6];
extern Gfx D_800762C8[];
extern u32 D_800777A8[];
extern u32 D_80077950[];

// extern Gfx D_800762C8[] = {
//     gsSPSetGeometryMode(0x00FDFFFF),
//     gsDPPipeSync(),
//     gsDPSetCombineLERP(
//         TEXEL0, 0, SHADE, 0,      // Color cycle 1
//         TEXEL0, 0, SHADE, 0,      // Color cycle 2
//         0, 0, 0, SHADE,           // Alpha cycle 1
//         0, 0, 0, SHADE            // Alpha cycle 2
//     ),
//     gsDPSetAlphaCompare(G_AC_NONE),
//     gsSPEndDisplayList(),
// };

s16 D_800AF7C0[4];
static MtxBuffer D_800AF7C8[2];
static StadiumModelSlot D_800AFFC8[2][3];
static unk_D_800B2EA8 D_800B2EA8[1];

#ifdef ROGUELIKE
/*
 * D_800AF7B0[row] is an s16 slot counter that increments on every func_80032F94 call and is
 * only reset on the func_80032670 draw path, so nothing bounds it to the three slots of
 * D_800AFFC8[row]. A slot is populated only by func_80033B2C, which sets modelSegment to the
 * slot's own unkSegment. Refuse out-of-range counters, and unpopulated slots for consumers.
 * An out-of-range counter latches the row dead until the func_80032670 reset (rl_shadow_slot_reset),
 * so the s16 counter wrapping back to 0..2 cannot re-admit a slot from before the refusal.
 */
static u8 rl_shadow_dead[2];

void rl_shadow_slot_reset(u32 row) {
    rl_shadow_dead[row & 1] = 0;
}

static StadiumModel* rl_shadow_slot(s32 row, s16 counter, s32 requirePopulated) {
    StadiumModel* model;

    if ((u32)row >= 2) {
        return NULL;
    }
    if ((u16)counter >= 3) {
        rl_shadow_dead[row] = 1;
        return NULL;
    }
    if (rl_shadow_dead[row]) {
        return NULL;
    }
    model = &D_800AFFC8[row][counter].model;
    if (requirePopulated && (model->modelSegment != (u32)&model->unkSegment)) {
        return NULL;
    }
    return model;
}
#endif

void func_800328D0(s32 arg0) {

}

s32 func_800328D8(UNUSED s32 arg0) {
    return 1;
}

void func_800328E4(u8 arg0) {
    func_8000ED4C(D_800AF7C8[arg0].mtx, func_800123D4(0));
    func_8000ED4C(D_800AF7C8[arg0].mtx[1].mf, func_800123D4(-1));
}

void func_80032940(u8 index) {
    MtxF* temp_a1;
    MtxF sp1C;

    temp_a1 = &D_800AF7C8[index];
    func_80031EF4(&sp1C, &D_8006F088->unk_60.mtxf);
    guMtxCatF(sp1C.mf, temp_a1->mf, temp_a1->mf);
}

void func_80032990(u8 index) {
    MtxF* temp_a0;
    
    s32 pad[2];
    f32 spC0;
    f32 spBC;
    f32 spB8;
    
    MtxF sp78;
    MtxF sp38;
    Vec3f sp2C;
    
    temp_a0 = &D_800AF7C8[index].mtx;
    
    func_80032034(temp_a0, &sp2C);
    guScaleF(sp38.mf, sp2C.x, sp2C.y, sp2C.z);
    guMtxXFMF(temp_a0->mf, 0.0f, 0.0f, 0.0f, &spC0, &spBC, &spB8);
    guTranslateF(sp78.mf, spC0, spBC, spB8);
    guMtxCatF(sp38.mf, sp78.mf, temp_a0->mf);
}

void func_80032A34(s32 arg0) {
    s32 i;
    StadiumModelSlot* slot = &D_800AFFC8[arg0];

    for (i = 0; i < 3; i++) {
        slot[i].model.unk_00 = 0;
    }
}

Gfx* func_80032A7C(Gfx* gfx) {
    u8 temp_v0 = D_8006F09C->unk_01D;

    switch (D_8006F09C->unk_01A) {
        case 0x92:
            gSPDisplayList(gfx++, D_800762C8);
            gDPSetPrimColor(gfx++, 0, temp_v0, 255, 255, 255, 200);
            gDPSetEnvColor(gfx++, 255, 32, 0, 0);
            break;
        case 0x7E:
            gSPDisplayList(gfx++, D_800762C8);
            gDPSetPrimColor(gfx++, 0, temp_v0, 255, 255, 5, 255);
            gDPSetEnvColor(gfx++, 255, 32, 0, 0);
            break;
        default:
            gSPDisplayList(gfx++, D_800762C8);
            gDPSetPrimColor(gfx++, 0, temp_v0, 255, 255, 255, 255);
            gDPSetEnvColor(gfx++, 180 - (D_8006F084 % 8) * 10, 32, 0, 0);
            break;
    }

    return gfx;
}

void func_80032BD4(void) {
    u8 index;
    s32 mode;

    mode = func_800325B4();
    index = (u8)mode;
    if (mode < 2) {
        func_800328E4(index);
        func_80032990(index);
        func_80032940(index);

#ifdef ROGUELIKE
        if (rl_shadow_slot(index, D_800AF7B0[index & 1], 0) == NULL) {
            return;
        }
#endif
        if (D_8006F09C->unk_01A == 0x92) {
            func_80033B2C(&D_800AFFC8[index][D_800AF7B0[index & 1]].model, D_800B2EA8, D_800AF7C8[index].mtx, &D_80077950);
            return;
        }
        func_80033B2C(&D_800AFFC8[index][D_800AF7B0[index & 1]].model, D_800B2EA8, D_800AF7C8[index].mtx, &D_800777A8);
    }
}

void func_80032D28(void) {
    u8 index;
    s32 mode;

    mode = func_800325B4();
    index = (u8)mode;
    if ((mode < 2) && (func_800325AC() == 0) && (D_800AF7C0[index] >= 2)) {
        func_800328E4(index);
        func_80032990(index);
        func_80032940(index);
#ifdef ROGUELIKE
        if (rl_shadow_slot(index, D_800AF7B0[index & 1], 1) == NULL) {
            return;
        }
#endif
        func_800361C4(&D_800AFFC8[index][D_800AF7B0[index & 1]].model, &D_800AF7C8[index].mtx);
    }
}

Gfx* func_80032E00(Gfx* gfx) {
    ModelSegment* segment;
    s32 pad[1];
    MtxF modelMtx;     // sp1E8
    MtxF tempCatMtx;   // sp1A8
    MtxF sp168;
    MtxF scaleMtx;     // sp128
    MtxF translateMtx; // spE8
    Mtx tempMtx;       // spA8
    Mtx* mtxL;
    u32 pad2[16];
    Vec3f scale;
    Vec3f translate;

    if (D_8006F09C->unk_01A == 0x92) {
        segment = (ModelSegment *)&D_80077950;
    } else {
        segment = (ModelSegment *)&D_800777A8;
    }
    func_80031EF4(&modelMtx, &D_8006F088->unk_60.mtxf);
    guMtxF2L(&modelMtx.mf[0], &tempMtx);
    func_8000ED4C(&tempCatMtx, func_800123D4(0));
    func_80032034(&tempCatMtx, &scale);
    guScaleF(&scaleMtx.mf[0], scale.x, scale.y, scale.z);
    guMtxXFMF(&tempCatMtx.mf[0], 0.0f, 0.0f, 0.0f, &translate.z, &translate.y, &translate.x);
    guTranslateF(&translateMtx.mf[0], translate.z, translate.y, translate.x);
    mtxL = func_80005F5C(0x40);
    guMtxCatF(&scaleMtx.mf[0], &translateMtx.mf[0], &tempCatMtx.mf[0]);
    guMtxF2L(&tempCatMtx.mf[0], mtxL);
    guMtxCatL(&tempMtx, mtxL, mtxL);
    {    Gfx *_gfx = (Gfx *)(gfx++);    _gfx->words.w0 = 0xDA380002;    _gfx->words.w1 = mtxL;    }
    gfx = func_80032A7C(gfx);
    {    Gfx *_gfx = (Gfx *)(gfx++);    _gfx->words.w0 = 0xDB060038;    _gfx->words.w1 = (s32) segment->vertexSegment;    }
    {    Gfx *_gfx = (Gfx *)(gfx++);    _gfx->words.w0 = 0xDE000000;    _gfx->words.w1 = (s32) segment->displayList;    }
    {    Gfx *_gfx = (Gfx *)(gfx++);    _gfx->words.w0 = 0xD8380002;    _gfx->words.w1 = 0x40;    }
    {    Gfx *_gfx = (Gfx *)(gfx++);    _gfx->words.w0 = 0xDE000000;    _gfx->words.w1 = &D_800762B0;    }
    return gfx;
}

Gfx* func_80032F94(Gfx* gfx) {
    u8 mode;
    Vtx* vtx;
    s16* var_v1;

    mode = func_800325B4();
    if ((mode == 0xFF) || (mode == 0xFE)) {
        return func_80032E00(gfx);
    }
    if (mode >= 4) {
        return gfx;
    }
    if (mode >= 2) {
        mode &= 1;
        if (D_800AF7C0[mode] >= 2) {
            func_80005F5C(0xA0);
#ifdef ROGUELIKE
            if (rl_shadow_slot(mode, D_800AF7B0[mode & 1], 1) == NULL) {
                D_800AF7B0[mode & 1] += 1;
                return gfx;
            }
#endif
            gfx = func_80032A7C(gfx);
            var_v1 = &D_800AF7B0[mode & 1];
            {
                ModelSegment* segment = (ModelSegment*)(&D_800AFFC8[mode][*var_v1].model.unkSegment);
                {    Gfx *_gfx = (Gfx *)(gfx++);    _gfx->words.w0 = 0xDB060038;    _gfx->words.w1 = (s32) segment->vertexSegment;    }
                {    Gfx *_gfx = (Gfx *)(gfx++);    _gfx->words.w0 = 0xDE000000;    _gfx->words.w1 = (s32) segment->displayList;    }
                {    Gfx *_gfx = (Gfx *)(gfx++);    _gfx->words.w0 = 0xDE000000;    _gfx->words.w1 = &D_800762B0;    }
            }
            *var_v1 += 1;
        }
        return gfx;
    }
    switch (D_800AF7C0[mode]) {
        default:
            var_v1 = &D_800AF7B0[mode & 1];
            break;
        case 1:
            func_80032BD4();
            var_v1 = &D_800AF7B0[mode & 1];
            break;
        case 2:
        case 3:
            vtx = func_80005F5C(0xA0);
#ifdef ROGUELIKE
            if ((vtx == NULL) || (rl_shadow_slot(mode, D_800AF7B0[mode & 1], 1) == NULL)) {
                var_v1 = &D_800AF7B0[mode & 1];
                break;
            }
#endif
            {    Gfx *_gfx = (Gfx *)(gfx++);    _gfx->words.w0 = 0xDA380002;    _gfx->words.w1 = &D_80076270;    }
            gfx = func_80032A7C(gfx);
            var_v1 = &D_800AF7B0[mode & 1];
            gfx = func_800366A4(gfx, &D_800AFFC8[mode][*var_v1].model, vtx);
            {    Gfx *_gfx = (Gfx *)(gfx++);    _gfx->words.w0 = 0xD8380002;    _gfx->words.w1 = 0x40;    }
            {    Gfx *_gfx = (Gfx *)(gfx++);    _gfx->words.w0 = 0xDE000000;    _gfx->words.w1 = &D_800762B0;    }
            break;
    }
    *var_v1 += 1;
    return gfx;
}
