#include "story_native_stubs.h"
#include "src/story/sc_native_operation.h"
#include "tools/rltest/story_six_fixture.h"
#include <assert.h>
#include <stdarg.h>

static ScNativeLifetime lifetime;
static ScNativeCampaignStorage storage;
static ScNativeOperation operation;
static unsigned notices;
static int validation;

s32 HAL_Printf(s16 x, s16 y, const char *format, ...) {
    assert(x == 8 && y == 8 && format);
    ++notices;
    return 0;
}

int sc_native_lifetime_validate(void *context, const ScNativeCampaignBacking *backing) {
    assert(context == &lifetime);
    if (backing != &lifetime.loop.backing) return SC_INVALID;
    return validation;
}

static void snapshots(ScBattleSnapshot party[6]) {
    unsigned i, j;
    memset(party, 0, 6 * sizeof(*party));
    for (i = 0; i < storage.ui.state.party_count; ++i) {
        party[i].hp = storage.ui.state.party[i].cur_hp;
        party[i].status = storage.ui.state.party[i].status;
        for (j = 0; j < 4; ++j) party[i].pp[j] = storage.ui.state.party[i].pp[j];
    }
}

static void initialize(void) {
    ScNativeCampaignBacking *b;
    memset(&lifetime, 0, sizeof(lifetime));
    memset(&storage, 0, sizeof(storage));
    memset(&operation, 0, sizeof(operation));
    validation = SC_OK;
    lifetime.live = lifetime.memory.live = 1;
    lifetime.memory.storage = &storage;
    b = &lifetime.loop.backing;
    b->ui = &storage.ui;
    b->driver = &storage.driver;
    b->request = &storage.request;
    b->event = &storage.event;
    b->catalog = &sc_catalog;
    b->lease_id = 1;
    storage.ui.catalog = &sc_catalog;
    assert(sc_six_checkpoint(&storage.ui.state, &sc_catalog, NULL, NULL) == SC_OK);
    storage.ui.state.party[0].level = 14;
    storage.ui.state.party[0].exp = sc_xp(15, storage.ui.state.party[0].growth) - 1;
    storage.ui.state.party[1].level = 8;
    storage.ui.state.party[1].exp = sc_xp(9, storage.ui.state.party[1].growth) - 1;
    sc_recalculate(&storage.ui.state.party[0], 0);
    sc_recalculate(&storage.ui.state.party[1], 0);
    assert(sc_begin(&storage.ui.state, &sc_catalog, 58, 0) == SC_OK);
    storage.ui.operation_id = 7;
    storage.ui.request_ready = 1;
    storage.driver.ui = &storage.ui;
    storage.driver.request = &storage.request;
    storage.driver.operation_id = 7;
    storage.driver.bound = storage.driver.launched = 1;
    assert(sc_ui_battle_request(&storage.ui, &storage.request) == SC_OK);
    storage.ui.bridge.enemy_count = storage.request.enemy_count;
    assert(sc_native_operation_bind(&operation, &lifetime) == SC_OK);
}

static void consume(void) {
    ScNativeCampaignBacking *b = &lifetime.loop.backing;
    assert(sc_native_operation_poll(&operation, b, b->event) == SC_OK);
    assert(sc_ui_driver_observe(b->driver, b->event->operation_id, b->event->party,
        b->event->active_slot, b->event->enemy_slot, b->event->enemy_hp) == SC_OK);
    assert(!memcmp(&storage.ui.state, &storage.settled, sizeof(storage.settled)));
}

