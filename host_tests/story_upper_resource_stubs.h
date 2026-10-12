#ifndef STORY_UPPER_RESOURCE_STUBS_H
#define STORY_UPPER_RESOURCE_STUBS_H
#define _COMMON_H
#define _3FB0_H_
#define _1CF30_H_
#define _MEMORY_H_
#define _STAGE_LOADER_H_
#include <stddef.h>
#include <string.h>
typedef unsigned char u8;
typedef signed char s8;
typedef unsigned short u16;
typedef short s16;
typedef unsigned int u32;
typedef int s32;
typedef void *OSMesg;
typedef struct OSMesgQueue { int unused; } OSMesgQueue;
typedef struct MainPoolBlock { int unused; } MainPoolBlock;
typedef void (*AllocateFunc)(MainPoolBlock *, u32);
typedef struct BinArchive {
    u16 unk_00, unk_02;
    u32 unk_04, total_size, num_files;
} BinArchive;
typedef struct BinArchiveFile { u32 offset, size, unk_08, unused; } BinArchiveFile;
typedef struct PERSSZP {
    char magic[8];
    u32 header_size, decompressed_size1, decompressed_size2, unk_14;
} PERSSZP;
typedef struct Yay0 {
    char magic[4];
    u32 decompressed_size, link_table_offset, byte_chunk_offset;
} Yay0;
typedef struct ret_func_8000484C { u8 bytes[0x90]; s32 unk_90; } ret_func_8000484C;
typedef struct unk_D_800AC870_sub { s16 unk_00, unk_02; u8 *unk_04, *unk_08; } unk_D_800AC870_sub;
typedef struct unk_D_800AC870 {
    unk_D_800AC870_sub unk_00[6];
    BinArchive *unk_48;
    s32 unk_4C;
    u8 unk_50, unk_51, unk_52, unk_53;
} unk_D_800AC870;
extern unk_D_800AC870 *D_800AC870;
extern u8 fonts_ROM_START[], fonts_ROM_END[], textdata_ROM_START[], textdata_ROM_END[];
#define bzero(p, n) memset(p, 0, n)
#define ASSET_LOAD2(asset, mode, side) func_800044F4(asset##_ROM_START, asset##_ROM_END, mode, side)
void *main_pool_alloc_with_func(u32, s32, s32, AllocateFunc);
void main_pool_set_func(void *, s32, AllocateFunc);
BinArchive *func_800044F4(u8 *, u8 *, s32, s32);
void *func_8000484C(BinArchive *, s32);
int osRecvMesg(OSMesgQueue *, OSMesg *, int);
void func_80003890(u8 *, u8 *);
void func_8001E7C0(s32, s32);
void func_8001E7D4(s32, s32);
s32 HAL_Printf(s16, s16, const char *, ...);
#endif
