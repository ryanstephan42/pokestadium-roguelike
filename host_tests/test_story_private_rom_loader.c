/* Device/cache/digest/container/import outcomes are explicitly mocked. */
#include "story_private_rom_loader.h"
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    int enqueue, receive, fail_stage;
    bool wrong, no_expected;
    unsigned enqueues, polls, stages, errors;
    int message;
    SprError last;
    bool retained;
} Mock;
static unsigned assertions;
#define CHECK(x) do { ++assertions; if (!(x)) { \
    fprintf(stderr, "%d: %s\n", __LINE__, #x); exit(1); \
} } while (0)

static int enqueue(void *ctx, const SprRequest *r, const void **expected) {
    Mock *m = ctx;
    CHECK(r->ram == 0x400000);
    ++m->enqueues;
    *expected = m->no_expected ? NULL : &m->message;
    return m->enqueue;
}
static int receive(void *ctx, const void **msg) {
    Mock *m = ctx;
    ++m->polls;
    *msg = m->wrong ? m : (void *)&m->message;
    return m->receive;
}
static bool stage(void *ctx, const SprRequest *r) {
    Mock *m = ctx;
    CHECK(r->generation == 7);
    ++m->stages;
    return (int)m->stages != m->fail_stage;
}
static void refusal(void *ctx, SprError e, bool retained) {
    Mock *m = ctx;
    ++m->errors;
    m->last = e;
    m->retained = retained;
}
static SprOps ops = {enqueue, receive, stage, stage, stage, stage, stage,
                     stage, refusal};
static SprRequest valid(void) {
    return (SprRequest){.ram = 0x400000, .bytes = 4096,
        .owner_base = 0x400000, .owner_end = 0x410000,
        .rom_offset = 0x1000, .rom_low = 0, .rom_end = 0x20000,
        .installed_bytes = 0x800000, .generation = 7, .operation = 11,
        .cold_route = true, .exclusive_staging = true,
        .unlocked = true, .cart_handle = true};
}

int main(void) {
    SprRequest r = valid();
    CHECK(spr_preflight(&r));
    for (unsigned field = 0; field < 18; ++field) {
        SprRequest bad = r;
        switch (field) {
            case 0: bad.installed_bytes = 0x400000; break;
            case 1: bad.cold_route = false; break;
            case 2: bad.exclusive_staging = false; break;
            case 3: bad.unlocked = false; break;
            case 4: bad.cart_handle = false; break;
            case 5: bad.generation = 0; break;
            case 6: bad.operation = 0; break;
            case 7: bad.ram++; break;
            case 8: bad.rom_offset++; break;
            case 9: bad.bytes = 1; break;
            case 10: bad.bytes = 0; break;
            case 11: bad.bytes = UINT32_MAX - 1; break;
            case 12: bad.owner_base = 0x3fffff; break;
            case 13: bad.owner_end = 0x7da800; break;
            case 14: bad.rom_end = 0x10000001; break;
            case 15: bad.rom_low = 0x1001; break;
            case 16: bad.ram = 0xa0400000; break;
            case 17: bad.owner_end = bad.ram + 4095; break;
        }
        Mock m = {0};
        SprLoader l = {0};
        CHECK(!spr_start(&l, &bad, &ops, &m));
        CHECK(l.state == SPR_REJECTED && !l.retained);
        CHECK(m.enqueues == 0 && m.stages == 0 && m.errors == 1);
    }
    for (unsigned fail = 0; fail <= 6; ++fail) {
        Mock m = {.receive = 1, .fail_stage = (int)fail};
        SprLoader l = {0};
        bool started = spr_start(&l, &r, &ops, &m);
        CHECK(started == (fail != 1));
        if (started) spr_poll(&l, false);
        CHECK(l.state == (fail ? SPR_FAIL_STOP : SPR_PUBLISHED));
        CHECK(l.retained);
        CHECK(m.stages == (fail ? fail : 6));
        unsigned polls = m.polls, stages = m.stages;
        CHECK(!spr_start(&l, &r, &ops, &m));
        CHECK(spr_poll(&l, true) == l.state);
        CHECK(m.polls == polls && m.stages == stages);
    }
    for (int queued = -2; queued <= 1; ++queued) {
        Mock m = {.enqueue = queued};
        SprLoader l = {0};
        CHECK(spr_start(&l, &r, &ops, &m) == (queued == 0));
        CHECK(m.enqueues == 1);
        if (queued == -1) CHECK(!l.retained && l.state == SPR_REJECTED);
        else CHECK(l.retained);
    }
    for (unsigned fault = 0; fault < 4; ++fault) {
        Mock m = {0};
        SprLoader l = {0};
        CHECK(spr_start(&l, &r, &ops, &m));
        CHECK(spr_poll(&l, false) == SPR_QUEUED);
        CHECK(m.stages == 1);
        if (fault == 0) m.receive = -1;
        if (fault == 1) { m.receive = 1; m.wrong = true; }
        if (fault == 2) m.receive = 2;
        CHECK(spr_poll(&l, fault == 3) == SPR_FAIL_STOP);
        CHECK(m.retained && m.errors == 1 && m.stages == 1);
        CHECK(spr_poll(&l, false) == SPR_FAIL_STOP);
    }
    Mock missing = {.no_expected = true};
    SprLoader l = {0};
    CHECK(!spr_start(&l, &r, &ops, &missing));
    CHECK(l.state == SPR_FAIL_STOP && l.retained);
    Mock delayed = {0};
    SprLoader queued = {0};
    CHECK(spr_start(&queued, &r, &ops, &delayed));
    r.rom_offset = 0; /* Caller request mutation cannot change queued copy. */
    CHECK(queued.request.rom_offset == 0x1000);
    CHECK(spr_poll(&queued, false) == SPR_QUEUED);
    CHECK(!spr_start(&queued, &r, &ops, &delayed));
    CHECK(delayed.enqueues == 1);
    delayed.receive = 1;
    CHECK(spr_poll(&queued, false) == SPR_PUBLISHED);
    CHECK(delayed.stages == 6);
    unsigned polls = delayed.polls;
    CHECK(spr_poll(&queued, false) == SPR_PUBLISHED);
    CHECK(delayed.polls == polls); /* No second post/consume after publication. */
    r = valid();
    Mock late = {.receive = 1};
    SprLoader expired = {0};
    CHECK(spr_start(&expired, &r, &ops, &late));
    CHECK(spr_poll(&expired, true) == SPR_FAIL_STOP);
    CHECK(late.polls == 0 && late.stages == 1);
    SprRequest edge = r;
    edge.ram += 8;
    edge.owner_base += 8;
    CHECK(!spr_preflight(&edge)); /* Rounded cache line not owned. */
    for (unsigned offset = 0; offset < 64; offset += 8) {
        edge = r;
        edge.ram += offset;
        for (unsigned bytes = 2; bytes <= 4096; bytes *= 2) {
            edge.bytes = bytes;
            CHECK(spr_preflight(&edge));
        }
    }
    printf("HOST PRIVATE ROM PASSED: %u assertions\n", assertions);
    return 0;
}
