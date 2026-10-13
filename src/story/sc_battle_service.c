#ifdef STORY_SERVICE_CANDIDATE
#include "sc_battle_service.h"
#include "host_tests/story_real_ai_image.h"
#include "ai_sizes.h"
#include "module-spans.h"

extern OSPiHandle __CartRomHandle;
/* First-init storage only; never clear generations or live leases on reentry. */
static ScMaArena native_arena = {0};
#define SC_SERVICE_BACKING_BYTES ((0x7da800U - 0x400000U - 1U) & ~31U)
static struct {
    ScMaArena *arena;
    ScMaHandle allocation;
    ScMaLease lease;
    ScBatPermit permit;
    ScBatImports imports;
    OSPiHandle *cart;
    void *transport;
    SprLoader loader;
    AiOwner owner;
    AiPublished published;
    ScAiRequest request;
    unsigned active, callable, pending, failed, in_call, initialized, stopping, used, retired;
} service;
volatile ScServiceLifecycle gScServiceLifecycle;
const u32 gScServiceLifecycleSize = sizeof(gScServiceLifecycle);

static void lifecycle_report(void) {
    gScServiceLifecycle.scope_generation = service.imports.generation;
    gScServiceLifecycle.module_generation = service.owner.generation;
    gScServiceLifecycle.pending = service.pending;
    gScServiceLifecycle.active = service.active;
    gScServiceLifecycle.failed = service.failed;
}

static int authentic(void) {
    return service.active && !service.failed && service.cart == &__CartRomHandle &&
        osMemSize == 0x800000 && sc_batl_scope_check(&service.permit) &&
        sc_ma_address(service.arena, service.allocation) != 0;
}

static int fail_stop(void) {
    service.failed = 1;
    if (service.lease.ticket) sc_ma_fail(service.arena, &service.lease);
    sc_batl_scope_fail();
    lifecycle_report();
    return 0;
}

void sc_service_require(int ok) {
    if (ok) return;
    fail_stop();
    osSyncPrintf("story callable service ownership failure\n");
    for (;;) osStopThread(NULL);
}

static uint8_t *map(void *ctx, uint32_t base, uint32_t bytes) {
    (void)ctx;
    if (!authentic() || base != sc_ma_address(service.arena, service.allocation) ||
        bytes != AI_RAM) return 0;
    return sc_ma_map(service.arena, &service.lease, 0, bytes, SC_MA_READ | SC_MA_WRITE);
}

static int request_matches(const SprRequest *r) {
    return authentic() && r && r->generation == service.imports.generation &&
        r->operation == service.owner.generation &&
        ai_owner_admit(service.owner, service.cart, r->generation, r->rom_offset, r->rom_end) &&
        ai_owner_span(service.owner, r->ram, r->owner_end, r->bytes, r->operation);
}

static bool prepare_cache(void *ctx, const SprRequest *r) {
    (void)ctx;
    return request_matches(r) &&
        spr_native_prepare_cache(service.transport, r);
}

static int enqueue(void *ctx, const SprRequest *r, const void **expected) {
    (void)ctx;
    if (!request_matches(r)) return -1;
    return spr_native_enqueue(service.transport, r, expected);
}

static int receive(void *ctx, const void **message) {
    (void)ctx;
    if (!authentic()) return -1;
    return spr_native_receive(service.transport, message);
}

static bool validate(void *ctx, const SprRequest *r) {
    uint8_t *p;
    (void)ctx;
    if (!request_matches(r)) return false;
    p = sc_ma_map(service.arena, &service.lease, 0, r->bytes, SC_MA_READ);
    return ai_image_validate(service.owner, service.cart, r->generation, p, r->bytes);
}

static bool imports(void *ctx, const SprRequest *r) {
    (void)ctx;
    return request_matches(r) &&
        service.imports.native_size == AI_NATIVE_END - 0x84300000U;
}

static bool relocate(void *ctx, const SprRequest *r) {
    uint8_t *p;
    (void)ctx;
    if (!request_matches(r)) return false;
    p = map(0, sc_ma_address(service.arena, service.allocation), AI_RAM);
    return ai_image_relocate(service.owner, service.cart, r->generation,
                            p, r->bytes, p, AI_RAM);
}

