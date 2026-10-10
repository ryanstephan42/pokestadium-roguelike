#ifndef SCENE_NODES_STUBS_H
#define SCENE_NODES_STUBS_H

/* Bypass only native platform headers; the scene and UI model compile unchanged. */
#define _COMMON_H
#define _2E460_H_
#define _232C0_H_
#include <stddef.h>
typedef unsigned char u8;
typedef unsigned short u16;
typedef short s16;
typedef int s32;
typedef struct Color_RGBA8 { u8 r, g, b, a; } Color_RGBA8;
typedef struct unk_D_80072B00 { u8 unk_05; } unk_D_80072B00;
typedef struct SceneSpeciesFlags { u8 bytes[23]; } SceneSpeciesFlags;
extern SceneSpeciesFlags D_80070FA0[151];
unk_D_80072B00* func_8002ED40(s32 move);
u8 func_80022A28(s32 index);

#endif
