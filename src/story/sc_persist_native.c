#if defined(STORY_SERVICE_CANDIDATE) && defined(STORY_NATIVE_UI)
#include "global.h"
#include "sc_persist.h"
#include "sc_battle_service.h"
#include "sc_native_launch_hook.h"
#include "sc_native_resources.h"
#include "sc_runtime_probe.h"
#include "src/3FB0.h"
#include "src/crash_screen.h"
#include "src/memmap.h"
#include "src/memory.h"

#define SC_PERSIST_ID 0xe9
#define SC_PERSIST_LINK 0x8f900000U
#define SC_PERSIST_LINK_END 0x90000000U

extern u8 fragmentSC_ROM_START[], fragmentSC_relocs_ROM_END[];
extern u8 fragmentSC_HDR_TEXT_SIZE[], fragmentSC_HDR_DATA_END[];
extern u8 fragmentSC_HDR_RODATA_END[], fragmentSC_HDR_SIZE_IN_RAM[];
extern int gRlBattleActive;

int sc_persist_native_layout(ScPersistLayout *l) {
    l->rom_start = (ScMaU32)fragmentSC_ROM_START;
    l->rom_end = (ScMaU32)fragmentSC_relocs_ROM_END;
    l->text = (ScMaU32)fragmentSC_HDR_TEXT_SIZE;
    l->data_end = (ScMaU32)fragmentSC_HDR_DATA_END;
    l->rodata_end = (ScMaU32)fragmentSC_HDR_RODATA_END;
    l->ram = (ScMaU32)fragmentSC_HDR_SIZE_IN_RAM;
    return 1;
}

int sc_persist_native_unmapped(void) {
    return !Memmap_GetFragmentBaseVaddr(SC_PERSIST_ID) &&
        !Memmap_GetFragmentSize(SC_PERSIST_ID) && !gScProbe.cold_busy;
}

static int header_valid(const Fragment *f, const ScPersistLayout *l, ScMaU32 rom) {
    static const char magic[8] = {'F', 'R', 'A', 'G', 'M', 'E', 'N', 'T'};
    const u32 *table;
    u32 i;
    for (i = 0; i < 8; ++i) if (f->magic[i] != magic[i]) return 0;
    if (f->headerSize != 0x20 || f->relocOffset != l->rodata_end ||
        f->sizeInRom != rom || f->sizeInRam != l->ram) return 0;
    table = (const u32 *)((const u8 *)f + f->relocOffset);
    return table[0] <= (rom - f->relocOffset - 4) / 4;
}

int sc_persist_native_load(unsigned char *image, const ScPersistLayout *l, ScMaU32 bytes) {
    Fragment *f = (Fragment *)image;
    ScMaU32 rom = l->rom_end - l->rom_start;
    if (!sc_persist_native_unmapped()) return 0;
    bzero(image, bytes);
    osWritebackDCache(image, bytes);
    osInvalDCache(image, bytes);
    func_80003B30((u32)image, l->rom_start, l->rom_end, 0);
    osInvalDCache(image, bytes);
    if (!header_valid(f, l, rom)) return 0;
    Memmap_RelocateFragment(SC_PERSIST_ID, f);
    if (Memmap_GetFragmentBaseVaddr(SC_PERSIST_ID) != (uintptr_t)image ||
        Memmap_GetFragmentSize(SC_PERSIST_ID) != l->ram) return 0;
    /* Relocation zeroes [relocOffset, sizeInRam); the table tail beyond
       sizeInRam is RW BSS-adjacent padding and must not leak stale words. */
    bzero(image + l->ram, bytes - l->ram);
    osWritebackDCache(image, bytes);
    osInvalICache(image, bytes);
    return 1;
}

int sc_persist_native_quiescent(void) {
    if (sc_crash_active) return -1;
    return !gScNativeLaunchHook && !gRlBattleActive && !gScServiceLifecycle.active &&
        !gScServiceLifecycle.pending && !gScProbe.cold_busy &&
        !sc_native_resources_outstanding();
}

static int inside(uintptr_t v, uintptr_t lo, uintptr_t hi) { return v >= lo && v < hi; }

static int block_clear(const MainPoolBlock *b, uintptr_t lo, uintptr_t hi) {
    uintptr_t f = (uintptr_t)b->func, a = (uintptr_t)(u32)b->arg;
    return !inside(f, lo, hi) && !inside(a, lo, hi) &&
        !inside(f, SC_PERSIST_LINK, SC_PERSIST_LINK_END) &&
        !inside(a, SC_PERSIST_LINK, SC_PERSIST_LINK_END);
}

int sc_persist_native_pool_clear(const unsigned char *image, ScMaU32 bytes) {
    MainPool *pool = main_pool_get_pool();
    MainPoolBlock *b;
    uintptr_t lo = (uintptr_t)image, hi = lo + bytes;
    int clear = 1;
    osRecvMesg(&pool->queue, NULL, OS_MESG_BLOCK);
    for (b = pool->listHeadL->prev; clear && b; b = b->prev) clear = block_clear(b, lo, hi);
    for (b = pool->listHeadR; clear && b && b->next; b = b->next) clear = block_clear(b, lo, hi);
    osSendMesg(&pool->queue, NULL, OS_MESG_NOBLOCK);
    return clear;
}

int sc_persist_native_unmap(unsigned char *image, ScMaU32 bytes) {
    if (Memmap_GetFragmentBaseVaddr(SC_PERSIST_ID) != (uintptr_t)image) return 0;
    Memmap_ClearFragmentMemmap(SC_PERSIST_ID);
    osInvalICache(image, bytes);
    return 1;
}

int sc_persist_native_enter(unsigned char *image, int arg0, unsigned arg1) {
    return ((FragmentEntry)image)(arg0, (s32)arg1);
}
#endif
