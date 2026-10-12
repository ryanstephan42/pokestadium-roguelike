#ifdef STORY_SERVICE_CANDIDATE
#include "sc_batl_scope.h"
#include "src/memmap.h"
#include "src/19840.h"
#include "src/memory.h"
#include "src/dp_intro.h"
#include "sc_batl_jobs.h"

static struct {
    ScBatImports imports;
    u32 next_generation, next_ticket, ticket, active, failed, stopping;
    OSMesg retained_message;
} scope;

static const u32 loader_order[5] = {
    SC_BATL_LOADER_MODEL, SC_BATL_LOADER_ANIMATION,
    SC_BATL_LOADER_MODEL, SC_BATL_LOADER_ANIMATION, SC_BATL_LOADER_STAGING
};
static struct {
    u8 *base;
    const void *workers[2];
    u32 bound, armed, taken, offset, live, generation, drained;
} loader;
static ScFxTable fx;
static ScJobTicket model_tickets[2];
static u32 fatal_code;

static int roots_match(int capture) {
    unsigned i;
    for (i = 0; i < 2; ++i) {
        unk_D_84390010 *root = D_84390010[i];
        unk_D_800AE540_0004 *group;
        if (!root || !root->unk_720 || root->unk_720->unk_01 != 1 ||
            !root->unk_720->unk_10) return 0;
        group = root->unk_720->unk_08[0];
        if (!group || group != root->unk_724 || !group->unk_214 ||
            !group->unk_002 || group->unk_002 > 6 ||
            group->unk_214->unk_002 != group->unk_002) return 0;
        if (capture) {
            scope.imports.roots[i] = root;
            scope.imports.groups[i] = group;
            scope.imports.registrations[i] = root->unk_720;
            scope.imports.metadata[i] = group->unk_214;
            scope.imports.counts[i] = group->unk_002;
            scope.imports.workers[i] = root->unk_720->unk_10;
        } else if (scope.imports.roots[i] != root ||
                   scope.imports.groups[i] != group ||
                   scope.imports.registrations[i] != root->unk_720 ||
                   scope.imports.metadata[i] != group->unk_214 ||
                   scope.imports.counts[i] != group->unk_002 ||
                   scope.imports.workers[i] != root->unk_720->unk_10) return 0;
    }
    return 1;
}

static int live(void) {
    uintptr_t encoded;
    if (!scope.active || scope.failed || !scope.imports.generation ||
        scope.imports.clock != sc_batl_clock_source() || !roots_match(0) ||
        scope.imports.map_generation != Memmap_GetStoryBattleMapGeneration()) return 0;
    encoded = Memmap_GetLoadedFragmentVaddr((uintptr_t)scope.imports.clock);
    return (encoded & 0xfff00000U) == 0x84300000U &&
        Memmap_GetFragmentVaddr((void *)0x84300000U) == scope.imports.native_base &&
        Memmap_GetFragmentSize(0x33) == scope.imports.native_size;
}

int sc_batl_scope_open(void) {
    uintptr_t clock, encoded, base;
    size_t bytes;
    u32 map_generation;
    if (scope.active || scope.ticket || scope.failed ||
        scope.next_generation == 0xffffffffU) return 0;
    clock = (uintptr_t)sc_batl_clock_source();
    encoded = Memmap_GetLoadedFragmentVaddr(clock);
    base = Memmap_GetFragmentVaddr((void *)0x84300000U);
    bytes = Memmap_GetFragmentSize(0x33);
    map_generation = Memmap_GetStoryBattleMapGeneration();
    if ((encoded & 0xfff00000U) != 0x84300000U || !base || !bytes ||
        !map_generation || bytes < sizeof(f32) || clock < base ||
        clock - base > bytes - sizeof(f32) || !roots_match(1)) return 0;
    if (loader.live && !loader.drained &&
        (scope.next_generation + 1 != loader.generation ||
         scope.imports.workers[0] != loader.workers[0] ||
         scope.imports.workers[1] != loader.workers[1])) return 0;
    scope.imports.generation = ++scope.next_generation;
    scope.imports.map_generation = map_generation;
    scope.imports.native_base = base;
    scope.imports.native_size = bytes;
    scope.imports.clock = sc_batl_clock_source();
    scope.active = 1;
    scope.stopping = 0;
    return 1;
}

