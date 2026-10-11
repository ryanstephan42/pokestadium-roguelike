#include "story_native_stubs.h"
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#define _6BC0_H_
typedef struct Gfx { u32 words[2]; } Gfx;
#define IMAGE_SIZE_BITS_4b 0
#define IMAGE_SIZE_BITS_8b 1
#define IMAGE_SIZE_BITS_16b 2
#define IMAGE_SIZE_BITS_32b 3
#define MEMORY_POOL_LEFT 0
#define MEMORY_POOL_RIGHT 1
#define ALIGN64(n) (((uintptr_t)(n) + 63) & ~(uintptr_t)63)
#define gSPClearGeometryMode(p, ...) ((void)(p))
#define gSPPerspNormalize(p, ...) ((void)(p))
#define gSPViewport(p, ...) ((void)(p))
#define gSPMatrix(p, ...) ((void)(p))
extern Gfx *gDisplayListHead;
extern unk_D_800A7440 D_800A7440;
static Mtx D_8006F010;
static unk_func_80007444 *current_screen;
static unk_D_80068BB0 *D_80068BB0;
static MainPool pool;
static MainPoolState scopes[4];
static unsigned depth, allocations, fail_at, used, marks[4], tasks;
static int fail_scope;
static struct { void *p; AllocateFunc callback; } blocks[16];

