#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "src/story/sc_native_resources.h"
#include "src/story/sc_native_icons.h"

u8 fonts_ROM_START[44304], textdata_ROM_START[4848], pokemon_icons_ROM_START[488848];
__asm__(".globl fonts_ROM_END\n.set fonts_ROM_END, fonts_ROM_START + 44304");
__asm__(".globl textdata_ROM_END\n.set textdata_ROM_END, textdata_ROM_START + 4848");
__asm__(".globl pokemon_icons_ROM_END\n.set pokemon_icons_ROM_END, pokemon_icons_ROM_START + 488848");
static u8 backing[0x100000];
static ScMaArena arena;
static unk_D_800AC870 borrowed, saved;
unk_D_800AC870 *D_800AC870;
static void *record;
static AllocateFunc cleanup;
static unsigned allocations, reads, logs;
static int fail_record;
static int fail_dma;
static unsigned receives;
OSMesgQueue D_800A62C0;

void *main_pool_alloc_with_func(u32 bytes, s32 side, s32 arg, AllocateFunc function) {
    assert(!side && !arg && !record);
    ++allocations;
    if (fail_record) return NULL;
    record = calloc(1, bytes);
    assert(record);
    cleanup = function;
    return record;
}
static void pop(void) {
    assert(record);
    cleanup(record, 0);
    free(record);
    record = NULL;
}
s32 func_80000E80(s32 kind, void *out, s32 start, s32 bytes, s32 queue, s32 priority) {
    assert(!kind && !priority && (OSMesgQueue *)queue == &D_800A62C0);
    ++reads;
    if (fail_dma) return 1;
    memcpy(out, (void *)start, bytes);
    return 0;
}
int osRecvMesg(OSMesgQueue *queue, OSMesg *result, int block) {
    assert(queue == &D_800A62C0 && block);
    ++receives;
    *result = NULL;
    return 0;
}
void func_80003890(u8 *input, u8 *out) {
    PERSSZP *header = (PERSSZP *)input;
    memset(out, header->decompressed_size2 & 255, header->decompressed_size2);
}
s32 HAL_Printf(s16 x, s16 y, const char *format, ...) {
    (void)x; (void)y; assert(format); ++logs; return 0;
}
/* Legacy lower-pool acquisition is not used by this test. */
BinArchive *func_800044F4(u8 *a, u8 *b, s32 c, s32 d) {
    (void)a; (void)b; (void)c; (void)d; assert(0); return NULL;
}
void *func_8000484C(BinArchive *a, s32 b) { (void)a; (void)b; assert(0); return NULL; }
s32 func_8002D510Checked(void) { assert(0); return 0; }
char **func_8002D5AC(s32 index) { (void)index; assert(0); return NULL; }
void main_pool_set_func(void *p, s32 n, AllocateFunc f) {
    (void)p; (void)n; (void)f; assert(0);
}
void func_8001E7C0(s32 a, s32 b) { (void)a; (void)b; assert(0); }
void func_8001E7D4(s32 a, s32 b) { (void)a; (void)b; assert(0); }

