#if defined(STORY_SERVICE_CANDIDATE) && defined(STORY_NATIVE_UI)
/* Resident campaign installer: owns the persistent SC mapping, its LIFETIME
   pin and the resident controls. Never imports image symbols; enters the image
   only through sc_persist_call*. */
#include "sc_native_campaign.h"

static ScCampaignControl control;
const ScCampaignControl *const gScCampaignObserverControl = &control;
static const ScNativeOperation zero_operation;
static const ScNativeTerminal zero_terminal;
static const ScNativeTerminalBinding zero_binding;
static unsigned phase, entered;

static int fail(int status) {
    phase = SC_CAMPAIGN_FAILED;
    sc_persist_fail();
    return status == SC_OK || status == SC_PENDING ? SC_INVALID : status;
}

/* Entry during OPEN/STEP requires publication; CLOSE uses the pinned entry so
   retirement can complete after admission closed. */
static int enter(unsigned command, int *status) {
    int ok;
    entered = 1;
    ok = command == SC_CAMPAIGN_CLOSE ?
        sc_persist_call_pinned(SC_PERSIST_PIN_LIFETIME, SC_CAMPAIGN_ENTRY, command, status) :
        sc_persist_call(SC_CAMPAIGN_ENTRY, command, status);
    entered = 0;
    return ok;
}

ScCampaignControl *sc_campaign_bound(void) {
    return entered && (phase == SC_CAMPAIGN_ACTIVE || phase == SC_CAMPAIGN_CLOSING) ?
        &control : 0;
}

int sc_campaign_open(ScMaArena *arena, RlU32 seed) {
    int status;
    if (phase != SC_CAMPAIGN_IDLE || !arena) return SC_INVALID;
    /* The lifetime is never zeroed here: its retirement token may still be
       referenced externally, and its own acquire refuses unsafe reuse. */
    control.operation = zero_operation;
    control.terminal = zero_terminal;
    control.binding = zero_binding;
    control.installed = 0;
    control.arena = arena;
    control.seed = seed;
    if (!sc_persist_acquire(arena)) {
        if (sc_persist_holds_fragment()) phase = SC_CAMPAIGN_FAILED;
        return SC_UNAVAILABLE;
    }
    if (!sc_persist_pin(SC_PERSIST_PIN_LIFETIME)) return fail(SC_INVALID);
    phase = SC_CAMPAIGN_ACTIVE;
    if (!enter(SC_CAMPAIGN_OPEN, &status)) return fail(SC_INVALID);
    if (status != SC_OK) {
        if (!sc_persist_unpublish()) return fail(status);
        phase = SC_CAMPAIGN_CLOSING;
    }
    return status;
}

int sc_campaign_step(void) {
    int status;
    if (phase != SC_CAMPAIGN_ACTIVE) return SC_INVALID;
    if (!enter(SC_CAMPAIGN_STEP, &status)) return fail(SC_INVALID);
    return status;
}

int sc_campaign_observe(const ScBattleSnapshot party[6], unsigned active, unsigned enemy,
                        unsigned hp) {
    int status;
    unsigned i;
    if (phase != SC_CAMPAIGN_ACTIVE || !party) return SC_INVALID;
    for (i = 0; i < 6; ++i) control.observed[i] = party[i];
    control.observed_active = active;
    control.observed_enemy = enemy;
    control.observed_hp = hp;
    if (!enter(SC_CAMPAIGN_OBSERVE, &status)) return fail(SC_INVALID);
    return status;
}

int sc_campaign_fail_stop(void) {
    if (phase == SC_CAMPAIGN_IDLE) return SC_INVALID;
    return fail(SC_INVALID);
}

static int live(const ScCampaignControl *c) {
    const ScNativeLoop *loop = &c->lifetime.loop;
    return (loop->prepared && (!loop->exited || loop->held_operation || loop->queued ||
            loop->drained_operation)) || c->terminal.armed;
}

int sc_campaign_close(void) {
    int status, released;
    switch (phase) {
    case SC_CAMPAIGN_IDLE:
        return SC_OK;
    case SC_CAMPAIGN_ACTIVE:
        if (live(&control)) return SC_PENDING;
        if (!sc_persist_unpublish()) return fail(SC_INVALID);
        phase = SC_CAMPAIGN_CLOSING;
        /* fall through */
    case SC_CAMPAIGN_CLOSING:
        if (!enter(SC_CAMPAIGN_CLOSE, &status)) return fail(SC_INVALID);
        if (status == SC_PENDING) return status;
        if (status != SC_OK || control.installed || control.lifetime.loop.acquired)
            return fail(status);
        if (!sc_persist_unpin(SC_PERSIST_PIN_LIFETIME)) return fail(SC_INVALID);
        phase = SC_CAMPAIGN_RELEASING;
        /* fall through */
    case SC_CAMPAIGN_RELEASING:
        released = sc_persist_release();
        if (released < 0) return fail(SC_INVALID);
        if (!released) return SC_PENDING;
        phase = SC_CAMPAIGN_IDLE;
        return SC_OK;
    }
    return SC_INVALID;
}

int sc_campaign_active(void) { return phase == SC_CAMPAIGN_ACTIVE; }
unsigned sc_campaign_phase(void) { return phase; }
const ScCampaignControl *sc_campaign_control(void) { return &control; }
#endif
