#include "story_native_stubs.h"
#include "src/story/sc_native_owner.h"
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
static unsigned workspace_allocations, pixel_allocations;
static int fail_scope;
static unsigned closures;
static void scheduler_submit(unk_D_80068BB0 *image);
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
void func_800076C0(void) {
    closures++; tasks++;
    scheduler_submit(current_screen->unk_18[0]); current_screen = NULL;
}
s32 func_80007778(void) {
    tasks++; scheduler_submit(current_screen->unk_18[0]);
    func_80005EDC(); gDisplayListHead++; return 0;
}
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
    if (size == 0x10000U) workspace_allocations++;
    if (size >= SC_NATIVE_FRAMEBUFFER_BYTES) pixel_allocations++;
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

typedef struct UnkArray4 {
    u8 unk_00, unk_01, unk_02, unk_03;
    uintptr_t unk_04, unk_08;
    unk_D_80068BB0 *unk_0C;
} UnkArray4;
static struct {
    struct { struct { int validCount; } queue; u16 unk_1C, unk_1E; } unk_A20;
    struct { int validCount; } unk_1C0;
    OSMesgQueue unk_9EC, unk_A04;
    int unk_A8A;
    struct UnkArray4 unk_A8C, unk_A9C, unk_AAC;
    unk_D_80068BB0 *unk_9E0;
} D_80083CA0;
static int D_80068B70;
static void *vi_current, *vi_next;
static int repeat_line;
static struct { void *frameBuf; unsigned width, height; } gCrashScreen;
#define THREAD5_END 0
void func_800049AC(void *p) { (void)p; }
void func_80001444(void *p, void *q, int n) { (void)p; (void)q; (void)n; }
void func_80004CF4(void *p) { (void)p; }
void func_80004D20(void *p) { (void)p; D_80083CA0.unk_1C0.validCount--; }
void osViSwapBuffer(void *p) { vi_next = p; }
void osViRepeatLine(int n) { repeat_line = n; }
void osViBlack(unsigned n) { (void)n; }
void func_80001474(s8 a, s8 b) { (void)a; (void)b; }
void crash_screen_set_draw_info(u16 *p, unsigned n, unsigned b) {
    gCrashScreen.frameBuf = (void *)((uintptr_t)p | 0xA0000000U);
    gCrashScreen.width = n; gCrashScreen.height = b;
}
void profiler_log_thread5_time(int n) { (void)n; }
#include "../build/story/native-display-review-v1/scheduler-body.inc"
static void scheduler_submit(unk_D_80068BB0 *image) {
    struct UnkArray4 submission;
    memset(&submission, 0, sizeof(submission));
    submission.unk_04 = 1; submission.unk_0C = image;
    func_8000152C(&submission); func_800015A8();
}
static ScNativeDisplay *expected_display;
static int validate_status, retirement_status, hold_vi;
static unsigned retirement_calls;
static int validate_display(void *context, const ScNativeCampaignBacking *backing) {
    (void)context;
    assert(backing->lease_id == 1);
    return validate_status;
}
static int leased_pixel(void *p, const ScNativeCampaignBacking *backing) {
    uintptr_t physical = (uintptr_t)p & 0x1FFFFFFFU;
    unsigned i;
    for (i = 0; i < 2; i++) {
        uintptr_t start = (uintptr_t)backing->framebuffer[i].pixels & 0x1FFFFFFFU;
        if (physical >= start && physical - start < backing->framebuffer[i].bytes) return 1;
    }
    return 0;
}
/* Fixture owner authority drives the actual tracked scheduler bodies. OS swap
   completion is simulated; this is not a native owner/VI safety acceptance. */