unk_D_80068BB0 *func_80006314(s32, s32, s32, s32, s32);
void func_80005EDC(void);
void func_80006414(unk_D_80068BB0 *a, unk_D_80068BB0 *b) { a->depth_p = b; }
u16 func_80001B2C(void) { return 0; }
unk_func_80007444 *func_800075F8(void) { return current_screen; }
#define D_800A7464 current_screen
#include "../build/story/native-display-review-v1/context-restore-body.inc"
unk_D_80068BB0 *func_8000648C(void) { return D_80068BB0; }
void guOrtho(Mtx *m, float a, float b, float c, float d, float e, float f, float g) {
    (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g; memset(m, 0, sizeof(*m));
}
void func_8001E6E8(Vp *v, int x, int y) { (void)x; (void)y; memset(v, 0, sizeof(*v)); }
void Cont_StartReadInputs(void) {}
void Cont_ReadInputs(void) {}
void func_8001F730(void) {}
void func_80006C6C(s32 n) { current_screen->unk_11 = (u8)n; }
void func_800079C4(void) { D_80068BB0 = current_screen->unk_18[0]; D_800A7440.x1 = 99; }
void func_80007678(unk_func_80007444 *screen) { current_screen = screen; func_80005EDC(); }
void func_800076C0(void) { tasks++; current_screen = NULL; }
s32 func_80007778(void) { tasks++; func_80005EDC(); gDisplayListHead++; return 0; }
void func_8000771C(void) { tasks = 0; }
s32 HAL_Printf(s16 x, s16 y, const char *fmt, ...) { (void)x; (void)y; (void)fmt; return 0; }
MainPool *main_pool_get_pool(void) { return &pool; }
u32 main_pool_push_state(u32 tag) {
    (void)tag;
    if (fail_scope) return 0;
    assert(depth < 4);
    scopes[depth].prev = pool.mainState; marks[depth] = used;
    pool.mainState = &scopes[depth++]; return 1;
}
u32 main_pool_pop_state(u32 tag) {
    (void)tag; assert(depth && !tasks);
    while (used > marks[depth - 1]) {
        --used;
        if (blocks[used].callback) blocks[used].callback(blocks[used].p, 0);
        free(blocks[used].p);
    }
    pool.mainState = scopes[--depth].prev; return 1;
}
void *main_pool_alloc(u32 size, s32 side) {
    void *p;
    (void)side;
    if (++allocations == fail_at) return NULL;
    assert(used < 16);
    p = malloc(size); assert(p);
    blocks[used].p = p; blocks[used++].callback = NULL; return p;
}
void *main_pool_alloc_with_func(u32 size, s32 side, s32 tag, AllocateFunc callback) {
    void *p = main_pool_alloc(size, side);
    (void)tag; if (p) blocks[used - 1].callback = callback; return p;
}

/* Exact checked acquisition, framebuffer allocator and no-implicit-framebuffer
   context body; the OS/pool and command submission remain HOST fixtures. */
#include "../build/story/native-display-review-v1/workspace-body.inc"
void func_80005EDC(void) {
    D_800A7428.unk10 ^= 1;
    gDisplayListHead = (Gfx *)(D_800A7428.unk10 ? D_800A7428.unk4 : D_800A7428.unk0);
    D_800A7428.unk8 = (u32)((uintptr_t)gDisplayListHead + D_800A7428.unkC);
}
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#include "../build/story/native-display-review-v1/framebuffer-body.inc"
#pragma GCC diagnostic pop
#include "../build/story/native-display-review-v1/context-body.inc"
#include "../build/story/native-display-review-v1/image-restore-body.inc"
#include "../src/story/sc_native_display.c"

int main(void) {
    ScNativeDisplay display;
    unk_func_80007444 borrowed, child, saved;
    unk_D_80068BB0 image;
    UnkStruct800A7428 workspace;
    Gfx borrowed_commands[8], child_commands[8], *retained;
    unsigned n;
    memset(&borrowed, 0, sizeof(borrowed)); memset(&child, 0, sizeof(child));
    memset(&image, 0, sizeof(image)); memset(&workspace, 0, sizeof(workspace));
    D_800A7428 = workspace; gDisplayListHead = borrowed_commands;
    current_screen = &borrowed; D_80068BB0 = &image; D_800A7440.x1 = 7;
    for (n = 0; n <= 6; n++) {
        memset(&display, 0, sizeof(display)); allocations = 0; fail_at = n;
        fail_scope = n == 0;
        assert(sc_native_display_acquire(&display) == SC_UNAVAILABLE);
        assert(!depth && !used && !sStoryWorkspace && !display.acquired);
        assert(current_screen == &borrowed && D_80068BB0 == &image && D_800A7440.x1 == 7);
        assert(!memcmp(&D_800A7428, &workspace, sizeof(workspace)) && gDisplayListHead == borrowed_commands);
    }
    fail_scope = 0; fail_at = 0; allocations = 0;
    assert(sc_native_display_acquire(&display) == SC_OK && allocations == 6);
    assert(gDisplayListHead == borrowed_commands && current_screen == &borrowed);
    assert(sc_native_display_begin(&display) == SC_OK);
    assert(sc_native_display_release(&display) == SC_UNAVAILABLE);
    assert(sc_native_display_end(&display) == SC_OK);
    assert(gDisplayListHead == borrowed_commands && D_80068BB0 == &image && D_800A7440.x1 == 7);
    retained = sStoryWorkspace->owned_head;
    main_pool_push_state(0);
    current_screen = &child; child.unk_11 = 42; saved = child;
    gDisplayListHead = child_commands;
    assert(sc_native_display_begin(&display) == SC_OK && gDisplayListHead > retained);
    assert(sc_native_display_end(&display) == SC_OK);
    assert(current_screen == &child && !memcmp(&child, &saved, sizeof(child)));
    assert(gDisplayListHead == child_commands && D_80068BB0 == &image && D_800A7440.x1 == 7);
    assert(sc_native_display_release(&display) == SC_UNAVAILABLE);
    main_pool_pop_state(0); current_screen = &borrowed; gDisplayListHead = borrowed_commands;
    assert(sc_native_display_release(&display) == SC_OK && !tasks && !used && !depth);
    assert(current_screen == &borrowed && D_80068BB0 == &image && D_800A7440.x1 == 7);
    assert(!memcmp(&D_800A7428, &workspace, sizeof(workspace)) && gDisplayListHead == borrowed_commands);
    assert(sc_native_display_release(&display) == SC_UNAVAILABLE);
    puts("Checked display: actual acquisition + 7 finite scope/allocation failures, borrowed state/child restoration; HOST only; OK");
    return 0;
}