int sc_batl_scope_borrow(ScBatPermit *p, ScBatImports *imports) {
    if (!p || !imports || p->ticket || scope.ticket || !live() ||
        scope.next_ticket == 0xffffffffU) return 0;
    p->generation = scope.imports.generation;
    p->ticket = scope.ticket = ++scope.next_ticket;
    *imports = scope.imports;
    return 1;
}

int sc_batl_scope_check(const ScBatPermit *p) {
    return p && p->ticket && p->ticket == scope.ticket &&
        p->generation == scope.imports.generation && live();
}

int sc_batl_worker_admit(unk_D_86002F58_004_000_010 *worker) {
    unsigned i;
    if (!scope.active) return !scope.failed;
    for (i = 0; i < 2; ++i)
        if (scope.imports.workers[i] == worker)
            return !scope.stopping && live();
    return 1;
}

/*
 * Story workers load archive 920000 entry model_id-1. Campaign model ids are party/enemy species
 * (validated 1..151), Substitute 0x98 and the 25->0x99 variant; anything else is not a campaign model.
 */
int sc_batl_model_admit(unk_D_86002F58_004_000_010 *worker, u16 model_id) {
    unsigned i;
    if (!scope.active) return 1;
    for (i = 0; i < 2; ++i)
        if (scope.imports.workers[i] == worker) return model_id >= 1 && model_id <= 0x99;
    return 1;
}

int sc_batl_scope_drain(const ScBatPermit *p) {
    unsigned i, bit;
    if (!sc_batl_scope_check(p)) {
        scope.failed = 1;
        return -1;
    }
    scope.stopping = 1;
    for (i = 0; i < 2; ++i) {
        unk_D_86002F58_004_000_010 *worker = scope.imports.workers[i];
        for (bit = 1; bit <= 2; bit <<= 1) {
            OSMesgQueue *queue = bit == 1 ? &worker->unk_30 : &worker->unk_50;
            OSMesg message;
            sp18_func_800198E4 *job;
            if (!(worker->unk_01 & bit)) continue;
            if (osRecvMesg(queue, &message, OS_MESG_NOBLOCK) == -1) return 0;
            scope.retained_message = message;
            job = message;
            if (!job || job->unk_00 != bit || (uintptr_t)job->unk_04 != (uintptr_t)queue ||
                !sc_batl_scope_check(p) || !(worker->unk_01 & bit) ||
                func_80019B48(worker, job) != 1 || (worker->unk_01 & bit)) {
                scope.failed = 1;
                return -1;
            }
            scope.retained_message = 0;
        }
    }
    return 1;
}

int sc_batl_scope_retire(ScBatPermit *p) {
    if (!p || !p->ticket || p->ticket != scope.ticket ||
        p->generation != scope.imports.generation) return 0;
    if (!sc_batl_scope_check(p)) {
        scope.failed = 1;
        return 0;
    }
    scope.ticket = p->ticket = 0;
    return 1;
}

int sc_batl_scope_close(void) {
    unsigned i;
    if (!live() || scope.ticket) return 0;
    for (i = 0; i < 2; ++i)
        if (scope.imports.workers[i]->unk_01 & 3) return 0;
    if (loader.live && scope.imports.generation == loader.generation) loader.drained = 1;
    scope.active = 0;
    scope.imports.clock = 0;
    scope.imports.roots[0] = scope.imports.roots[1] = 0;
    scope.imports.groups[0] = scope.imports.groups[1] = 0;
    scope.imports.workers[0] = scope.imports.workers[1] = 0;
    return 1;
}

void sc_batl_scope_fail(void) { scope.failed = 1; }

void sc_batl_scope_require(int result) {
    if (result) return;
    scope.failed = 1;
    osSyncPrintf("story BATL scope ownership failure\n");
    for (;;) osStopThread(NULL);
}

/* Loader install record. Requests are checked against the exact vanilla
   order of func_84301430 (func_80019760(3) twice, then func_8001987C). Model
   pools stay lower (they execute); animation and staging take fixed upper
   DATA offsets. Any other request inside the window stops before a job can
   reference a span. Unbinding requires a BATL scope that captured the same
   two workers to have opened after install and closed fully drained. */