static bool sync(void *ctx, uint8_t *p, uint32_t base, uint32_t bytes) {
    unsigned i;
    (void)ctx;
    if (!authentic() || p != map(0, base, bytes)) return false;
    for (i = 0; i < SC_IMAGE_SPAN_COUNT; ++i) {
        const ScMaSpan *span = &sc_image_spans[i];
        osWritebackDCache(p + span->offset, span->size);
        if (span->rights & SC_MA_EXEC)
            osInvalICache(p + span->offset, span->size);
    }
    return authentic();
}

static bool publish(void *ctx, const SprRequest *r) {
    (void)ctx;
    if (!request_matches(r) ||
        !ai_image_publish(service.owner, service.cart, r->generation,
                          sync, 0, &service.published)) return false;
    if (!sc_ma_seal(service.arena, &service.lease, sc_image_spans, SC_IMAGE_SPAN_COUNT))
        return false;
    service.callable = 1;
    return true;
}

static void refusal(void *ctx, SprError error, bool retained) {
    (void)ctx; (void)error; (void)retained;
    /* Even definitive enqueue refusal retains: explicit one-shot owner. */
    fail_stop();
}

int sc_service_start(ScMaArena *arena, OSPiHandle *cart) {
    SprRequest r;
    SprOps ops;
    if ((service.used && (!service.retired || arena != service.arena)) ||
        service.active || service.failed || !arena || cart != &__CartRomHandle ||
        osMemSize != 0x800000 || gScServiceLifecycle.starts == 0xffffffffU) return 0;
    if (!sc_batl_scope_borrow(&service.permit, &service.imports)) return 0;
    service.active = 1;
    service.retired = service.initialized = service.stopping = 0;
    service.arena = arena;
    service.cart = cart;
    gScServiceLifecycle.starts++;
    lifecycle_report();
    if (service.imports.native_size != AI_NATIVE_END - 0x84300000U ||
        !sc_ma_alloc(arena, AI_RAM, &service.allocation) ||
        !sc_ma_staging(arena, service.allocation, &service.lease)) return fail_stop();
    if (!service.used) {
        if (!ai_authority_init(cart, osMemSize, map, 0)) return fail_stop();
        service.used = 1;
    }
    if (!ai_owner_allocate(sc_ma_address(arena, service.allocation),
                           service.imports.native_base, &service.owner) ||
        !ai_owner_bind(service.owner, cart, AI_ROM_START, AI_ROM_END,
                       service.imports.generation)) return fail_stop();
    service.transport = spr_native_transport(cart);
    if (!service.transport) return fail_stop();
    r.ram = r.owner_base = sc_ma_address(arena, service.allocation) & 0x1fffffff;
    r.owner_end = r.owner_base + AI_RAM;
    r.bytes = AI_ROM_BYTES;
    r.rom_offset = r.rom_low = AI_ROM_START;
    r.rom_end = AI_ROM_END;
    r.installed_bytes = osMemSize;
    r.generation = service.imports.generation;
    r.operation = service.owner.generation;
    r.cold_route = r.unlocked = r.cart_handle = true;
    r.exclusive_staging = sc_ma_map(arena, &service.lease, 0, r.bytes, SC_MA_WRITE) != 0;
    ops.enqueue = enqueue; ops.receive = receive; ops.prepare_cache = prepare_cache;
    ops.digest = ops.container = validate; ops.imports = imports;
    ops.relocate = relocate; ops.publish_cache = publish; ops.refusal = refusal;
    lifecycle_report();
    return spr_start(&service.loader, &r, &ops, 0);
}

ScMaArena *sc_service_native_arena(void) {
    if (osMemSize != 0x800000 || service.failed) return NULL;
    if (!native_arena.active) {
        if (service.used || !sc_ma_init(&native_arena, osMemSize,
            (unsigned char *)0x80400000U, SC_SERVICE_BACKING_BYTES)) return NULL;
    } else if (native_arena.end != 0x7da7e0U ||
               native_arena.backing != (unsigned char *)0x80400000U ||
               native_arena.backing_bytes != SC_SERVICE_BACKING_BYTES) return NULL;
    return &native_arena;
}

int sc_service_bootstrap(void) {
    ScMaArena *arena;
    if (service.active) return 0;
    arena = sc_service_native_arena();
    return arena && sc_service_start(arena, &__CartRomHandle);
}

