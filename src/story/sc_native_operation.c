#ifdef STORY_NATIVE_UI
#include "sc_native_operation.h"

static int equal(const void *a, const void *b, unsigned size) {
    const unsigned char *left = a, *right = b;
    unsigned i;
    for (i = 0; i < size; ++i)
        if (left[i] != right[i]) return 0;
    return 1;
}

static void clear(ScNativeCampaignEvent *event) {
    unsigned char *bytes = (unsigned char *)event;
    unsigned i;
    for (i = 0; i < sizeof(*event); ++i) bytes[i] = 0;
}

static int refusal(int status, const char *message) {
    HAL_Printf(8, 8, "%s", message);
    return status;
}

static int live(ScNativeOperation *operation, const ScNativeCampaignBacking *backing) {
    ScNativeLifetime *owner;
    int status;
    if (!operation || !(owner = operation->lifetime))
        return refusal(SC_INVALID, "Story operation: lifetime binding unavailable");
    status = sc_native_lifetime_validate(owner, backing);
    if (status != SC_OK) return status;
    if (!owner->memory.live || !owner->memory.storage || owner->retirement.backing_retired)
        return refusal(SC_UNAVAILABLE, "Story operation: DATA storage retired");
    if (operation->phase == SC_NATIVE_OPERATION_FAILED)
        return refusal(SC_UNAVAILABLE, "Story operation: failed transaction retained");
    return SC_OK;
}

static int fail(ScNativeOperation *operation, const char *message) {
    operation->phase = SC_NATIVE_OPERATION_FAILED;
    operation->lifetime->memory.failed = 1;
    return refusal(SC_INVALID, message);
}

int sc_native_operation_bind(ScNativeOperation *operation, ScNativeLifetime *owner) {
    int status;
    if (!operation || !owner || operation->lifetime || operation->phase || operation->queued)
        return refusal(SC_INVALID, "Story operation: control cannot be rebound");
    status = sc_native_lifetime_validate(owner, &owner->loop.backing);
    if (status != SC_OK) return status;
    if (!owner->memory.live || !owner->memory.storage || owner->retirement.backing_retired)
        return refusal(SC_UNAVAILABLE, "Story operation: live DATA storage required");
    operation->lifetime = owner;
    return SC_OK;
}

int sc_native_operation_submit(ScNativeOperation *operation,
                                const ScNativeCampaignBacking *backing,
                                const ScUiBattleRequest *request) {
    ScUiBattleRequest canonical;
    ScNativeCampaignStorage *storage;
    int status = live(operation, backing);
    if (status != SC_OK) return status;
    if (operation->phase != SC_NATIVE_OPERATION_IDLE || operation->queued)
        return refusal(SC_PENDING, "Story operation: previous battle still owns work");
    if (!request || request != backing->request || !backing->driver->launched ||
        backing->driver->operation_id != request->operation_id ||
        !request->operation_id || request->operation_id <= operation->last_operation ||
        sc_ui_battle_request(backing->ui, &canonical) != SC_OK ||
        !equal(request, &canonical, sizeof(canonical)))
        return refusal(SC_INVALID, "Story operation: checkpointed request changed");
    storage = operation->lifetime->memory.storage;
    storage->submitted = *request;
    storage->settled = backing->ui->state;
    storage->progression = request->progression;
    operation->bridge = backing->ui->bridge;
    operation->operation = operation->last_operation = request->operation_id;
    operation->serial = operation->held_serial = 0;
    operation->phase = SC_NATIVE_OPERATION_RUNNING;
    clear(backing->event);
    return SC_OK;
}