/* Spans already handed out stay retained: live and never drained. */
static void loader_fail(void) {
    loader.armed = 0;
    loader.live = 1;
    loader.drained = 0;
    sc_batl_scope_require(0);
}

int sc_batl_loader_bind(void *base, u32 bytes) {
    if (loader.bound || loader.armed || loader.live || !base ||
        bytes != SC_BATL_LOADER_BYTES || ((uintptr_t)base & 63U)) return 0;
#ifndef SC_SERVICE_HOST
    if ((uintptr_t)base < 0x80400000U || (uintptr_t)base > 0x807da7e0U - bytes) return 0;
#endif
    loader.base = base;
    loader.bound = 1;
    return 1;
}

int sc_batl_loader_unbind(void) {
    if (!loader.bound || loader.armed) return 0;
    if (loader.live && (!loader.drained || scope.active || scope.ticket || scope.failed))
        return 0;
    /* Same graphics-idle fence vanilla uses before overwriting loader buffers:
     * no queued RSP/RDP task may still read A/S when the span is released. */
    while (func_80001C90() == 0) {}
    loader.live = loader.drained = loader.generation = 0;
    loader.workers[0] = loader.workers[1] = 0;
    loader.bound = 0;
    loader.base = 0;
    return 1;
}

int sc_batl_loader_arm(void) {
    if (!loader.bound || loader.armed || loader.live || scope.active ||
        scope.ticket || scope.failed) return 0;
    loader.armed = 1;
    loader.taken = loader.offset = 0;
    return 1;
}

int sc_batl_loader_disarm(const void *worker0, const void *worker1) {
    if (!loader.armed || loader.taken != 5 || loader.offset != SC_BATL_LOADER_BYTES ||
        !worker0 || !worker1 || worker0 == worker1 ||
        scope.next_generation == 0xffffffffU) return 0;
    loader.armed = 0;
    loader.workers[0] = worker0;
    loader.workers[1] = worker1;
    loader.generation = scope.next_generation + 1;
    loader.drained = 0;
    loader.live = 1;
    sc_fx_reset(&fx);
    model_tickets[0].state = model_tickets[1].state = 0;
    return 1;
}

int sc_batl_loader_live(void) { return loader.bound || loader.armed || loader.live; }

void *sc_batl_loader_alloc(u32 size) {
    u8 *span;
    u32 want;
    if (!loader.armed) return main_pool_alloc(size, 1);
    if (loader.taken >= 5 || loader_order[loader.taken] != size) {
        loader_fail();
        return 0;
    }
    want = loader_order[loader.taken++];
    if (want == SC_BATL_LOADER_MODEL) return main_pool_alloc(size, 1);
    if (loader.offset > SC_BATL_LOADER_BYTES - size) {
        loader_fail();
        return 0;
    }
    span = loader.base + loader.offset;
    loader.offset += size;
    return span;
}

/* Loader job completion and effect-record publication (UI actor-history contracts). A story
 * worker is one recorded by the loader install, or captured by the open scope. */

static int story_worker(const unk_D_86002F58_004_000_010 *worker) {
    int i;
    for (i = 0; i < 2; ++i)
        if ((loader.live && loader.workers[i] == worker) ||
            (scope.active && scope.imports.workers[i] == worker)) return i;
    return -1;
}

void sc_batl_job_fatal(u32 code) {
    fatal_code = code;
    osSyncPrintf("story BATL loader job fatal %x\n", code);
    sc_batl_scope_require(0);
}