static int dispatch(unsigned command) {
    int result;
#ifndef SC_SERVICE_HOST
    int (*entry)(int, ScAiRequest *);
    void *address;
#endif
    if (!authentic() || !service.callable || service.in_call) return fail_stop();
    service.in_call = 1;
    service.request.generation = service.imports.generation;
#ifdef SC_SERVICE_HOST
    result = sc_ai_entry(command, &service.request);
#else
    address = sc_ma_map(service.arena, &service.lease, AI_ENTRY_OFFSET, 4, SC_MA_EXEC);
    if (!address || (uintptr_t)address != service.published.entry) return fail_stop();
    entry = (int (*)(int, ScAiRequest *))address;
    result = entry(command, &service.request);
#endif
    service.in_call = 0;
    if (!authentic() || result == SC_AI_RESTORE_FAILED) return fail_stop();
    return result == SC_AI_OK;
}

int sc_service_poll(int deadline) {
    if (!authentic() || service.stopping) return -1;
    if (spr_poll(&service.loader, deadline != 0) == SPR_FAIL_STOP) return -1;
    if (service.loader.state != SPR_PUBLISHED) return 0;
    if (!service.initialized) {
        service.request.clock = service.imports.clock;
        if (!dispatch(SC_AI_INIT)) {
            fail_stop();
            return -1;
        }
        service.initialized = 1;
        gScServiceLifecycle.ready++;
        lifecycle_report();
    }
    return 1;
}

static int side_owner(unk_D_84390010 *side) {
    if (side == service.imports.roots[0]) return 0;
    if (side == service.imports.roots[1]) return 1;
    return -1;
}

int sc_service_prepare(unk_D_84390010 *side, unsigned domain, const u8 mapping[6],
                       unsigned count, ScAiSelection *choice) {
    int owner = side_owner(side);
    unsigned i;
    if (!authentic() || !service.initialized || service.pending || service.stopping ||
        owner < 0 || !mapping || !choice) return 0;
    service.request.owner = owner;
    service.request.domain = domain;
    service.request.party_count = count;
    for (i = 0; i < 6; ++i) service.request.mapping[i] = mapping[i];
    if (!dispatch(SC_AI_PREPARE)) return 0;
    service.pending = 1;
    lifecycle_report();
    *choice = service.request.selection;
    return 1;
}

int sc_service_accept(unk_D_84390010 *side, unsigned route) {
    if (!service.pending || side_owner(side) != service.request.owner) return 0;
    service.request.route = route;
    if (!dispatch(SC_AI_ACCEPT)) return 0;
    service.pending = service.request.selection.kind == SC_AI_STRUGGLE ? 2 : 0;
    lifecycle_report();
    return 1;
}

int sc_service_reject(unk_D_84390010 *side, unsigned route) {
    if (service.pending != 1 || side_owner(side) != service.request.owner) return 0;
    service.request.route = route;
    if (!dispatch(SC_AI_REJECT)) return 0;
    service.pending = 0;
    lifecycle_report();
    return 1;
}

int sc_service_struggle(unk_D_84390010 *side) {
    if (service.pending != 2 || side_owner(side) != service.request.owner) return 0;
    if (!dispatch(SC_AI_CONSUME_STRUGGLE)) return 0;
    service.pending = 0;
    lifecycle_report();
    return 1;
}

int sc_service_stop_poll(void) {
    int drain;
    if (!authentic() || service.in_call || service.pending) return -1;
    service.stopping = 1;
    drain = sc_batl_scope_drain(&service.permit);
    if (drain < 0) {
        fail_stop();
        return -1;
    }
    if (!drain) return 0;
    gScServiceLifecycle.drains++;
    if (!dispatch(SC_AI_SHUTDOWN) || !ai_owner_revoke(service.owner)) {
        fail_stop();
        return -1;
    }
    service.callable = 0;
    gScServiceLifecycle.revokes++;
    if (!spr_native_retire(service.transport, &service.loader.request) ||
        !ai_owner_retire(service.owner, &service.published) ||
        !spr_retire(&service.loader, service.imports.generation, service.owner.generation)) {
        fail_stop();
        return -1;
    }
    if (!sc_batl_scope_retire(&service.permit)) {
        fail_stop();
        return -1;
    }
    if (!sc_ma_end(service.arena, &service.lease) ||
        !sc_ma_release(service.arena, service.allocation)) {
        fail_stop();
        return -1;
    }
    service.active = 0;
    service.retired = 1;
    gScServiceLifecycle.retires++;
    lifecycle_report();
    return 1;
}

int sc_service_pending(void) { return service.pending; }
#endif