static void request_mutations(void) {
    ScNativeCampaignBacking *b = &lifetime.loop.backing;
    ScNativeCampaignStorage previous;
    ScNativeOperation control;
    ScBattleSnapshot party[6];
    unsigned path, byte, slot;
    int status;
    for (path = 0; path < 4; ++path) {
        for (byte = 0; byte < sizeof(storage.request); ++byte) {
            initialize();
            assert(sc_native_operation_submit(&operation, b, b->request) == SC_OK);
            snapshots(party);
            if (path == 2)
                assert(sc_native_operation_observe(&operation, b, party, 0, 0, 1) == SC_OK);
            if (path == 3) {
                assert(sc_native_operation_observe(&operation, b, party, 0, 0, 0) == SC_OK);
                assert(operation.phase == SC_NATIVE_OPERATION_HELD);
                consume();
                lifetime.loop.held_operation = operation.operation;
                lifetime.loop.held_generation = operation.held_serial;
                lifetime.loop.held_presented = 1;
                while (storage.ui.state.learn_mask)
                    for (slot = 0; slot < 6; ++slot)
                        if (storage.ui.state.learn_mask & (1U << slot))
                            assert(sc_learn_pp(&storage.ui.state, storage.request.progression.pp,
                                slot, 0, 0) == SC_OK);
            }
            ((unsigned char *)&storage.request)[byte] ^= 1;
            previous = storage;
            control = operation;
            if (path == 0)
                status = sc_native_operation_submit(&operation, b, b->request);
            else if (path == 1)
                status = sc_native_operation_observe(&operation, b, party, 0, 0, 1);
            else if (path == 2)
                status = sc_native_operation_poll(&operation, b, b->event);
            else
                status = sc_native_operation_resume(&operation, b, operation.operation,
                    operation.held_serial, &storage.ui.state, &storage.request.progression);
            assert(status == SC_INVALID);
            assert(lifetime.memory.failed);
            assert(!memcmp(&storage, &previous, sizeof(storage)));
            control.phase = SC_NATIVE_OPERATION_FAILED;
            assert(!memcmp(&operation, &control, sizeof(control)));
            ((unsigned char *)&storage.request)[byte] ^= 1;
            assert(sc_native_operation_poll(&operation, b, b->event) == SC_UNAVAILABLE);
        }
    }
}