static int model_done(unk_D_86002F58_004_000_010 *worker, sp18_func_800198E4 *job) {
    ScJobModelView v;
    const u8 *header;
    const u32 *roots;
    u32 i, n, entry;
    int result;
    v.result = job->unk_08;
    v.token = job->unk_0C;
    v.size = job->unk_14;
    v.buffer = job->unk_10;
    v.expected = worker->unk_00 == 3 && worker->unk_02 == 0 ? worker->unk_04[0] : 0;
    v.live = (u32)worker->unk_24;
    v.live_model = worker->unk_24 ? worker->unk_24->unk_00 : 0;
    v.request = job->unk_02 + 1U;
    v.live_tint = worker->unk_2C.raw;
    v.job_tint = job->unk_18.raw;
    result = sc_job_model_result(&v);
    if (result != SC_JOB_LOADED) return result == SC_JOB_CACHED;
    header = (const u8 *)job->unk_08;
    n = header[3];
    roots = *(u32 *const *)(header + 8);
    entry = *(const u32 *)(header + 0x14);
    /* fresh result must be the loader's own load of archive index request-1 for this job */
    if (loader.live &&
        !sc_job_ticket_take(&model_tickets[story_worker(worker)], (u32)job, job->unk_02, entry, (u32)header))
        return 0;
    if (!sc_job_model_header_ok(v.buffer, entry, n, (u32)roots)) return 0;
    for (i = 0; i < n; ++i) {
        const u8 *root = (const u8 *)roots[i];
        if (!sc_job_in(v.buffer, SC_JOB_MODEL_BYTES, (u32)root, SC_JOB_ROOT_NODE_BYTES) ||
            (root[0] == 0xE && !sc_job_in(v.buffer, SC_JOB_MODEL_BYTES, (u32)root, SC_JOB_ROOT_BYTES)) ||
            !sc_job_model_root_ok(v.buffer, (u32)root, root[0],
                                  root[0] == 0xE ? *(const u32 *)(root + 0x28) : 0, entry)) return 0;
    }
    return 1;
}

/* Called from func_80019484 on the loader thread AFTER 19328 parsed the result and the image scope
 * was cleared: ties the completed job to the index/fragment/header actually loaded. It does not
 * snapshot the index at enqueue time and does not prove header+0 == species. */
void sc_batl_model_loaded(const void *queue, const void *job, s32 index, const void *fragment,
                          const void *header) {
    int i;
    if (!loader.live || header == NULL) return;
    for (i = 0; i < 2; ++i)
        if (loader.workers[i] && queue == &((const unk_D_86002F58_004_000_010 *)loader.workers[i])->unk_30)
            sc_job_ticket_put(&model_tickets[i], (u32)job, (u32)(u16)index, (u32)fragment, (u32)header);
}

int sc_batl_job_admit(unk_D_86002F58_004_000_010 *worker, sp18_func_800198E4 *job) {
    int w = story_worker(worker);
    if (w < 0) return 1;
    if (job->unk_00 == 1) return model_done(worker, job);
    if (job->unk_00 != 2 ||
        !sc_job_fx_complete(job->unk_14, job->unk_10, worker->unk_18, (const short *)job->unk_0C,
                            (const unsigned int *)job->unk_08)) return 0;
    return !loader.live || sc_fx_complete(&fx, w);
}

/* 19A7C send: both sides share the request list and the result array. */
int sc_batl_fx_send(unk_D_86002F58_004_000_010 *worker) {
    int w = story_worker(worker);
    if (w < 0 || !loader.live) return 1;
    return sc_fx_send(&fx, w, (loader.workers[w ^ 1] != 0 &&
                               (((const unk_D_86002F58_004_000_010 *)loader.workers[w ^ 1])->unk_01 & 2)));
}

static int fx_pending(void) {
    int i;
    for (i = 0; i < 2; ++i)
        if (loader.workers[i] && (((const unk_D_86002F58_004_000_010 *)loader.workers[i])->unk_01 & 2)) return 1;
    return 0;
}

/* C958/C9C0 rewrite the shared request list. */
int sc_batl_fx_request(void) { return !loader.live || !fx_pending(); }

/* begin: 1 for C86C base records, 2 for CA48 results; end: 0. */
int sc_batl_fx_publish(int phase) {
    if (!loader.live) return 1;
    if (phase == 0) {
        sc_fx_publish_end(&fx);
        return 1;
    }
    if (phase == 1) {
        sc_fx_base_begin(&fx);
        return 1;
    }
    return sc_fx_publish_begin(&fx, fx_pending());
}

void sc_batl_fx_note(s32 id, s32 kind) {
    if (loader.live) sc_fx_note(&fx, id, kind);
}

int sc_batl_fx_admit(s32 id, s32 graph) { return !loader.live || sc_fx_admit(&fx, id, graph); }

int sc_batl_scope_map_change(void) {
    return !scope.active && !scope.ticket && !scope.failed;
}
#endif