static int retire_fixture(void *context, const ScNativeCampaignBacking *backing,
                           const unk_D_80068BB0 *images) {
    struct UnkArray4 *pending[3];
    unsigned i;
    (void)context; assert(images == expected_display->images);
    assert(expected_display->closing && expected_display->acquired && depth && !tasks);
    retirement_calls++;
    if (retirement_status != SC_OK) return retirement_status;
    scheduler_submit(D_80083CA0.unk_9E0);
    if (!hold_vi) vi_current = vi_next;
    pending[0] = &D_80083CA0.unk_A8C; pending[1] = &D_80083CA0.unk_A9C;
    pending[2] = &D_80083CA0.unk_AAC;
    for (i = 0; i < 3; i++)
        if (pending[i]->unk_0C &&
            (pending[i]->unk_0C == &images[0] || pending[i]->unk_0C == &images[1] ||
             leased_pixel(pending[i]->unk_0C->img_p, backing))) return SC_PENDING;
    if (leased_pixel(vi_current, backing) || leased_pixel(vi_next, backing)) return SC_PENDING;
    return SC_OK;
}

int main(void) {
    ScNativeDisplay display;
    unk_func_80007444 borrowed, child, saved;
    unk_D_80068BB0 image;
    UnkStruct800A7428 workspace;
    Gfx borrowed_commands[8], child_commands[8], *retained;
    ScNativeFramebufferSpan spans[2], bad[2];
    ScNativeCampaignBacking backing;
    ScNativeCampaignOwner owner;
    unsigned n, before_allocations;
    spans[0].pixels = (RlU8 *)0x80400000U; spans[1].pixels = (RlU8 *)0x80500000U;
    spans[0].bytes = spans[1].bytes = SC_NATIVE_FRAMEBUFFER_BYTES;
    memset(&borrowed, 0, sizeof(borrowed)); memset(&child, 0, sizeof(child));
    memset(&image, 0, sizeof(image)); memset(&workspace, 0, sizeof(workspace));
    image.img_p = (u8 *)0x80010000U; D_80083CA0.unk_9E0 = &image;
    memset(&backing, 0, sizeof(backing)); backing.lease_id = 1;
    memcpy(backing.framebuffer, spans, sizeof(spans));
    memset(&owner, 0, sizeof(owner)); owner.validate = validate_display;
    owner.retire_display = retire_fixture; expected_display = &display;
    D_800A7428 = workspace; gDisplayListHead = borrowed_commands;
    current_screen = &borrowed; D_80068BB0 = &image; D_800A7440.x1 = 7;
    for (n = 0; n < 7; n++) {
        memcpy(bad, spans, sizeof(bad));
        if (n == 0) bad[0].pixels = NULL;
        if (n == 1) bad[0].pixels++;
        if (n == 2) bad[0].bytes--;
        if (n == 3) bad[0].pixels = (RlU8 *)0x803FFFc0U;
        if (n == 4) bad[0].pixels = (RlU8 *)0x807DA7C0U;
        if (n == 5) bad[1].pixels = bad[0].pixels;
        if (n == 6) bad[0].pixels = (RlU8 *)0xFFFFFFC0U;
        memset(&display, 0, sizeof(display)); allocations = 0;
        assert(sc_native_display_acquire(&display, bad) == SC_UNAVAILABLE);
        assert(!allocations && !depth && !used && !display.acquired);
    }
    for (n = 0; n <= 4; n++) {
        memset(&display, 0, sizeof(display)); allocations = 0; fail_at = n;
        fail_scope = n == 0;
        assert(sc_native_display_acquire(&display, spans) == SC_UNAVAILABLE);
        assert(!depth && !used && !sStoryWorkspace && !display.acquired);
        assert(current_screen == &borrowed && D_80068BB0 == &image && D_800A7440.x1 == 7);
        assert(!memcmp(&D_800A7428, &workspace, sizeof(workspace)) && gDisplayListHead == borrowed_commands);
    }
    fail_scope = 0; fail_at = 0; allocations = 0;
    workspace_allocations = pixel_allocations = 0;
    assert(sc_native_display_acquire(&display, spans) == SC_OK && allocations == 4);
    assert(workspace_allocations == 2 && !pixel_allocations);
    assert(display.screen->unk_18[0] == &display.images[0] &&
           display.screen->unk_18[1] == &display.images[1]);
    assert(display.images[0].img_p == spans[0].pixels && display.images[1].img_p == spans[1].pixels);
    assert(display.images[0].width == 640 && display.images[0].height == 480 &&
           display.images[0].size == IMAGE_SIZE_BITS_16b && !display.images[0].depth_p);
    assert(gDisplayListHead == borrowed_commands && current_screen == &borrowed);
    assert(sc_native_display_begin(&display) == SC_OK);
    assert(sc_native_display_release(&display) == SC_UNAVAILABLE);
    assert(sc_native_display_end(&display) == SC_OK);
    assert(gDisplayListHead == borrowed_commands && D_80068BB0 == &image && D_800A7440.x1 == 7);
    retained = sStoryWorkspace->owned_head;
    main_pool_push_state(0);
    before_allocations = allocations;
    current_screen = &child; child.unk_11 = 42; saved = child;
    gDisplayListHead = child_commands;
    assert(sc_native_display_begin(&display) == SC_OK && gDisplayListHead > retained);
    assert(sc_native_display_end(&display) == SC_OK);
    assert(allocations == before_allocations);
    assert(current_screen == &child && !memcmp(&child, &saved, sizeof(child)));
    assert(gDisplayListHead == child_commands && D_80068BB0 == &image && D_800A7440.x1 == 7);
    assert(sc_native_display_release(&display) == SC_UNAVAILABLE);
    main_pool_pop_state(0); current_screen = &borrowed; gDisplayListHead = borrowed_commands;
    assert(sc_native_display_release(&display) == SC_UNAVAILABLE && used);
    validate_status = SC_INVALID;
    assert(sc_native_display_retire(&display, &owner, &backing) == SC_INVALID && !closures);
    validate_status = SC_OK;
    display.images[0].width--;
    assert(sc_native_display_retire(&display, &owner, &backing) == SC_UNAVAILABLE && !closures);
    display.images[0].width++;
    retirement_status = SC_UNAVAILABLE;
    assert(sc_native_display_retire(&display, &owner, &backing) == SC_UNAVAILABLE && closures == 1);
    assert(sc_native_display_begin(&display) == SC_UNAVAILABLE && used && depth);
    assert(sc_native_display_release(&display) == SC_UNAVAILABLE && used && depth);
    retirement_status = SC_OK; hold_vi = 1;
    vi_current = (void *)0xA0400000U;
    for (n = 0; n < 3; n++)
        assert(sc_native_display_retire(&display, &owner, &backing) == SC_PENDING && closures == 1);
    assert(!display.retired && used && depth);
    hold_vi = 0;
    assert(sc_native_display_retire(&display, &owner, &backing) == SC_OK && closures == 1);
    hold_vi = 1; vi_current = (void *)0x80400000U;
    assert(sc_native_display_retire(&display, &owner, &backing) == SC_PENDING && closures == 1);
    assert(!display.retired && sc_native_display_release(&display) == SC_UNAVAILABLE && depth);
    hold_vi = 0;
    assert(sc_native_display_retire(&display, &owner, &backing) == SC_OK && closures == 1);
    assert(sc_native_display_release(&display) == SC_OK && !tasks && !used && !depth);
    assert(current_screen == &borrowed && D_80068BB0 == &image && D_800A7440.x1 == 7);
    assert(!memcmp(&D_800A7428, &workspace, sizeof(workspace)) && gDisplayListHead == borrowed_commands);
    assert(sc_native_display_release(&display) == SC_UNAVAILABLE);
    current_screen = NULL; closures = 0;
    assert(sc_native_display_acquire(&display, spans) == SC_OK);
    assert(sc_native_display_begin(&display) == SC_OK && sc_native_display_end(&display) == SC_OK);
    hold_vi = 1; vi_current = (void *)0x80400000U;
    assert(sc_native_display_retire(&display, &owner, &backing) == SC_PENDING && !current_screen);
    assert(display.acquired && depth && !display.retired);
    hold_vi = 0;
    for (n = 0; n < 3 && !display.retired; n++)
        assert(sc_native_display_retire(&display, &owner, &backing) != SC_INVALID);
    assert(display.retired && closures == 1);
    assert(sc_native_display_release(&display) == SC_OK && !depth && !used);
    puts("Checked display: owner spans, finite acquisition refusal, actual scheduler transition with simulated VI, pre-pop retain/retry/fallback; HOST only; OK");
    return 0;
}