static BinArchiveFile *files(u8 *bytes) {
    return (BinArchiveFile *)(bytes + sizeof(BinArchive));
}
static void fixture(void) {
    static const unsigned offsets[3] = {3584, 12096, 27840};
    static const unsigned sizes[3] = {8512, 15744, 16464};
    static const unsigned unpacked[3] = {37264, 69744, 83664};
    unsigned i;
    memset(fonts_ROM_START, 0, sizeof(fonts_ROM_START));
    memset(textdata_ROM_START, 0, sizeof(textdata_ROM_START));
    memset(pokemon_icons_ROM_START, 0, sizeof(pokemon_icons_ROM_START));
    ((BinArchive *)fonts_ROM_START)->num_files = 6;
    ((BinArchive *)textdata_ROM_START)->num_files = 46;
    ((BinArchive *)pokemon_icons_ROM_START)->num_files = 152;
    for (i = 0; i < 3; ++i) {
        PERSSZP *header = (PERSSZP *)(fonts_ROM_START + offsets[i]);
        Yay0 *yay = (Yay0 *)((u8 *)header + 24);
        files(fonts_ROM_START)[i + 2].offset = offsets[i];
        files(fonts_ROM_START)[i + 2].size = sizes[i];
        memcpy(header->magic, "PERS-SZP", 8);
        header->header_size = 24;
        header->decompressed_size1 = header->decompressed_size2 = unpacked[i];
        memcpy(yay->magic, "Yay0", 4); yay->decompressed_size = unpacked[i];
    }
    files(textdata_ROM_START)[36].offset = 752;
    files(textdata_ROM_START)[36].size = 1872;
    files(textdata_ROM_START)[37].offset = 2624;
    files(textdata_ROM_START)[37].size = 2224;
    for (i = 0; i < 152; ++i) {
        files(pokemon_icons_ROM_START)[i].offset = 2448 + i * 3200;
        files(pokemon_icons_ROM_START)[i].size = 3200;
        memset(pokemon_icons_ROM_START + 2448 + i * 3200, i, 3200);
    }
}
int main(void) {
    BinArchive *icons = (BinArchive *)1;
    char **moves = (char **)2, **species = (char **)3;
    ScMaLease unused = {{0, 0}, 0, 0};
    unsigned i, old_reads;
    assert(sizeof(unk_D_800AC870) == 84);
    assert(sc_ma_init(&arena, 0x800000, backing, sizeof(backing)));
    fixture();
    memset(&borrowed, 0x5a, sizeof(borrowed)); saved = borrowed;
    D_800AC870 = &borrowed;
    fail_record = 1;
    assert(!sc_native_resources_acquire_upper(&arena, &icons, &moves, &species));
    assert(!sc_native_resources_outstanding());
    fail_record = 0;
    fail_dma = 1; old_reads = receives;
    assert(!sc_native_resources_acquire_upper(&arena, &icons, &moves, &species));
    assert(receives == old_reads);
    pop(); fail_dma = 0;
    assert(!sc_native_resources_outstanding() && !arena.blocks[0].live);
    assert(sc_native_resources_acquire_upper(&arena, &icons, &moves, &species));
    assert(sc_native_resources_outstanding() == 1 && allocations == 3);
    assert(D_800AC870 != &borrowed && !memcmp(&borrowed, &saved, sizeof(saved)));
    assert(arena.blocks[0].size == SC_NATIVE_RESOURCE_BYTES);
    assert(D_800AC870->unk_00[2].unk_00 == 16);
    assert(D_800AC870->unk_00[3].unk_02 == 20);
    assert(D_800AC870->unk_00[4].unk_02 == 24);
    old_reads = reads;
    for (i = 1; i <= 151; ++i) {
        u8 *pixels = (u8 *)files((u8 *)icons)[i - 1].unk_08;
        assert(sc_native_icon_cached(icons, i) && sc_native_icon_prefetch(icons, i));
        assert(pixels[0] == i - 1 && pixels[3199] == i - 1);
    }
    assert(!files((u8 *)icons)[151].unk_08);
    {
        u32 last = files((u8 *)icons)[150].unk_08;
        assert((u8 *)last + 3200 == backing + SC_NATIVE_RESOURCE_BYTES);
        files((u8 *)icons)[150].unk_08 = 0;
        assert(!sc_native_icon_prefetch(icons, 151));
        files((u8 *)icons)[150].unk_08 = last;
    }
    assert(reads == old_reads && allocations == 3);
    assert(!sc_native_icon_prefetch(icons, 0) && !sc_native_icon_prefetch(icons, 152));
    assert(!sc_native_resources_retire(0));
    assert(!sc_ma_shutdown(&arena));
    assert(!sc_ma_module(&arena, (ScMaHandle){0, arena.blocks[0].generation}, &unused));
    assert(sc_native_resources_retire(1));
    assert(D_800AC870 == &borrowed && icons == (BinArchive *)1);
    assert(moves == (char **)2 && species == (char **)3);
    assert(!arena.blocks[0].live);
    pop();
    assert(!sc_native_resources_outstanding());
    for (i = 0; i < 4; ++i) {
        fixture();
        if (i == 0) ((BinArchive *)fonts_ROM_START)->num_files = 5;
        if (i == 1) files(pokemon_icons_ROM_START)[150].size = 3199;
        if (i == 2) ((PERSSZP *)(fonts_ROM_START + 3584))->decompressed_size2++;
        if (i == 3) files(textdata_ROM_START)[37].offset = 0xffffffffU;
        assert(!sc_native_resources_acquire_upper(&arena, &icons, &moves, &species));
        pop();
        assert(!sc_native_resources_outstanding() && !arena.blocks[0].live);
        assert(D_800AC870 == &borrowed && icons == (BinArchive *)1);
        assert(!memcmp(&borrowed, &saved, sizeof(saved)));
    }
    assert(!logs);
    assert(sc_ma_shutdown(&arena));
    assert(sc_ma_init(&arena, 0x800000, backing, sizeof(backing)));
    fixture();
    assert(sc_native_resources_acquire_upper(&arena, &icons, &moves, &species));
    pop();
    assert(logs == 1 && sc_native_resources_outstanding() == 1);
    assert(arena.blocks[0].live && arena.blocks[0].ticket);
    assert(!sc_ma_shutdown(&arena));
    assert(!sc_native_resources_acquire_upper(&arena, &icons, &moves, &species));
    puts("Upper story resources: PASS (fixtures, not native execution)");
    return 0;
}