int main(void) {
    ScNativeCampaignBacking *b = &lifetime.loop.backing;
    ScNativeCampaignStorage previous;
    ScNativeCampaignEvent wrong, event;
    ScNativeOperation control;
    ScBattleSnapshot party[6];
    unsigned enemy, slot, learning = 0, j;
    RlU32 serial;
    initialize();
    previous = storage;
    storage.request.enemy[0].level++;
    assert(sc_native_operation_submit(&operation, b, b->request) == SC_INVALID);
    storage.request = previous.request;
    assert(!memcmp(&storage.submitted, &previous.submitted, sizeof(storage.submitted)));
    assert(operation.phase == SC_NATIVE_OPERATION_IDLE && !operation.operation);
    assert(sc_native_operation_submit(&operation, b, b->request) == SC_OK);
    previous = storage;
    assert(sc_native_operation_submit(&operation, b, b->request) == SC_PENDING);
    assert(!memcmp(&storage, &previous, sizeof(storage)));
    assert(sc_native_operation_poll(&operation, b, b->event) == SC_PENDING);
    for (j = 0; j < sizeof(storage.event); ++j)
        assert(!((unsigned char *)&storage.event)[j]);
    assert(sc_native_operation_poll(&operation, b, &wrong) == SC_INVALID);
    assert(sc_native_operation_bind(&operation, &lifetime) == SC_INVALID);
    for (enemy = 0; enemy < storage.request.enemy_count; ++enemy) {
        snapshots(party);
        assert(sc_native_operation_observe(&operation, b, party, 0, enemy, 1) == SC_OK);
        event = storage.event;
        assert(event.kind == SC_NATIVE_EVENT_OBSERVE && event.generation);
        assert(sc_native_operation_observe(&operation, b, party, 0, enemy, 0) == SC_PENDING);
        assert(!memcmp(&event, &storage.event, sizeof(event)));
        consume();
        snapshots(party);
        assert(sc_native_operation_observe(&operation, b, party, 1, enemy, 1) == SC_OK);
        consume();
        snapshots(party);
        assert(sc_native_operation_observe(&operation, b, party, 0, enemy, 0) == SC_OK);
        serial = operation.serial;
        if (operation.phase == SC_NATIVE_OPERATION_HELD) {
            ++learning;
            assert(storage.ui.state.phase == SC_BATTLE);
            assert(storage.event.kind == SC_NATIVE_EVENT_LEARNING);
            assert(sc_native_operation_resume(&operation, b, 7, serial,
                &storage.ui.state, &storage.request.progression) == SC_PENDING);
        }
        consume();
        if (operation.phase == SC_NATIVE_OPERATION_HELD) {
            lifetime.loop.held_operation = 7;
            lifetime.loop.held_generation = serial;
            while (storage.ui.state.learn_mask)
                for (slot = 0; slot < 6; ++slot)
                    if (storage.ui.state.learn_mask & (1U << slot))
                        assert(sc_learn_pp(&storage.ui.state, storage.request.progression.pp,
                            slot, slot % 4, slot % 2) == SC_OK);
            assert(sc_native_operation_resume(&operation, b, 7, serial,
                &storage.ui.state, &storage.request.progression) == SC_PENDING);
            lifetime.loop.held_presented = 1;
            assert(sc_native_operation_resume(&operation, b, 8, serial,
                &storage.ui.state, &storage.request.progression) == SC_INVALID);
            assert(sc_native_operation_resume(&operation, b, 7, serial,
                &storage.ui.state, &storage.request.progression) == SC_OK);
            assert(sc_native_operation_resume(&operation, b, 7, serial,
                &storage.ui.state, &storage.request.progression) == SC_INVALID);
            assert(!memcmp(&storage.ui.state, &storage.settled, sizeof(storage.settled)));
        }
        snapshots(party);
        previous = storage;
        assert(sc_native_operation_observe(&operation, b, party, 0, enemy, 0) == SC_OK);
        assert(operation.serial == serial + 1);
        consume();
        assert(!memcmp(&storage.ui.state, &previous.ui.state, sizeof(storage.ui.state)));
        assert(sc_native_operation_poll(&operation, b, b->event) == SC_PENDING);
    }
    assert(learning);
    control = operation;
    previous = storage;
    validation = SC_UNAVAILABLE;
    assert(sc_native_operation_observe(&operation, b, party, 0, 0, 0) == SC_UNAVAILABLE);
    assert(!memcmp(&operation, &control, sizeof(control)));
    assert(!memcmp(&storage, &previous, sizeof(storage)));
    validation = SC_OK;
    lifetime.memory.live = 0;
    lifetime.memory.storage = NULL;
    assert(sc_native_operation_poll(&operation, b, b->event) == SC_UNAVAILABLE);
    lifetime.memory.live = 1;
    lifetime.memory.storage = &storage;
    operation.serial = ~(RlU32)0;
    assert(sc_native_operation_observe(&operation, b, party, 0, 0, 0) == SC_INVALID);
    assert(operation.phase == SC_NATIVE_OPERATION_FAILED);
    assert(lifetime.memory.failed);
    assert(sc_native_operation_poll(&operation, b, b->event) == SC_UNAVAILABLE);
    initialize();
    assert(sc_native_operation_submit(&operation, b, b->request) == SC_OK);
    snapshots(party);
    assert(sc_native_operation_observe(&operation, b, party, 0, 0, 1) == SC_OK);
    storage.event.party[0].hp--;
    assert(sc_native_operation_poll(&operation, b, b->event) == SC_INVALID);
    assert(operation.phase == SC_NATIVE_OPERATION_FAILED && operation.queued);
    for (j = 0; j < sizeof(storage.event); ++j)
        assert(!((unsigned char *)&storage.event)[j]);
    initialize();
    assert(sc_native_operation_submit(&operation, b, b->request) == SC_OK);
    snapshots(party);
    assert(sc_native_operation_observe(&operation, b, party, 0, 0, 0) == SC_OK);
    assert(operation.phase == SC_NATIVE_OPERATION_HELD);
    consume();
    lifetime.loop.held_operation = 7;
    lifetime.loop.held_generation = operation.serial;
    lifetime.loop.held_presented = 1;
    while (storage.ui.state.learn_mask)
        for (slot = 0; slot < 6; ++slot)
            if (storage.ui.state.learn_mask & (1U << slot))
                assert(sc_learn_pp(&storage.ui.state, storage.request.progression.pp, slot, 3, 0) == SC_OK);
    storage.ui.state.seed++;
    assert(sc_native_operation_resume(&operation, b, 7, operation.serial,
        &storage.ui.state, &storage.request.progression) == SC_INVALID);
    assert(operation.phase == SC_NATIVE_OPERATION_FAILED && operation.held_serial);
    request_mutations();
    assert(notices);
    puts("NATIVE OPERATION HOST PASSED: canonical copies, all-byte accepted-request mutation refusals at submit/observe/poll/resume, held-before-delivery, exact learning replay, monotonic/refusal policy; lifetime validation injected, no native launch/drain");
    return 0;
}