int sc_native_operation_observe(ScNativeOperation *operation,
                                 const ScNativeCampaignBacking *backing,
                                 const ScBattleSnapshot party[6], unsigned active,
                                 unsigned enemy, unsigned hp) {
    ScState next;
    ScBridge bridge;
    ScNativeCampaignStorage *storage;
    ScNativeCampaignEvent *event;
    unsigned i;
    int status = live(operation, backing);
    if (status != SC_OK) return status;
    if (operation->queued || operation->phase == SC_NATIVE_OPERATION_HELD)
        return refusal(SC_PENDING, "Story operation: observation or learning remains held");
    if (operation->phase != SC_NATIVE_OPERATION_RUNNING)
        return refusal(SC_INVALID, "Story operation: no submitted battle");
    storage = operation->lifetime->memory.storage;
    if (operation->serial == ~(RlU32)0)
        return fail(operation, "Story operation: event serial exhausted");
    if (!backing->driver->launched || backing->driver->operation_id != operation->operation ||
        backing->ui->operation_id != operation->operation ||
        backing->ui->pending_save || !backing->ui->request_ready ||
        !equal(&backing->ui->state, &storage->settled, sizeof(storage->settled)) ||
        !equal(&backing->ui->bridge, &operation->bridge, sizeof(operation->bridge)))
        return fail(operation, "Story operation: observation/progression ownership changed");
    next = storage->settled;
    bridge = operation->bridge;
    status = sc_combat_observe(&next, &bridge, &storage->progression, party, active, enemy, hp);
    if (status != SC_OK)
        return fail(operation, "Story operation: native observation rejected");
    if (next.phase != SC_BATTLE && next.phase != SC_LEARNING)
        return fail(operation, "Story operation: unsupported observed phase");
    event = backing->event;
    clear(event);
    for (i = 0; i < 6; ++i) event->party[i] = party[i];
    event->operation_id = operation->operation;
    event->generation = ++operation->serial;
    event->active_slot = active;
    event->enemy_slot = enemy;
    event->enemy_hp = hp;
    event->kind = next.phase == SC_LEARNING ?
        SC_NATIVE_EVENT_LEARNING : SC_NATIVE_EVENT_OBSERVE;
    storage->settled = next;
    operation->bridge = bridge;
    if (event->kind == SC_NATIVE_EVENT_LEARNING) {
        operation->held_serial = event->generation;
        operation->phase = SC_NATIVE_OPERATION_HELD;
    }
    operation->pending = *event;
    operation->queued = 1;
    return SC_OK;
}

int sc_native_operation_poll(ScNativeOperation *operation,
                              const ScNativeCampaignBacking *backing,
                              ScNativeCampaignEvent *event) {
    int status = live(operation, backing);
    if (status != SC_OK) return status;
    if (!event || event != backing->event)
        return refusal(SC_INVALID, "Story operation: event destination changed");
    if (!operation->queued) {
        clear(event);
        return SC_PENDING;
    }
    if (!equal(event, &operation->pending, sizeof(*event))) {
        clear(event);
        return fail(operation, "Story operation: queued event changed");
    }
    operation->queued = 0;
    return SC_OK;
}

int sc_native_operation_resume(ScNativeOperation *operation,
                                const ScNativeCampaignBacking *backing,
                                RlU32 id, RlU32 serial,
                                const ScState *settled, const ScCombat *progression) {
    ScState trial;
    ScNativeCampaignStorage *storage;
    unsigned i, choice;
    int matched, status = live(operation, backing);
    if (status != SC_OK) return status;
    if (operation->phase != SC_NATIVE_OPERATION_HELD || !operation->held_serial ||
        id != operation->operation || serial != operation->held_serial)
        return refusal(SC_INVALID, "Story operation: stale learning resume");
    if (operation->queued)
        return refusal(SC_PENDING, "Story operation: learning event not delivered");
    if (operation->lifetime->loop.held_operation != id ||
        operation->lifetime->loop.held_generation != serial ||
        !operation->lifetime->loop.held_presented)
        return refusal(SC_PENDING, "Story operation: learning not presented by admitted loop");
    storage = operation->lifetime->memory.storage;
    if (!settled || !progression || backing->ui->pending_save ||
        !backing->driver->launched || backing->driver->operation_id != id ||
        backing->ui->operation_id != id || !backing->ui->request_ready ||
        settled->phase != SC_BATTLE || settled->learn_mask ||
        !equal(settled, &backing->ui->state, sizeof(*settled)) ||
        !equal(progression, &storage->progression, sizeof(*progression)) ||
        !equal(&backing->ui->bridge, &operation->bridge, sizeof(operation->bridge)))
        return fail(operation, "Story operation: unsettled learning resume");
    for (i = 0; i < storage->settled.party_count; ++i) {
        if (!(storage->settled.learn_mask & (1U << i))) continue;
        matched = 0;
        for (choice = 0; choice < 5; ++choice) {
            trial = storage->settled;
            status = sc_learn_pp(&trial, storage->progression.pp, i,
                                 choice ? choice - 1 : 0, choice != 0);
            if (status == SC_OK &&
                equal(&trial.party[i], &settled->party[i], sizeof(trial.party[i])) &&
                trial.learn_moves[i] == settled->learn_moves[i]) {
                storage->settled = trial;
                matched = 1;
                break;
            }
        }
        if (!matched)
            return fail(operation, "Story operation: incompatible learning settlement");
    }
    if (!equal(&storage->settled, settled, sizeof(*settled)))
        return fail(operation, "Story operation: settlement changed campaign values");
    operation->held_serial = 0;
    operation->phase = SC_NATIVE_OPERATION_RUNNING;
    return SC_OK;
}
#endif
