#include "story_native_stubs.h"
#include "src/story/sc_native_operation.h"
#include "src/story/sc_native_terminal.h"
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
    memset(b->event, 0, sizeof(*b->event));
}

static int event_clean(void) {
    unsigned j;
    for (j = 0; j < sizeof(storage.event); ++j)
        if (((unsigned char *)&storage.event)[j]) return 0;
    return 1;
}

/* Output-only event: every byte written into the clean shared buffer before
   delivery (queued or not, running or held) fail-stops and is erased; retained
   queued identity changes fail-stop; a delivered event is never replayed. */
static void output_mutations(void) {
    ScNativeCampaignBacking *b = &lifetime.loop.backing;
    ScNativeCampaignEvent delivered;
    ScBattleSnapshot party[6];
    unsigned path, byte;
    RlU32 *field;
    for (path = 0; path < 4; ++path) {
        for (byte = 0; byte < sizeof(storage.event); ++byte) {
            initialize();
            assert(sc_native_operation_submit(&operation, b, b->request) == SC_OK);
            snapshots(party);
            if (path)
                assert(sc_native_operation_observe(&operation, b, party, 0, 0, path == 2 ? 0 : 1) == SC_OK);
            assert(event_clean());
            if (path == 3) {
                assert(sc_native_operation_poll(&operation, b, b->event) == SC_OK);
                delivered = storage.event;
                memset(&storage.event, 0, sizeof(storage.event));
                assert(sc_native_operation_poll(&operation, b, b->event) == SC_PENDING && event_clean());
                storage.event = delivered;
                ((unsigned char *)&storage.event)[byte] ^= (unsigned char)(byte & 1);
                assert(sc_native_operation_poll(&operation, b, b->event) == SC_INVALID);
            } else {
                ((unsigned char *)&storage.event)[byte] = 0x5a;
                assert(sc_native_operation_poll(&operation, b, b->event) == SC_INVALID);
            }
            assert(event_clean() && lifetime.memory.failed);
            assert(operation.phase == SC_NATIVE_OPERATION_FAILED);
            assert(sc_native_operation_poll(&operation, b, b->event) == SC_UNAVAILABLE);
        }
    }
    for (path = 0; path < 3; ++path) {
        initialize();
        assert(sc_native_operation_submit(&operation, b, b->request) == SC_OK);
        snapshots(party);
        assert(sc_native_operation_observe(&operation, b, party, 0, 0, 1) == SC_OK);
        field = path == 0 ? &operation.pending.operation_id : &operation.pending.generation;
        if (path == 2) operation.pending.kind = SC_NATIVE_EVENT_LEARNING;
        else ++*field;
        assert(sc_native_operation_poll(&operation, b, b->event) == SC_INVALID);
        assert(event_clean() && lifetime.memory.failed && operation.queued);
    }
    initialize();
    assert(sc_native_operation_submit(&operation, b, b->request) == SC_OK);
    snapshots(party);
    storage.event.enemy_hp = 1;
    assert(sc_native_operation_observe(&operation, b, party, 0, 0, 1) == SC_INVALID);
    assert(lifetime.memory.failed && !operation.queued);
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

static void settle_learning(void) {
    unsigned slot;
    lifetime.loop.held_operation = operation.operation;
    lifetime.loop.held_generation = operation.held_serial;
    lifetime.loop.held_presented = 1;
    while (storage.ui.state.learn_mask)
        for (slot = 0; slot < 6; ++slot)
            if (storage.ui.state.learn_mask & (1U << slot))
                assert(sc_learn_pp(&storage.ui.state, storage.request.progression.pp,
                    slot, 0, 0) == SC_OK);
    assert(sc_native_operation_resume(&operation, &lifetime.loop.backing, operation.operation,
        operation.held_serial, &storage.ui.state, &storage.request.progression) == SC_OK);
}

/* Terminal producer: exactly one DRAINED after settled observations; WIN only
   when the actual sc_finish accepts the settled battle; then IDLE. */
static void drain(void) {
    ScNativeCampaignBacking *b = &lifetime.loop.backing;
    ScBattleSnapshot party[6];
    ScNativeOperation control;
    ScNativeCampaignStorage previous;
    unsigned enemy, outcome;
    for (outcome = 0; outcome < 4; ++outcome) {
        initialize();
        assert(sc_native_operation_drain(&operation, b, SC_LOSS) == SC_INVALID);
        assert(sc_native_operation_submit(&operation, b, b->request) == SC_OK);
        snapshots(party);
        assert(sc_native_operation_observe(&operation, b, party, 0, 0, 1) == SC_OK);
        control = operation; previous = storage;
        assert(sc_native_operation_drain(&operation, b, SC_LOSS) == SC_PENDING);
        assert(!memcmp(&operation, &control, sizeof(control)) && !memcmp(&storage, &previous, sizeof(storage)));
        consume();
        if (outcome == SC_WIN) {
            assert(sc_native_operation_drain(&operation, b, SC_WIN) == SC_INVALID);
            assert(operation.phase == SC_NATIVE_OPERATION_FAILED && lifetime.memory.failed);
            initialize();
            assert(sc_native_operation_submit(&operation, b, b->request) == SC_OK);
            for (enemy = 0; enemy < storage.request.enemy_count; ++enemy) {
                snapshots(party);
                assert(sc_native_operation_observe(&operation, b, party, 0, enemy, 0) == SC_OK);
                if (operation.phase == SC_NATIVE_OPERATION_HELD) {
                    assert(sc_native_operation_drain(&operation, b, SC_WIN) == SC_PENDING);
                    consume();
                    assert(sc_native_operation_drain(&operation, b, SC_WIN) == SC_PENDING);
                    settle_learning();
                } else consume();
            }
        }
        if (outcome == 3) {
            assert(sc_native_operation_drain(&operation, b, 3) == SC_INVALID);
            assert(operation.phase == SC_NATIVE_OPERATION_FAILED);
            continue;
        }
        assert(sc_native_operation_drain(&operation, b, outcome) == SC_OK);
        assert(operation.phase == SC_NATIVE_OPERATION_DRAINING && operation.queued && event_clean());
        assert(operation.pending.kind == SC_NATIVE_EVENT_DRAINED &&
               operation.pending.outcome == outcome && operation.pending.generation == operation.serial);
        assert(sc_native_operation_drain(&operation, b, outcome) == SC_PENDING);
        snapshots(party);
        assert(sc_native_operation_observe(&operation, b, party, 0, 0, 1) == SC_PENDING);
        assert(sc_native_operation_submit(&operation, b, b->request) == SC_PENDING);
        assert(sc_native_operation_poll(&operation, b, b->event) == SC_OK);
        assert(storage.event.kind == SC_NATIVE_EVENT_DRAINED && storage.event.outcome == outcome);
        assert(storage.event.operation_id == 7 && operation.phase == SC_NATIVE_OPERATION_IDLE);
        assert(!operation.operation && operation.last_operation == 7 && !operation.queued);
        assert(sc_native_operation_poll(&operation, b, b->event) == SC_INVALID);
        assert(operation.phase == SC_NATIVE_OPERATION_FAILED && event_clean());
    }
    initialize();
    assert(sc_native_operation_submit(&operation, b, b->request) == SC_OK);
    assert(sc_native_operation_drain(&operation, b, SC_DRAW) == SC_OK);
    operation.pending.outcome = 4;
    assert(sc_native_operation_poll(&operation, b, b->event) == SC_INVALID && event_clean());
    initialize();
    assert(sc_native_operation_submit(&operation, b, b->request) == SC_OK);
    assert(sc_native_operation_drain(&operation, b, SC_LOSS) == SC_OK);
    assert(sc_native_operation_poll(&operation, b, b->event) == SC_OK);
    memset(b->event, 0, sizeof(*b->event));
    assert(sc_native_operation_submit(&operation, b, b->request) == SC_INVALID);
    assert(operation.phase == SC_NATIVE_OPERATION_IDLE);
}

static ScNativeTerminalWitness native;
static unsigned witness_reads;
static int pool_states[2];

void sc_native_terminal_witness(ScNativeTerminalWitness *w) {
    witness_reads++;
    *w = native;
}

static void service_cycle(void) {
    native.starts++; native.drains++; native.revokes++; native.retires++;
}

static void terminal(void) {
    ScNativeCampaignBacking *b = &lifetime.loop.backing;
    ScNativeTerminal t, held;
    ScNativeOperation control;
    ScNativeCampaignStorage previous;
    ScBattleSnapshot party[6];
    unsigned flags, field, enemy;
    static const unsigned expected[4] = { SC_DRAW, SC_LOSS, SC_WIN, SC_DRAW };
    memset(&native, 0, sizeof(native));
    native.scope_idle = 1; native.pool_state = &pool_states[0];
    /* the outcome comes only from raw side flags: equal -> DRAW */
    for (flags = 0; flags < 4; ++flags) {
        unsigned player = flags & 2 ? 5 : 0, opponent = flags & 1 ? 9 : 0;
        initialize(); memset(&t, 0, sizeof(t));
        assert(sc_native_terminal_arm(&t, &operation) == SC_INVALID && t.failed);
        memset(&t, 0, sizeof(t));
        assert(sc_native_operation_submit(&operation, b, b->request) == SC_OK);
        assert(sc_native_terminal_arm(&t, &operation) == SC_OK && t.armed);
        assert(sc_native_terminal_arm(&t, &operation) == SC_INVALID && t.failed);
        memset(&t, 0, sizeof(t));
        assert(sc_native_terminal_arm(&t, &operation) == SC_OK);
        /* no completed service cycle yet: retirement unproven */
        control = operation; previous = storage;
        assert(sc_native_terminal_drain(&t, &operation, b, player, opponent) == SC_INVALID);
        assert(t.failed && !t.armed && !memcmp(&operation, &control, sizeof(control)));
        assert(!memcmp(&storage, &previous, sizeof(storage)));
        memset(&t, 0, sizeof(t));
        assert(sc_native_terminal_arm(&t, &operation) == SC_OK);
        service_cycle();
        if (expected[flags] == SC_WIN) {
            for (enemy = 0; enemy < storage.request.enemy_count; ++enemy) {
                snapshots(party);
                assert(sc_native_operation_observe(&operation, b, party, 0, enemy, 0) == SC_OK);
                if (operation.phase == SC_NATIVE_OPERATION_HELD) {
                    assert(sc_native_terminal_drain(&t, &operation, b, player, opponent) == SC_PENDING);
                    assert(t.armed && !t.failed);
                    consume(); settle_learning();
                } else {
                    assert(sc_native_terminal_drain(&t, &operation, b, player, opponent) == SC_PENDING);
                    consume();
                }
            }
        }
        assert(sc_native_terminal_drain(&t, &operation, b, player, opponent) == SC_OK);
        assert(!t.armed && !t.failed && !t.operation && !t.pool_state);
        assert(operation.pending.kind == SC_NATIVE_EVENT_DRAINED &&
               operation.pending.outcome == expected[flags]);
        /* finish-once: a disarmed authority cannot produce another result */
        control = operation;
        assert(sc_native_terminal_drain(&t, &operation, b, player, opponent) == SC_INVALID);
        assert(!memcmp(&operation, &control, sizeof(control)));
        assert(sc_native_operation_poll(&operation, b, b->event) == SC_OK);
        assert(storage.event.outcome == expected[flags]);
        memset(b->event, 0, sizeof(*b->event));
    }
    /* each witness field independently refuses a forged/incomplete retirement */
    for (field = 0; field < 10; ++field) {
        initialize(); memset(&t, 0, sizeof(t));
        assert(sc_native_operation_submit(&operation, b, b->request) == SC_OK);
        assert(sc_native_terminal_arm(&t, &operation) == SC_OK);
        service_cycle();
        switch (field) {
            case 0: native.drains--; break;
            case 1: native.revokes--; break;
            case 2: native.active = 1; break;
            case 3: native.pending = 1; break;
            case 4: native.failed = 1; break;
            case 5: native.scope_idle = 0; break;
            case 6: native.pool_state = &pool_states[1]; break;
            case 7: native.pool_state = 0; break;
            case 8: service_cycle(); break;
            case 9: t.operation++; break;
        }
        control = operation; previous = storage;
        assert(sc_native_terminal_drain(&t, &operation, b, 1, 0) == SC_INVALID && t.failed);
        assert(!memcmp(&operation, &control, sizeof(control)));
        assert(!memcmp(&storage, &previous, sizeof(storage)));
        assert(sc_native_terminal_drain(&t, &operation, b, 1, 0) == SC_INVALID);
        switch (field) {
            case 0: native.drains++; break;
            case 1: native.revokes++; break;
            case 2: native.active = 0; break;
            case 3: native.pending = 0; break;
            case 4: native.failed = 0; break;
            case 5: native.scope_idle = 1; break;
            case 6: case 7: native.pool_state = &pool_states[0]; break;
            case 8: native.starts--; native.drains--; native.revokes--; native.retires--; break;
            default: break;
        }
        service_cycle();
    }
    /* arming is refused while a service is live or its scope is borrowed */
    initialize(); memset(&t, 0, sizeof(t));
    assert(sc_native_operation_submit(&operation, b, b->request) == SC_OK);
    native.starts++;
    assert(sc_native_terminal_arm(&t, &operation) == SC_INVALID);
    native.starts--;
    memset(&t, 0, sizeof(t)); native.scope_idle = 0;
    assert(sc_native_terminal_arm(&t, &operation) == SC_INVALID);
    native.scope_idle = 1;
    /* a refused operation-level drain fail-stops the authority, not retries */
    memset(&t, 0, sizeof(t));
    assert(sc_native_terminal_arm(&t, &operation) == SC_OK);
    held = t;
    service_cycle();
    lifetime.loop.backing.ui->pending_save = 1;
    assert(sc_native_terminal_drain(&t, &operation, b, 0, 1) == SC_INVALID && t.failed);
    assert(operation.phase == SC_NATIVE_OPERATION_FAILED);
    (void)held;
}

ScNativeLaunchHook *gScNativeLaunchHook;

/* Drives the binding exactly as the rl_battle.c call sites do: arm before the
   RLBT push, drain after the pop with raw flags, abort on init failure. */
static void launch_hook(void) {
    ScNativeCampaignBacking *b = &lifetime.loop.backing;
    ScNativeTerminal t;
    ScNativeTerminalBinding bind, other;
    ScNativeLaunchHook *h;
    ScNativeOperation control;
    memset(&native, 0, sizeof(native));
    native.scope_idle = 1; native.pool_state = &pool_states[0];
    initialize(); memset(&t, 0, sizeof(t)); memset(&bind, 0, sizeof(bind));
    assert(!gScNativeLaunchHook);
    assert(sc_native_terminal_install(&bind, &t, &operation, 0) == SC_INVALID && !gScNativeLaunchHook);
    t.failed = 1;
    assert(sc_native_terminal_install(&bind, &t, &operation, b) == SC_INVALID && !gScNativeLaunchHook);
    t.failed = 0;
    assert(sc_native_terminal_install(&bind, &t, &operation, b) == SC_OK);
    h = gScNativeLaunchHook;
    assert(h == &bind.hook && h->context == &bind && bind.status == SC_PENDING);
    assert(sc_native_terminal_install(&other, &t, &operation, b) == SC_INVALID && gScNativeLaunchHook == h);
    assert(sc_native_terminal_uninstall(&other) == SC_INVALID && gScNativeLaunchHook == h);
    /* arm refuses without a running operation: launcher returns before push */
    assert(h->arm(h->context) != SC_OK && t.failed);
    assert(sc_native_terminal_uninstall(&bind) == SC_OK && !gScNativeLaunchHook);
    /* normal LOSS: arm, one service cycle, drain from raw flags, uninstall */
    memset(&t, 0, sizeof(t));
    assert(sc_native_operation_submit(&operation, b, b->request) == SC_OK);
    assert(sc_native_terminal_install(&bind, &t, &operation, b) == SC_OK);
    h = gScNativeLaunchHook;
    assert(h->arm(h->context) == SC_OK && t.armed && bind.status == SC_OK);
    assert(sc_native_terminal_uninstall(&bind) == SC_INVALID && gScNativeLaunchHook == h);
    service_cycle();
    h->drain(h->context, 0, 3);
    assert(bind.status == SC_OK && bind.drained && !bind.player_alive && bind.enemy_alive);
    assert(operation.pending.kind == SC_NATIVE_EVENT_DRAINED && operation.pending.outcome == SC_LOSS);
    /* a second drain/arm on the consumed binding is stale and cannot reproduce a result */
    control = operation;
    h->drain(h->context, 1, 0);
    assert(bind.status == SC_INVALID && t.failed && !memcmp(&operation, &control, sizeof(control)));
    assert(h->arm(h->context) == SC_INVALID);
    assert(sc_native_terminal_uninstall(&bind) == SC_OK && !gScNativeLaunchHook);
    assert(sc_native_operation_poll(&operation, b, b->event) == SC_OK && storage.event.outcome == SC_LOSS);
    memset(b->event, 0, sizeof(*b->event));
    /* a hook that is no longer installed is stale */
    initialize(); memset(&t, 0, sizeof(t));
    assert(sc_native_operation_submit(&operation, b, b->request) == SC_OK);
    assert(sc_native_terminal_install(&bind, &t, &operation, b) == SC_OK);
    h = gScNativeLaunchHook;
    gScNativeLaunchHook = 0;
    assert(h->arm(h->context) == SC_INVALID && t.failed && !t.armed);
    gScNativeLaunchHook = h;
    assert(sc_native_terminal_uninstall(&bind) == SC_OK);
    /* initialization failure after arm fail-stops without a result */
    memset(&t, 0, sizeof(t));
    assert(sc_native_terminal_install(&bind, &t, &operation, b) == SC_OK);
    h = gScNativeLaunchHook;
    assert(h->arm(h->context) == SC_OK);
    control = operation;
    h->abort(h->context);
    assert(bind.status == SC_INVALID && t.failed && !t.armed && bind.drained);
    assert(!memcmp(&operation, &control, sizeof(control)));
    h->drain(h->context, 1, 0);
    assert(!memcmp(&operation, &control, sizeof(control)));
    assert(sc_native_terminal_uninstall(&bind) == SC_OK && !gScNativeLaunchHook);
    /* drain refused by the witness leaves the operation untouched */
    initialize(); memset(&t, 0, sizeof(t));
    assert(sc_native_operation_submit(&operation, b, b->request) == SC_OK);
    assert(sc_native_terminal_install(&bind, &t, &operation, b) == SC_OK);
    h = gScNativeLaunchHook;
    assert(h->arm(h->context) == SC_OK);
    control = operation;
    h->drain(h->context, 1, 0);
    assert(bind.status == SC_INVALID && t.failed && !memcmp(&operation, &control, sizeof(control)));
    assert(sc_native_terminal_uninstall(&bind) == SC_OK && !gScNativeLaunchHook);
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
        assert(event_clean());
        event = operation.pending;
        assert(event.kind == SC_NATIVE_EVENT_OBSERVE && event.generation);
        assert(sc_native_operation_observe(&operation, b, party, 0, enemy, 0) == SC_PENDING);
        assert(!memcmp(&event, &operation.pending, sizeof(event)) && event_clean());
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
            assert(operation.pending.kind == SC_NATIVE_EVENT_LEARNING && event_clean());
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
    output_mutations();
    drain();
    terminal();
    launch_hook();
    assert(notices);
    puts("NATIVE OPERATION HOST PASSED: canonical copies; all-byte request mutation refusals at submit/observe/poll/resume, held-before-delivery, owner-retained queued identity with all-byte clean-output/replay/identity refusals, single DRAINED producer with WIN/sc_finish and settlement refusals, terminal authority with raw-flag outcome, per-field forged-retirement refusal and finish-once, resident launch-hook binding; lifetime validation and retirement witness injected, no native launch");
    return 0;
}
