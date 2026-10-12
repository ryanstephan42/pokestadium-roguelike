#ifndef STORY_NATIVE_STUBS_H
#define STORY_NATIVE_STUBS_H
#define _COMMON_H
#define _MEMORY_H_
#define _STAGE_LOADER_H_
#define _6BC0_H_
#define _1CF30_H_
#define _3FB0_H_
#define _OS_INTERNAL_REG_H_
#include <stddef.h>
#include <string.h>
#include <stdio.h>
typedef signed char s8;
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef short s16;
typedef int s32;
typedef float f32;
typedef struct OSContStatus { int unused; } OSContStatus;
typedef struct OSContPad { int unused; } OSContPad;
typedef struct OSMesgQueue { int validCount; } OSMesgQueue;
typedef void *OSMesg;
typedef struct OSPfs {
    OSMesgQueue *queue;
    int channel, status, banks, version, inode_start_page, dir_size;
    int inode_table, minode_table, dir_table;
    u8 id[32];
} OSPfs;
typedef struct MainPoolState { struct MainPoolState *prev; } MainPoolState;
typedef struct MainPool { MainPoolState *mainState; } MainPool;
typedef struct MainPoolBlock { int unused; } MainPoolBlock;
typedef void (*AllocateFunc)(MainPoolBlock *, u32);
typedef struct BinArchive { u16 num_files; unsigned kind, unk_00, alignment, unk_04, total_size; } BinArchive;
typedef struct BinArchiveFile { unsigned offset, size; void *unk_08; } BinArchiveFile;
typedef struct PERSSZP {
    char magic[8];
    u32 header_size, decompressed_size1, decompressed_size2, unk_14;
} PERSSZP;
typedef struct Yay0 { char magic[4]; u32 decompressed_size, link_table_offset, byte_chunk_offset; } Yay0;
typedef struct ret_func_8000484C { u8 bytes[0x90]; s32 unk_90; } ret_func_8000484C;
typedef struct unk_D_800AC870_sub { s16 unk_00, unk_02; u8 *unk_04, *unk_08; } unk_D_800AC870_sub;
typedef struct unk_D_800AC870 {
    unk_D_800AC870_sub unk_00[6];
    void *unk_48;
    s32 unk_4C;
    u8 unk_50, unk_51, unk_52, unk_53;
} unk_D_800AC870;
extern unk_D_800AC870 *D_800AC870;
extern u8 fonts_ROM_START[], fonts_ROM_END[], textdata_ROM_START[];
extern u8 textdata_ROM_END[];
int osRecvMesg(OSMesgQueue *, OSMesg *, int);
u32 __osDisableInt(void);
void __osRestoreInt(u32);
void func_80003890(u8 *, u8 *);
#define ASSET_LOAD2(asset, mode, side) func_800044F4(asset##_ROM_START, asset##_ROM_END, mode, side)
BinArchive *func_800044F4(u8 *, u8 *, s32, s32);
void *func_8000484C(BinArchive *, s32);
void *main_pool_alloc_with_func(u32, s32, s32, AllocateFunc);
void *main_pool_alloc(u32, s32);
void main_pool_set_func(void *, s32, AllocateFunc);
void func_8001E7C0(s32, s32);
void func_8001E7D4(s32, s32);
typedef struct Color_RGBA8 { u8 r, g, b, a; } Color_RGBA8;
typedef struct Vp { unsigned unused[4]; } Vp;
typedef struct Mtx { unsigned unused[16]; } Mtx;
typedef struct unk_D_800A7440 { s16 x1, y1, x2, y2; } unk_D_800A7440;
typedef struct unk_D_80068BB0 {
    u16 fmt, size, width, height;
    u8 *img_p;
    struct unk_D_80068BB0 *depth_p;
} unk_D_80068BB0;
typedef struct unk_func_80007444 {
    s16 unk_00, unk_02, unk_04, unk_06, unk_08, unk_0A;
    s8 unk_0C, unk_0D, unk_0E, unk_0F, unk_10;
    u8 unk_11, unk_12, unk_13;
    u16 unk_14;
    s8 unk_16;
    u8 pad17;
    unk_D_80068BB0 *unk_18[3];
} unk_func_80007444;
void func_8000771C(void);
#define PFS_ERR_NOPACK 1
#define PFS_ERR_NEW_PACK 2
#define PFS_DATA_FULL 3
#define PFS_DIR_FULL 4
#define PFS_ERR_DEVICE 5
#define PFS_ERR_INCONSISTENT 6
#define PFS_ERR_BAD_DATA 7
#define PFS_INITIALIZED 1
#define bzero(p, n) memset(p, 0, n)
s32 osPfsIsPlug(OSMesgQueue *, u8 *);
s32 HAL_Printf(s16, s16, const char *, ...);
MainPool *main_pool_get_pool(void);
u32 main_pool_push_state(u32);
u32 main_pool_pop_state(u32);
#endif
