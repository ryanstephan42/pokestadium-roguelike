/* Connected loader job -> ready -> publication -> consumer regression. The worker, publisher and
 * effect-table functions are extracted verbatim from src/19840.c, src/1C720.c and
 * src/fragments/62/fragment62_315D50.c (see run_story_batl_jobs.sh); only the archive loader,
 * geo parser, OS mailbox and draw consumers are injected seams. */

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "src/fragments/62/fragment62.h"
#include "src/19840.h"
#include "src/1C720.h"
#include "src/17300.h"
#include "src/geo_layout.h"
#include "src/memory.h"
#include "src/story/sc_batl_scope.h"
#include "src/story/sc_batl_jobs.h"
#include "src/roguelike/rl_geo_guard.h"
#include <sys/mman.h>

#define CHECK(x)                                                         \
    do {                                                                 \
        if (!(x)) {                                                      \
            printf("CHECK FAILED %s:%d %s\n", __FILE__, __LINE__, #x); \
            fflush(NULL);                                                \
            abort();                                                     \
        }                                                                \
    } while (0)

unk_D_800ABE10 D_800ABE10;
unk_D_84390010* D_84390010[2];
u8* D_843920C0[256];
s16 D_843924C0[0x10];
arg1_func_80019420* D_843924E0[40];
OSThread* __osRunningThread;
static OSThread game_thread;

/* ---- injected OS seams ---- */
static struct {
    OSMesgQueue* q;
    OSMesg m[8];
    int n;
} boxes[8];
static int nboxes;
static jmp_buf stop_return, loader_return;
static int expect_stop, stops, hang;

static int box(OSMesgQueue* q) {
    int i;
    for (i = 0; i < nboxes; i++) {
        if (boxes[i].q == q) {
            return i;
        }
    }
    CHECK(nboxes < 8);
    boxes[nboxes].q = q;
    boxes[nboxes].n = 0;
    return nboxes++;
}
s32 osSendMesg(OSMesgQueue* q, OSMesg m, s32 flags) {
    int b = box(q);
    (void)flags;
    CHECK(boxes[b].n < 8);
    boxes[b].m[boxes[b].n++] = m;
    return 0;
}
s32 osRecvMesg(OSMesgQueue* q, OSMesg* m, s32 flags) {
    int b = box(q);
    if (boxes[b].n == 0) {
        if (flags == OS_MESG_NOBLOCK) {
            return -1;
        }
        if (__osRunningThread == &D_800ABE10.unk_000) {
            longjmp(loader_return, 1); /* loader idles after its job */
        }
        /* a blocking receive with nothing queued never returns on hardware */
        hang = 1;
        longjmp(stop_return, 2);
    }
    *m = boxes[b].m[0];
    memmove(&boxes[b].m[0], &boxes[b].m[1], sizeof(OSMesg) * --boxes[b].n);
    return 0;
}
void osSyncPrintf(const char* f, ...) {
    (void)f;
}
void osStopThread(OSThread* t) {
    CHECK(t == NULL);
    if (__osRunningThread == &D_800ABE10.unk_000) {
        longjmp(loader_return, 1);
    }
    ++stops;
    CHECK(expect_stop);
    longjmp(stop_return, 1);
}
void __osSetFpcCsr(u32 v) {
    (void)v;
}
static int mallocs, frees;
void* Util_Malloc(s32 n) {
    ++mallocs;
    return calloc(1, n);
}
void Util_Free(void* p) {
    ++frees;
    free(p);
}
void* main_pool_alloc(u32 size, s32 side) {
    CHECK(side == 1);
    return aligned_alloc(64, size);
}
s32 func_80001C90(void) {
    return 1;
}
static f32 clock_value;
const f32* sc_batl_clock_source(void) {
    return &clock_value;
}

/* ---- injected archive/parser seams ---- */
static u8 *model_buf[2], *fx_buf[2];
static union {
    MemoryBlock block;
    MainPoolState state;
} pool_view;
static MainPoolBlock pool_head;
static int fail_load_at = -1, refuse_at = -1, bad_root, loads;
/* fx_shape: effect list placed at the effect-buffer end on load fx_shape_at */
static int fx_shape, fx_shape_at = -1;
static u32 bound_buffer;
void* func_80002DA4(MainPoolState* pool, s32 size) {
    bound_buffer = (u32)pool;
    pool_view.block.size = 0x1000;
    pool_view.state.listHeadL = &pool_head;
    (void)size;
    return &pool_view;
}
u32 osMemSize = 0x800000;
/* rl_geo_refuse/rl_geo_image_set/rl_geo_recolor_admit extracted verbatim from src/geo_layout.c */
#include "geo-image-extracted.inc"
/* image_case: the model fragment is a FRAGMENT image in a fixed "RDRAM" page whose type-E
 * recolor descriptor reaches past the image, so the real recolor admission refuses */
static int image_case;
static Fragment* rdram_image(void) {
    static Fragment* f;
    if (f == NULL) {
        void* at = mmap((void*)0x80100000, 0x2000, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
        CHECK(at == (void*)0x80100000);
        f = at;
        ((u32*)f->magic)[0] = 'FRAG';
        ((u32*)f->magic)[1] = 'MENT';
        f->headerSize = 0x20;
        f->relocOffset = 0x1000;
        f->sizeInRam = 0x2000;
    }
    return f;
}
/* clears always run the real scope reset; only the image fixture's load is a FRAGMENT image */
void rl_geo_image_set(Fragment* f) {
    if (f == NULL || image_case) {
        geo_image_set(f);
    }
}
/* mirrors rl_geo_refuse on the loader thread */
static void parser_refuse(void) {
    rl_loader_refuse_notify();
    osStopThread(NULL);
}
Fragment* func_80019204(MainPoolState* pool, BinArchive* archive, s32 index, u32 a3, s32 a4) {
    int call = loads++;
    u8* base = (u8*)bound_buffer;
    (void)pool;
    (void)a3;
    (void)a4;
    if (call == fail_load_at) {
        return NULL;
    }
    if (call == refuse_at) {
        parser_refuse();
    }
    if (archive == D_800ABE10.unk_A04.unk_00 && image_case) {
        return rdram_image();
    }
    if (archive == D_800ABE10.unk_A04.unk_00) {
        memset(base, 0, 0x1000);
        return (Fragment*)base;
    }
    if (call == fx_shape_at) {
        /* records occupy the last 16 bytes of the 0x18000 effect buffer */
        arg1_func_80019420* r = (arg1_func_80019420*)(base + 0x18000 - 16);
        Fragment* f = (Fragment*)r - 1;
        r[0].unk_00 = 1;
        r[0].unk_02 = fx_shape == 3 ? 256 : fx_shape == 4 ? -1 : fx_shape == 5 ? 0 : 200;
        /* shapes 13..16: published payload at the last aligned word, one past the end, misaligned, NULL */
        r[0].unk_04 = fx_shape == 13   ? base + 0x18000 - 4
                      : fx_shape == 14 ? base + 0x18000
                      : fx_shape == 15 ? base + 0x42
                      : fx_shape == 16 ? NULL
                                       : base + 0x40;
        r[1].unk_00 = fx_shape == 2 ? 1 : 0;
        r[1].unk_02 = 201;
        r[1].unk_04 = base + 0x40;
        return f;
    }
    {
        Fragment* f = (Fragment*)(base + 0x1000 * (index & 7));
        arg1_func_80019420* r = (arg1_func_80019420*)(f + 1);
        memset(f, 0, 0x100);
        r[0].unk_00 = 3;
        r[0].unk_02 = 100 + index * 2;
        r[0].unk_04 = (u8*)f + 0x80;
        r[1].unk_00 = 1;
        r[1].unk_02 = 101 + index * 2;
        r[1].unk_04 = (u8*)f + 0xC0;
        r[2].unk_00 = 0;
        return f;
    }
}
GraphNode* process_geo_layout(MemoryBlock* pool, void* seg) {
    (void)pool;
    return (GraphNode*)seg;
}
s32 func_80019328(MemoryBlock* pool, Fragment* frag, arg1_func_80010CA8 tint) {
    u8* base = (u8*)frag;
    u8* header = base + 0x100;
    u32* roots = (u32*)(base + 0x200);
    u8* root = base + 0x300;
    (void)pool;
    if (image_case) {
        /* the real geo_layout image scope is active for exactly this loader-thread image */
        static unk_D_86002F34 node;
        static RlGeoTexDesc tex[1];
        CHECK(rl_geo_image_active() && sRlGeoImageLo == (u32)frag && sRlGeoImageHi == (u32)frag + 0x1000 &&
              sRlGeoImageThread == &D_800ABE10.unk_000);
        tex[0].kind = 0;
        tex[0].type = 2;
        tex[0].texels = 4;
        tex[0].texture = (u32)frag + 0x1000 - 4; /* 8 bytes written, 4 past the image */
        node.unk_00.unk_00 = 0xE;
        node.unk_18 = (void*)tex;
        node.unk_24 = 1;
        node.unk_1C = NULL;
        node.unk_25 = 0;
        /* func_80019328's ROGUELIKE call before func_80018C40 */
        rl_geo_recolor_admit(&node);
        CHECK(0);
    }
    /* near-end type-E roots: 4 -> end-0x18 (node fits, root does not), 5 -> end-0x2C (exact),
     * 6 -> end-0x28 (four bytes short); only bytes inside the buffer are written */
    if (bad_root >= 4) {
        root = base + 0x3C000 - (bad_root == 4 ? 0x18 : bad_root == 5 ? 0x2C : 0x28);
    }
    (void)tint;
    memset(header, 0, 0x300);
    *(u16*)header = 0;
    header[3] = 1;
    *(u32**)(header + 8) = roots;
    *(Fragment**)(header + 0x14) = frag;
    root[0] = 0xE;
    if (bad_root < 4 || bad_root == 5) {
        *(u32*)(root + 0x28) = bad_root == 2 ? (u32)frag + 4 : (u32)frag;
    }
    roots[0] = bad_root == 1 ? (u32)base + 0x3C000 : (u32)root;
    if (bad_root == 3) {
        /* roots array outside the worker buffer, root itself valid */
        static u32 outside[1];
        outside[0] = (u32)root;
        *(u32**)(header + 8) = outside;
    }
    return (s32)header;
}

/* ---- injected draw/consumer seams ---- */
static void *graph_seen, *anim_seen;
static arg1_func_80019420 base_records[3];
arg1_func_80019420* func_80019EA0(s32 i) {
    CHECK(i == 0);
    return base_records;
}
arg1_func_80010CA8* func_8001BEE8(arg1_func_80010CA8* t, unk_func_80026268_arg0* g) {
    (void)g;
    return t;
}
void func_8001BC34(unk_D_86002F58_004_000* a, s16 b, s16 c, unk_D_86002F58_004_000_004* g) {
    (void)a;
    (void)b;
    (void)c;
    graph_seen = g;
}
s32 func_800173DC(unk_D_86002F58_004_000* a, s16 b, void* anim, s32 d) {
    (void)a;
    (void)b;
    (void)d;
    anim_seen = anim;
    return 0;
}
void func_80017464(unk_D_86002F58_004_000* a, s16 b) {
    (void)a;
    (void)b;
}

static u8 row_one[] = { 3, 0x3F };
static u8 row_two[] = { 4, 5, 0x3F };
static u8* D_84386D44[][2] = { { row_one, row_one }, { row_two, row_two } };
static u8* D_84386E08[][5] = { { row_one, row_one, row_one, row_two, row_one } };

#define SC_FX_REQUIRE(ok, code)      \
    do {                             \
        if (!(ok)) {                 \
            sc_batl_job_fatal(code); \
        }                            \
    } while (0)
#include "jobs-extracted.inc"

static unk_D_86002F58_004_000_010 workers[2];
static unk_D_800AE540_1194 regs[2];
static unk_D_86002F58_004_000 actor;

static void install(void) {
    static u8* upper;
    int i;
    upper = aligned_alloc(64, SC_BATL_LOADER_BYTES);
    CHECK(sc_batl_loader_bind(upper, SC_BATL_LOADER_BYTES) && sc_batl_loader_arm());
    for (i = 0; i < 2; i++) {
        model_buf[i] = sc_batl_loader_alloc(0x3C000);
        fx_buf[i] = sc_batl_loader_alloc(0x18000);
    }
    CHECK(sc_batl_loader_alloc(0x28000) != NULL);
    CHECK(sc_batl_loader_disarm(&workers[0], &workers[1]));
    for (i = 0; i < 2; i++) {
        workers[i].unk_00 = 3;
        workers[i].unk_04[0] = (s32)model_buf[i];
        workers[i].unk_18 = (s32)fx_buf[i];
        regs[i].unk_10 = &workers[i];
    }
    D_800ABE10.unk_A04.unk_00 = (BinArchive*)0x920000;
    D_800ABE10.unk_A04.unk_04 = (BinArchive*)0x8CC000;
    base_records[0].unk_00 = 3;
    base_records[0].unk_02 = 1;
    base_records[0].unk_04 = &base_records[2];
    base_records[1].unk_00 = 1;
    base_records[1].unk_02 = 2;
    base_records[1].unk_04 = &base_records[1];
    __osRunningThread = &game_thread;
}

/* one iteration of the real loader thread body */
static void loader(void) {
    __osRunningThread = &D_800ABE10.unk_000;
    if (!setjmp(loader_return)) {
        func_80019600(NULL);
    }
    __osRunningThread = &game_thread;
}

static void model_request(int side, int species) {
    regs[side].unk_00 = 0x80;
    regs[side].unk_02 = species;
    regs[side].unk_04 = NULL;
    func_8001C07C(&regs[side]);
}

/* 1C1E8 ready publication; returns 1 when the run stopped visibly */
static int model_ready(int side) {
    int r;
    expect_stop = 1;
    hang = 0;
    if (!(r = setjmp(stop_return))) {
        func_8001C1E8((unk_func_8001C248*)&regs[side]);
        expect_stop = 0;
        return 0;
    }
    expect_stop = 0;
    CHECK(r == 1 && !hang);
    return 1;
}

static int guarded(void (*f)(void)) {
    int r;
    expect_stop = 1;
    if (!(r = setjmp(stop_return))) {
        f();
        expect_stop = 0;
        return 0;
    }
    expect_stop = 0;
    CHECK(r == 1);
    return 1;
}

static int fx_side, fx_a, fx_b, fx_row;
static void do_fx_request(void) {
    func_8432C9C0(&regs[fx_side], fx_row);
    func_8001C248((unk_func_8001C248*)&regs[fx_side]);
}
static void do_fx_send(void) {
    func_8001C248((unk_func_8001C248*)&regs[fx_side]);
}
static void do_fx_ready(void) {
    func_8001C2D8((unk_func_8001C248*)&regs[fx_side]);
    CHECK(regs[fx_side].unk_1D & 0x10);
    func_8432CAA8(&regs[fx_side]);
}
static void do_cd70(void) {
    func_8432CD70(&actor, fx_a, fx_b);
}
static void do_ce00(void) {
    func_8432CE00(&actor, fx_a);
}
static void do_ce3c(void) {
    func_8432CE3C(&actor, fx_a);
}
static void do_publish(void) {
    func_8432CAA8(&regs[fx_side]);
}

static void model_cases(int c) {
    unk_D_86002F58_004_000_010_024* first;
    model_request(0, 25);
    CHECK(boxes[box(&D_800ABE10.unk_9D0)].n == 1);
    if (c == 29) {
        sp18_func_800198E4* job = (sp18_func_800198E4*)boxes[box(&D_800ABE10.unk_9D0)].m[0];
        int reply = box((OSMesgQueue*)job->unk_04);
        image_case = 1;
        loader();
        /* refusal completed this job (size 0, no result) and cleared the image scope before any
         * publication; the caller then stops visibly instead of waiting on the stopped loader */
        CHECK(!rl_geo_image_active() && sRlGeoImageLo == 0 && sRlGeoImageHi == 0 && sRlGeoImageThread == NULL);
        CHECK(boxes[reply].n == 1 && boxes[reply].m[0] == (OSMesg)job && job->unk_14 == 0 && job->unk_08 == 0);
        CHECK(!(regs[0].unk_00 & 0x10) && regs[0].unk_14 == NULL && workers[0].unk_24 == NULL);
        CHECK(model_ready(0) && stops == 1);
        CHECK(!(regs[0].unk_00 & 0x10) && regs[0].unk_14 == NULL && workers[0].unk_24 == NULL);
        printf("MODEL case 29: active-image recolor refusal -> notify -> scope cleared -> job completed -> "
               "caller terminal stop\n");
        return;
    }
    if (c == 28) {
        /* queued job retargeted to worker 1's model buffer instead of worker 0's admitted buffer */
        ((sp18_func_800198E4*)boxes[box(&D_800ABE10.unk_9D0)].m[0])->unk_10 = (s32)model_buf[1];
    }
    if (c == 3) {
        fail_load_at = 0;
    }
    if (c == 4) {
        refuse_at = 0;
    }
    if (c == 5 || c == 6 || c == 7) {
        bad_root = c - 4;
    }
    if (c >= 19 && c <= 21) {
        bad_root = c - 15;
    }
    loader();
    if (c == 31 || c == 32 || c == 33) {
        int q = box(&workers[0].unk_30);
        sp18_func_800198E4* job;
        CHECK(boxes[q].n == 1);
        job = (sp18_func_800198E4*)boxes[q].m[0];
        if (c == 33) {
            /* a second load recorded for the same worker before admission, even with identical values */
            __osRunningThread = &D_800ABE10.unk_000;
            sc_batl_model_loaded(&workers[0].unk_30, job, job->unk_02,
                                 (void*)*(u32*)((u8*)job->unk_08 + 0x14), (void*)job->unk_08);
            __osRunningThread = &game_thread;
        } else if (c == 31) {
            /* loaded index 24 (request 25), job now claims request 31 */
            job->unk_02 = 30;
        } else {
            /* identical, fully valid header copy elsewhere in the buffer: only provenance differs */
            memcpy(model_buf[0] + 0x180, (void*)job->unk_08, 0x18);
            job->unk_08 = (s32)(model_buf[0] + 0x180);
        }
    }
    if ((c >= 3 && c <= 7) || c == 19 || c == 21 || c == 28 || c == 31 || c == 32 || c == 33) {
        CHECK(model_ready(0));
        CHECK(!(regs[0].unk_00 & 0x10) && regs[0].unk_14 == NULL && workers[0].unk_24 == NULL);
        CHECK(workers[0].unk_01 & 1);
        printf("MODEL case %d: failed job stopped visibly before ready (no old-worker publication)\n", c);
        return;
    }
    CHECK(!model_ready(0));
    first = workers[0].unk_24;
    CHECK(first == (void*)(model_buf[0] + 0x100) && (void*)regs[0].unk_14 == first);
    CHECK((regs[0].unk_00 & 0x10) && !(workers[0].unk_01 & 1));
    if (c == 20) {
        CHECK(*(u32*)(model_buf[0] + 0x3C000 - 4) == (u32)model_buf[0]);
        printf("MODEL case 20: type-E root ending exactly at the buffer end admitted\n");
        return;
    }
    CHECK(*(u32*)(model_buf[0] + 0x300 + 0x28) == (u32)model_buf[0]);
    if (c == 0) {
        printf("MODEL case 0: new load admitted (header/roots/owner in buffer) and readied\n");
        return;
    }
    /* the same species/tint is an explicit live-cache hit, sent straight back */
    *(u16*)first = 25;
    model_request(0, 25);
    CHECK(boxes[box(&D_800ABE10.unk_9D0)].n == 0);
    if (c == 8) {
        /* live header rewritten to another model while the cache token is queued */
        *(u16*)first = 27;
        CHECK(model_ready(0) && !(regs[0].unk_00 & 0x10));
        printf("MODEL case 8: queued cache token whose live header changed model stops\n");
        return;
    }
    if (c == 27) {
        /* live tint changed while the cache token is queued */
        workers[0].unk_2C.raw ^= 1;
        CHECK(model_ready(0) && !(regs[0].unk_00 & 0x10));
        printf("MODEL case 27: queued cache token whose live tint changed stops\n");
        return;
    }
    if (c == 9) {
        /* live header replaced by another same-model header while the cache token is queued */
        static u8 other[0x40] __attribute__((aligned(8)));
        memcpy(other, first, sizeof(other));
        workers[0].unk_24 = (void*)other;
        CHECK(model_ready(0) && !(regs[0].unk_00 & 0x10));
        printf("MODEL case 9: queued cache token for a replaced live header stops\n");
        return;
    }
    CHECK(!model_ready(0));
    CHECK(workers[0].unk_24 == first && (void*)regs[0].unk_14 == first && (regs[0].unk_00 & 0x10));
    if (c == 1) {
        printf("MODEL case 1: new load then explicit live-cache hit both ready with their header\n");
        return;
    }
    /* c == 2: a failed replacement after a valid one must not republish the old header */
    model_request(0, 26);
    fail_load_at = loads;
    loader();
    CHECK(model_ready(0));
    CHECK(workers[0].unk_24 == first && !(regs[0].unk_00 & 0x10) && (workers[0].unk_01 & 1));
    printf("MODEL case 2: failed replacement after valid load stops; old header not re-readied\n");
}

static void fx_cases(int c) {
    func_8432C86C();
    fx_a = 1;
    fx_b = 2;
    CHECK(!guarded(do_cd70) && graph_seen == &base_records[2] && anim_seen == &base_records[1]);
    if (c == 16) {
        fx_side = 0;
        CHECK(guarded(do_publish));
        printf("EFFECT case 16: publication without a completed request stops\n");
        return;
    }
    fx_side = 0;
    fx_row = 1; /* entries 4,5 -> records 108/109 and 110/111 */
    if (c == 30) {
        /* both sides arm while nothing is pending; the later send must see the other side's request */
        int q = box(&D_800ABE10.unk_9D0);
        func_8432C9C0(&regs[0], fx_row);
        func_8432C9C0(&regs[1], fx_row);
        CHECK(!guarded(do_fx_send) && boxes[q].n == 1 && (workers[0].unk_01 & 2));
        fx_side = 1;
        CHECK(guarded(do_fx_send) && boxes[q].n == 1 && !(workers[1].unk_01 & 2) && !(regs[1].unk_1D & 0x40));
        printf("EFFECT case 30: second side's send while the first is pending stops before queueing\n");
        return;
    }
    if (c >= 22) {
        fx_shape = c - 21;
        fx_shape_at = loads;
    }
    if (c == 11) {
        fail_load_at = loads + 1;
    }
    if (c == 12) {
        refuse_at = loads + 1;
    }
    CHECK(!guarded(do_fx_request));
    loader();
    if (c == 17) {
        /* shared request list shortened after the load: result count no longer matches */
        D_843924C0[1] = -1;
        CHECK(guarded(do_fx_ready) && D_843920C0[108] == NULL);
        printf("EFFECT case 17: request list rewritten after load stops before publication\n");
        return;
    }
    if (c == 11 || c == 12) {
        expect_stop = 1;
        hang = 0;
        if (!setjmp(stop_return)) {
            func_8001C2D8((unk_func_8001C248*)&regs[0]);
            CHECK(0);
        }
        expect_stop = 0;
        CHECK(!hang && !(regs[0].unk_1D & 0x10) && D_843920C0[108] == NULL);
        printf("EFFECT case %d: %s request stops before any record is published\n", c,
               c == 11 ? "partial" : "refused");
        return;
    }
    if (c >= 22) {
        if (c == 22 || c == 34) {
            CHECK(!guarded(do_fx_ready));
            CHECK(D_843920C0[200] == fx_buf[0] + (c == 22 ? 0x40 : 0x18000 - 4) &&
                  D_843920C0[110] == fx_buf[0] + 0x5000 + 0x80);
            printf("EFFECT case %d: %s published\n", c,
                   c == 22 ? "record list ending exactly at the buffer end" : "payload at the last buffer word");
            return;
        }
        CHECK(guarded(do_fx_ready) && D_843920C0[200] == NULL && D_843920C0[110] == NULL);
        printf("EFFECT case %d: %s stops before publication\n", c,
               c == 23   ? "list running past the buffer end"
               : c == 24 ? "slot id 256"
               : c == 25 ? "negative slot id"
               : c == 26 ? "non-terminal zero-slot record (C830 would spin)"
               : c == 35 ? "payload one past the buffer end"
               : c == 36 ? "misaligned payload"
                         : "NULL payload");
        return;
    }
    CHECK(!guarded(do_fx_ready));
    CHECK(D_843920C0[108] == fx_buf[0] + 0x4000 + 0x80 && D_843920C0[111] == fx_buf[0] + 0x5000 + 0xC0);
    fx_a = 108;
    fx_b = 109;
    CHECK(!guarded(do_cd70) && graph_seen == D_843920C0[108] && anim_seen == D_843920C0[109]);
    fx_a = 1;
    CHECK(!guarded(do_ce00) && graph_seen == &base_records[2]);
    if (c == 10) {
        printf("EFFECT case 10: complete two-entry request published; base entry-0 records kept\n");
        return;
    }
    if (c == 13) {
        fx_a = 109;
        CHECK(guarded(do_ce00));
        printf("EFFECT case 13: data record used as attachment root stops\n");
        return;
    }
    if (c == 14) {
        fx_side = 1;
        fx_row = 0;
        CHECK(!guarded(do_fx_request));
        fx_side = 0;
        fx_row = 1;
        CHECK(guarded(do_fx_request));
        CHECK(D_843924C0[0] == 3 && D_843924C0[1] == -1);
        printf("EFFECT case 14: shared request list rewrite while the other side loads stops\n");
        return;
    }
    /* c == 15/18: the same side sends a new request; its previous records are now stale */
    fx_row = 0;
    CHECK(!guarded(do_fx_request));
    fx_a = 109;
    if (c == 18) {
        loader();
        CHECK(!guarded(do_fx_ready));
        fx_a = 106;
        CHECK(!guarded(do_ce00) && graph_seen == D_843920C0[106]);
        fx_a = 109;
        CHECK(D_843920C0[109] != NULL);
        CHECK(guarded(do_ce3c));
        printf("EFFECT case 18: previous-generation record after the next request publishes stops\n");
        return;
    }
    CHECK(guarded(do_ce3c));
    printf("EFFECT case 15: stale previous-request record after a new send stops\n");
}

int main(int argc, char** argv) {
    int c = argc > 1 ? atoi(argv[1]) : 0;
    install();
    if (c >= 19 && c <= 21) {
        /* exact-size model buffer so any read past its end is a sanitizer fault */
        model_buf[0] = aligned_alloc(64, 0x3C000);
        workers[0].unk_04[0] = (s32)model_buf[0];
    }
    if (c < 10 || (c >= 19 && c <= 21) || (c >= 27 && c <= 29) || (c >= 31 && c <= 33)) {
        model_cases(c);
    } else {
        fx_cases(c);
    }
    CHECK(c >= 10 || mallocs >= 1);
    return 0;
}
