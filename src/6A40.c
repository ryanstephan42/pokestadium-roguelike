#include "global.h"
#include "6A40.h"
#ifdef STORY_NATIVE_UI
#include "src/memory.h"
#endif

typedef struct UnkStruct800A7428 {
    u32* unk0;
    u32* unk4;
    u32 unk8;
    s32 unkC;
    s16 unk10;
} UnkStruct800A7428; // size = 0x12

Gfx* gDisplayListHead;
UnkStruct800A7428 D_800A7428;
unk_D_800A7440 D_800A7440;

void func_80005EDC(void);

#ifdef STORY_NATIVE_UI
typedef struct ScWorkspaceRestore {
    UnkStruct800A7428 workspace;
    UnkStruct800A7428 owned, suspended;
    Gfx *head;
    Gfx *owned_head;
    Gfx *suspended_head;
    int active;
} ScWorkspaceRestore;
static ScWorkspaceRestore *sStoryWorkspace;

static void sc_workspace_restore(MainPoolBlock *block, u32 arg) {
    ScWorkspaceRestore *restore = (ScWorkspaceRestore *)block;
    (void)arg;
    D_800A7428 = restore->workspace;
    gDisplayListHead = restore->head;
    sStoryWorkspace = NULL;
}

s32 func_80005E40Checked(u32 bytes) {
    ScWorkspaceRestore *restore;
    u32 *first, *second;
    if (!bytes || (bytes & 15) || sStoryWorkspace) return 0;
    restore = main_pool_alloc_with_func(sizeof(*restore), 0, 0, sc_workspace_restore);
    if (!restore) return 0;
    restore->workspace = D_800A7428;
    restore->head = gDisplayListHead;
    restore->active = 0;
    first = main_pool_alloc(bytes, 0);
    if (!first) return 0;
    second = main_pool_alloc(bytes, 0);
    if (!second) return 0;
    D_800A7428.unk0 = first; D_800A7428.unk4 = second;
    D_800A7428.unk10 = 0; D_800A7428.unkC = bytes;
    func_80005EDC();
    restore->owned = D_800A7428;
    restore->owned_head = gDisplayListHead;
    D_800A7428 = restore->workspace;
    gDisplayListHead = restore->head;
    sStoryWorkspace = restore;
    return 1;
}

s32 func_80005E40ActivateChecked(void) {
    if (!sStoryWorkspace || sStoryWorkspace->active) return 0;
    sStoryWorkspace->suspended = D_800A7428;
    sStoryWorkspace->suspended_head = gDisplayListHead;
    D_800A7428 = sStoryWorkspace->owned;
    gDisplayListHead = sStoryWorkspace->owned_head;
    sStoryWorkspace->active = 1;
    return 1;
}

s32 func_80005E40SuspendChecked(void) {
    if (!sStoryWorkspace || !sStoryWorkspace->active) return 0;
    sStoryWorkspace->owned = D_800A7428;
    sStoryWorkspace->owned_head = gDisplayListHead;
    D_800A7428 = sStoryWorkspace->suspended;
    gDisplayListHead = sStoryWorkspace->suspended_head;
    sStoryWorkspace->active = 0;
    return 1;
}
#endif

void func_80005E40(u32 arg0, s32 arg1) {
    D_800A7428.unk10 = 0;
    D_800A7428.unkC = arg0;
    D_800A7428.unk0 = main_pool_alloc_with_func(arg0, arg1, 'DYN0', NULL);
    D_800A7428.unk4 = main_pool_alloc_with_func(arg0, arg1, 'DYN1', NULL);
    func_80005EDC();
}

void func_80005EAC(void) {
    main_pool_try_free(D_800A7428.unk4);
    main_pool_try_free(D_800A7428.unk0);
}

void func_80005EDC(void) {
    s32* temp_v0 = (s32*)&D_800A7428;

    D_800A7428.unk10 ^= 1;
    temp_v0 += D_800A7428.unk10;

    D_800A7428.unk8 = *temp_v0 + D_800A7428.unkC;
    gDisplayListHead = *temp_v0;
}

void func_80005F1C(Gfx** arg0, Gfx** arg1) {
    s32* ptr = (s32*)&D_800A7428;
    *arg1 = (Gfx*)((uintptr_t)gDisplayListHead - ptr[D_800A7428.unk10]);
    *arg0 = (Gfx*)(ptr[D_800A7428.unk10]);
}

void* func_80005F5C(s32 arg0) {
    u32 temp_v0;
    u32 ret;

    arg0 = ALIGN8(arg0);
    temp_v0 = (D_800A7428.unk8 - arg0);
    ret = NULL;
    if (temp_v0 >= (uintptr_t)gDisplayListHead) {
        D_800A7428.unk8 = temp_v0;
        ret = temp_v0;
    }
    return ret;
}

s32 func_80005F9C(void) {
    return D_800A7428.unk8 - (uintptr_t)gDisplayListHead;
}
