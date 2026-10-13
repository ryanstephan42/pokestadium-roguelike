/* Campaign installer + persistent-image owner adapter: actual sc_campaign.c,
   sc_native_campaign.c, sc_persist.c and sc_module_arena.c. Loop, lifetime,
   operation and terminal are recorded stubs (their actual behaviour has its
   own runners); the persist native backend dispatches the image entry. */
#include "src/story/sc_native_campaign.h"
#include "src/story/sc_native_loop.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

const ScCatalog sc_catalog;
ScNativeLaunchHook *gScNativeLaunchHook;
static int pak_session;
static int pak_begin_status = SC_SAVE_OK, pak_end_status = SC_SAVE_OK;
int sc_native_pak_begin(ScNativePak *pak) {
    assert(pak && !pak_session);
    if (pak_begin_status != SC_SAVE_OK) return pak_begin_status;
    pak_session = 1;
    return SC_SAVE_OK;
}
int sc_native_pak_end(ScNativePak *pak) {
    assert(pak && pak_session);
    pak_session = 0;
    return pak_end_status;
}

#ifndef SC_CAMPAIGN_RUN_TEST
int sc_ui_driver_detach_battle(ScUiDriver *driver) {
    (void)driver;
    assert(!"installer-only fixture must not detach a sequential battle");
    return SC_INVALID;
}
int sc_ui_resume_postbattle(ScUi *ui, const ScUiBattleRequest *request,
                            const ScState *state, const RlLaunchResult *result,
                            const ScKoLedger *ledger, unsigned outcome) {
    (void)ui; (void)request; (void)state; (void)result; (void)ledger; (void)outcome;
    assert(!"installer-only fixture must not receive a sequential battle result");
    return SC_INVALID;
}
#endif

#ifdef SC_CAMPAIGN_RUN_TEST
#define BACKING 0x80000U
static union { unsigned char bytes[BACKING]; double align; } store __attribute__((aligned(64)));
#else
#define BACKING 0x4000U
static union { unsigned char bytes[BACKING]; double align; } store;
#endif
static ScMaArena arena;
static int mapped, quiescent = 1, load_ok = 1;
static unsigned loads, unmaps;
static char trace[512];

static void note(const char *s) {
    assert(strlen(trace) + strlen(s) + 2 < sizeof(trace));
    if (*trace) strcat(trace, " ");
    strcat(trace, s);
}
static int seen(const char *s) { return strcmp(trace, s) == 0; }

int sc_persist_native_layout(ScPersistLayout *l) {
    ScPersistLayout v = {0x1000, 0x13a0, 0x100, 0x200, 0x300, 0x3c0};
    *l = v; return 1;
}
int sc_persist_native_unmapped(void) { return !mapped; }
int sc_persist_native_load(unsigned char *image, const ScPersistLayout *l, ScMaU32 n) {
    (void)l; ++loads;
    if (!load_ok) return 0;
    memset(image, 0, n); mapped = 1; return 1;
}
int sc_persist_native_quiescent(void) { return quiescent; }
int sc_persist_native_pool_clear(const unsigned char *image, ScMaU32 n) {
    (void)image; (void)n; return 1;
}
int sc_persist_native_unmap(unsigned char *image, ScMaU32 n) {
    (void)image; (void)n; ++unmaps; mapped = 0; return 1;
}
int sc_persist_native_enter(unsigned char *image, int arg0, unsigned arg1) {
    const ScPersist *p = sc_persist_state();
    assert(mapped && image == p->image && p->depth == 1);
    assert(arg0 == SC_CAMPAIGN_ENTRY && sc_campaign_bound());
    return sc_campaign_dispatch(arg1);
}

/* ---- loop stub: retains the owner like the actual loop ---- */
static int open_status = SC_OK, step_status = SC_OK, close_status = SC_OK;
static int acquire_status = SC_OK, release_status = SC_OK;
int sc_native_loop_open(ScNativeLoop *loop, ScNativeDisplay *display,
                        const ScNativeCampaignOwner *owner) {
    int status;
    note("open");
    assert(display == &sc_campaign_control()->lifetime.display);
    loop->prepared = loop->exited = loop->queued = 0;
    loop->held_operation = loop->held_generation = loop->drained_operation = 0;
    loop->owner = *owner; loop->display = display;
    if (open_status != SC_OK) return open_status;
    status = owner->acquire(owner->context, &loop->backing);
    if (status == SC_OK) loop->acquired = 1;
    return status;
}
/* Runs inside the image entry, as the actual loop drives owner callbacks. */
static int (*during_step)(void);
int sc_native_loop_step(ScNativeLoop *loop) {
    (void)loop; note("step");
    return during_step ? during_step() : step_status;
}
int sc_native_loop_close(ScNativeLoop *loop) {
    int status;
    note("close");
    if (close_status != SC_OK) return close_status;
    status = loop->owner.release(loop->owner.context, &loop->backing);
    if (status == SC_OK) loop->acquired = loop->prepared = 0;
    return status;
}

/* ---- lifetime ---- */
static void (*on_lifetime_acquire)(void), (*on_lifetime_release)(void);
int sc_native_lifetime_acquire(ScNativeLifetime *l, ScMaArena *a, const ScCatalog *c,
                               RlU32 seed, ScNativeCampaignBacking *b) {
    note("acquire");
    assert(l == &sc_campaign_control()->lifetime && a == &arena && c == &sc_catalog);
    assert(seed == 77 && b == &l->loop.backing);
    if (acquire_status == SC_OK) {
        l->live = 1;
        if (on_lifetime_acquire) on_lifetime_acquire();
    }
    return acquire_status;
}
int sc_native_lifetime_validate(void *p, const ScNativeCampaignBacking *b) {
    (void)p; (void)b; return SC_OK;
}
int sc_native_lifetime_retire(void *p, const ScNativeCampaignBacking *b,
                              const unk_D_80068BB0 *i) {
    (void)p; (void)b; (void)i; note("retire"); return SC_OK;
}
int sc_native_lifetime_release(void *p, const ScNativeCampaignBacking *b) {
    ScNativeLifetime *l = p;
    (void)b; note("release");
    assert(l == &sc_campaign_control()->lifetime);
    /* the image is still mapped and the LIFETIME pin still held */
    assert(mapped && sc_persist_state()->pins[SC_PERSIST_PIN_LIFETIME] == 1);
    if (release_status == SC_OK) {
        if (on_lifetime_release) on_lifetime_release();
        l->live = 0;
    }
    return release_status;
}

/* ---- operation ---- */
/* Optional scripting hooks for the connected runner test (null here). */
static void (*on_submit)(void), (*on_poll)(void), (*on_resume)(void), (*on_observe)(void);
static int submit_status = SC_OK, bind_status = SC_OK, drain_status = SC_OK;
int sc_native_operation_bind(ScNativeOperation *o, ScNativeLifetime *l) {
    note("bind");
    assert(o == &sc_campaign_control()->operation && l == &sc_campaign_control()->lifetime);
    return bind_status;
}
int sc_native_operation_submit(ScNativeOperation *o, const ScNativeCampaignBacking *b,
                                const ScUiBattleRequest *r) {
    (void)o; (void)b; (void)r;
    note("submit");
    /* the hook is already installed when the request is accepted */
    assert(sc_campaign_control()->installed && gScNativeLaunchHook);
    if (submit_status == SC_OK && on_submit) on_submit();
    return submit_status;
}
int sc_native_operation_poll(ScNativeOperation *o, const ScNativeCampaignBacking *b,
                              ScNativeCampaignEvent *e) {
    (void)o; (void)b; (void)e; note("poll");
    if (on_poll) on_poll();
    return SC_OK;
}
int sc_native_operation_resume(ScNativeOperation *o, const ScNativeCampaignBacking *b,
                                RlU32 id, RlU32 g, const ScState *s, const ScCombat *c) {
    (void)o; (void)b; (void)id; (void)g; (void)s; (void)c; note("resume");
    if (on_resume) on_resume();
    return SC_OK;
}

static int observe_status = SC_OK;
static ScBattleSnapshot observed_party[6];
static unsigned observed_values[3];
int sc_native_operation_observe(ScNativeOperation *o, const ScNativeCampaignBacking *b,
                                const ScBattleSnapshot party[6], unsigned active,
                                unsigned enemy, unsigned hp) {
    note("observe");
    assert(o == &sc_campaign_control()->operation);
    assert(b == &sc_campaign_control()->lifetime.loop.backing);
    /* the resident staging is passed through unchanged */
    assert(party == sc_campaign_control()->observed);
    memcpy(observed_party, party, sizeof(observed_party));
    observed_values[0] = active; observed_values[1] = enemy; observed_values[2] = hp;
    if (observe_status == SC_OK && on_observe) on_observe();
    return observe_status;
}

/* ---- terminal ---- */
static int install_status = SC_OK;
static unsigned drains;
int sc_native_terminal_install(ScNativeTerminalBinding *b, ScNativeTerminal *t,
                               ScNativeOperation *o, const ScNativeCampaignBacking *k) {
    note("install");
    if (install_status != SC_OK) return install_status;
    assert(!gScNativeLaunchHook && sc_persist_pin(SC_PERSIST_PIN_HOOK));
    b->terminal = t; b->operation = o; b->backing = k;
    b->drained = 0; b->status = SC_PENDING;
    gScNativeLaunchHook = &b->hook;
    return SC_OK;
}
int sc_native_terminal_uninstall(ScNativeTerminalBinding *b) {
    note("uninstall");
    if (gScNativeLaunchHook != &b->hook || b->terminal->armed) return SC_INVALID;
    gScNativeLaunchHook = 0;
    assert(sc_persist_unpin(SC_PERSIST_PIN_HOOK));
    return SC_OK;
}
int sc_native_terminal_drain(ScNativeTerminal *t, ScNativeOperation *o,
                             const ScNativeCampaignBacking *b, unsigned p, unsigned e) {
    (void)o; (void)b;
    ++drains; note("drain");
    assert(p == 1 && e == 0);
    if (drain_status == SC_OK) t->armed = 0;
    return drain_status;
}

static ScCampaignControl *ctl(void) { return (ScCampaignControl *)sc_campaign_control(); }
static const ScNativeCampaignOwner *owner(void) { return &ctl()->lifetime.loop.owner; }

static void reset(void) {
    *trace = 0;
    open_status = step_status = close_status = acquire_status = release_status = SC_OK;
    submit_status = bind_status = drain_status = install_status = observe_status = SC_OK;
    quiescent = load_ok = 1;
}

static void open_ok(void) {
    reset();
    assert(sc_campaign_open(&arena, 77) == SC_OK && sc_campaign_active());
    assert(seen("open acquire bind") && mapped);
    assert(sc_persist_state()->phase == SC_PERSIST_PUBLISHED);
    assert(sc_persist_state()->pins[SC_PERSIST_PIN_LIFETIME] == 1);
    *trace = 0;
}

static void close_ok(void) {
    unsigned before = unmaps;
    assert(sc_campaign_close() == SC_OK && sc_campaign_phase() == SC_CAMPAIGN_IDLE);
    assert(!mapped && unmaps == before + 1 && !sc_persist_holds_fragment());
    assert(!sc_persist_state()->pins[SC_PERSIST_PIN_LIFETIME]);
}

static int submit_now(void) {
    ScUiBattleRequest r; memset(&r, 0, sizeof(r));
    return owner()->submit(owner()->context, &ctl()->lifetime.loop.backing, &r);
}
static int poll_now(void) {
    ScNativeCampaignEvent e; memset(&e, 0, sizeof(e));
    return owner()->poll(owner()->context, &ctl()->lifetime.loop.backing, &e);
}
static int step_with(int (*fn)(void)) {
    int status;
    during_step = fn; *trace = 0;
    status = sc_campaign_step();
    during_step = 0;
    return status;
}
/* resident rl_battle invoking the installed hook (outside any image entry) */
static void battle(unsigned player, unsigned enemy) {
    ScNativeTerminalBinding *b = &ctl()->binding;
    assert(gScNativeLaunchHook == &b->hook);
    b->terminal->armed = 1;
    b->drained = 1; b->player_alive = player; b->enemy_alive = enemy;
    b->status = drain_status;
    /* OK retires the authority; a refusal fail-stops it (armed cleared) */
    if (drain_status != SC_PENDING) b->terminal->armed = 0;
}

static void lifecycle(void) {
    ScNativeLoop *loop = &ctl()->lifetime.loop;
    ScUiBattleRequest request;
    memset(&request, 0, sizeof(request));
    /* no arena (ordinary 4 MiB): refused without mapping anything */
    assert(sc_campaign_open(NULL, 77) == SC_INVALID && !sc_persist_holds_fragment());
    assert(sc_campaign_step() == SC_INVALID && sc_campaign_close() == SC_OK);
    open_ok();
    assert(sc_campaign_open(&arena, 77) == SC_INVALID && loads == 1);
    assert(step_with(0) == SC_OK && seen("step"));
    /* callbacks outside an image entry have no bound control */
    assert(!sc_campaign_bound());
    assert(owner()->submit(owner()->context, &loop->backing, &request) == SC_INVALID);
    assert(owner()->poll(owner()->context, &loop->backing, NULL) == SC_INVALID);
    /* refused install owns no request and pins nothing */
    install_status = SC_INVALID;
    assert(step_with(submit_now) == SC_INVALID && seen("step install"));
    assert(!ctl()->installed && !sc_persist_state()->pins[SC_PERSIST_PIN_HOOK]);
    install_status = SC_OK;
    /* refused submit uninstalls the unarmed hook again */
    submit_status = SC_UNAVAILABLE;
    assert(step_with(submit_now) == SC_UNAVAILABLE && seen("step install submit uninstall"));
    assert(!ctl()->installed && !gScNativeLaunchHook && !sc_persist_state()->pins[SC_PERSIST_PIN_HOOK]);
    submit_status = SC_OK;
    /* accepted submit: hook installed (HOOK pin) before the request is copied */
    assert(step_with(submit_now) == SC_OK && seen("step install submit"));
    assert(ctl()->installed && sc_persist_state()->pins[SC_PERSIST_PIN_HOOK] == 1);
    assert(step_with(submit_now) == SC_INVALID && seen("step"));
    /* close refused to begin while the battle is armed: nothing changes */
    ctl()->terminal.armed = 1;
    assert(sc_campaign_close() == SC_PENDING && sc_campaign_active());
    assert(sc_persist_state()->phase == SC_PERSIST_PUBLISHED);
    ctl()->terminal.armed = 0;
    /* live UI likewise holds close before unpublication */
    loop->prepared = 1; loop->exited = 0;
    assert(sc_campaign_close() == SC_PENDING && sc_campaign_active());
    loop->exited = 1; loop->queued = 1;
    assert(sc_campaign_close() == SC_PENDING && sc_campaign_active());
    loop->queued = 0; loop->prepared = 0;
    /* PENDING raw-flag drain is retried by the owner with recorded flags */
    drain_status = SC_PENDING; battle(1, 0);
    assert(step_with(poll_now) == SC_OK && seen("step drain poll"));
    assert(ctl()->installed && ctl()->binding.status == SC_PENDING);
    drain_status = SC_OK;
    assert(step_with(poll_now) == SC_OK && seen("step drain uninstall poll"));
    assert(!ctl()->installed && !gScNativeLaunchHook && drains == 2);
    assert(!sc_persist_state()->pins[SC_PERSIST_PIN_HOOK]);
    /* settled: further polls do not retry or replay */
    assert(step_with(poll_now) == SC_OK && seen("step poll") && drains == 2);
    /* teardown: unpublish first, retire through the pinned entry, release */
    *trace = 0; release_status = SC_PENDING;
    assert(sc_campaign_close() == SC_PENDING && seen("close release"));
    assert(sc_campaign_phase() == SC_CAMPAIGN_CLOSING && !sc_campaign_active());
    assert(sc_persist_state()->phase == SC_PERSIST_DRAINING && mapped);
    /* closing: no new image entry, pin or STEP admission */
    assert(sc_campaign_step() == SC_INVALID && !sc_persist_pin(SC_PERSIST_PIN_SERVICE));
    assert(sc_persist_state()->pins[SC_PERSIST_PIN_LIFETIME] == 1);
    release_status = SC_OK; *trace = 0;
    quiescent = 0;
    assert(sc_campaign_close() == SC_PENDING && seen("close release"));
    assert(sc_campaign_phase() == SC_CAMPAIGN_RELEASING && mapped);
    assert(!sc_persist_state()->pins[SC_PERSIST_PIN_LIFETIME]);
    /* releasing never re-enters the image */
    *trace = 0;
    assert(sc_campaign_close() == SC_PENDING && seen("") && mapped);
    quiescent = 1;
    close_ok();
    assert(sc_campaign_close() == SC_OK && sc_campaign_step() == SC_INVALID);
    /* reopen in the same boot: fresh mapping, operation/terminal reset */
    open_ok();
    assert(loads == 2 && !ctl()->installed && !ctl()->binding.terminal);
    assert(sc_campaign_close() == SC_OK);
}

static void teardown_with_hook(void) {
    /* an installed, never-armed hook is uninstalled by release before the
       lifetime retires; an armed one keeps release PENDING */
    open_ok();
    assert(step_with(submit_now) == SC_OK);
    ctl()->binding.terminal->armed = 1;
    *trace = 0;
    /* (armed is checked before unpublication) */
    assert(sc_campaign_close() == SC_PENDING && sc_campaign_active());
    ctl()->binding.terminal->armed = 0;
    *trace = 0;
    assert(sc_campaign_close() == SC_OK && seen("close uninstall release"));
    assert(!gScNativeLaunchHook && !mapped);
}

static void refused_drain(void) {
    /* a refused arm/drain/abort keeps the hook installed: the HOOK pin
       retains the image and the lifetime is never released under it */
    open_ok();
    assert(step_with(submit_now) == SC_OK);
    drain_status = SC_INVALID; battle(1, 0);
    assert(step_with(poll_now) == SC_INVALID && ctl()->installed);
    assert(sc_campaign_close() == SC_INVALID && sc_campaign_phase() == SC_CAMPAIGN_FAILED);
    assert(sc_persist_state()->phase == SC_PERSIST_FAILED && mapped);
    assert(sc_persist_holds_fragment() && sc_campaign_close() == SC_INVALID);
    assert(sc_campaign_open(&arena, 77) == SC_INVALID);
}

static void refused_open(void) {
    unsigned before = unmaps;
    reset();
    /* refused lifetime acquire: CLOSING, nothing acquired, release follows */
    acquire_status = SC_UNAVAILABLE;
    assert(sc_campaign_open(&arena, 77) == SC_UNAVAILABLE && seen("open acquire"));
    assert(sc_campaign_phase() == SC_CAMPAIGN_CLOSING && !sc_campaign_active());
    assert(sc_persist_state()->phase == SC_PERSIST_DRAINING);
    *trace = 0;
    assert(sc_campaign_close() == SC_OK && seen("") && unmaps == before + 1 && !mapped);
    /* refused bind after acquire: close drives loop close then release */
    reset(); bind_status = SC_INVALID;
    assert(sc_campaign_open(&arena, 77) == SC_INVALID && seen("open acquire bind"));
    *trace = 0;
    assert(sc_campaign_close() == SC_OK && seen("close release") && !mapped);
}

static void refused_load(void) {
    /* refused image load: persist failure retains and the campaign fails */
    reset(); load_ok = 0;
    assert(sc_campaign_open(&arena, 77) == SC_UNAVAILABLE);
    assert(sc_campaign_phase() == SC_CAMPAIGN_FAILED && sc_persist_holds_fragment());
}

static void observe(void) {
    ScBattleSnapshot party[6];
    unsigned i;
    memset(party, 0, sizeof(party));
    for (i = 0; i < 6; ++i) party[i].hp = (RlU16)(10 + i);
    /* not open: refused without entering the image */
    reset();
    assert(sc_campaign_observe(party, 0, 0, 5) == SC_INVALID && seen(""));
    open_ok();
    /* null party refused before staging */
    assert(sc_campaign_observe(NULL, 0, 0, 5) == SC_INVALID && seen(""));
    /* no installed operation hook: the adapter refuses */
    assert(sc_campaign_observe(party, 1, 2, 5) == SC_INVALID && seen(""));
    assert(sc_campaign_active());
    assert(step_with(submit_now) == SC_OK);
    *trace = 0;
    assert(sc_campaign_observe(party, 1, 2, 5) == SC_OK && seen("observe"));
    assert(!memcmp(observed_party, party, sizeof(party)));
    assert(observed_values[0] == 1 && observed_values[1] == 2 && observed_values[2] == 5);
    /* an operation refusal is reported; the installer stays active */
    observe_status = SC_PENDING; *trace = 0;
    assert(sc_campaign_observe(party, 1, 2, 0) == SC_PENDING && sc_campaign_active());
    /* launcher fail-stop retains mapping, pins and backing */
    assert(sc_campaign_fail_stop() == SC_INVALID);
    assert(sc_campaign_phase() == SC_CAMPAIGN_FAILED && mapped);
    assert(sc_persist_state()->phase == SC_PERSIST_FAILED && sc_persist_holds_fragment());
    assert(sc_campaign_observe(party, 1, 2, 5) == SC_INVALID && sc_campaign_step() == SC_INVALID);
    assert(sc_campaign_close() == SC_INVALID && mapped);
}

static void fail_stop_idle(void) {
    reset();
    assert(sc_campaign_fail_stop() == SC_INVALID && sc_campaign_phase() == SC_CAMPAIGN_IDLE);
    assert(!sc_persist_holds_fragment());
}

typedef void (*Scenario)(void);
static void run(Scenario fn, const char *name) {
    pid_t pid = fork();
    int status;
    assert(pid >= 0);
    if (!pid) {
        assert(sc_ma_init(&arena, 0x800000U, store.bytes, BACKING));
        fn();
        _exit(0);
    }
    assert(waitpid(pid, &status, 0) == pid && WIFEXITED(status) && !WEXITSTATUS(status));
    printf("  %s\n", name);
}

#ifndef SC_CAMPAIGN_RUN_TEST
int main(void) {
    run(lifecycle, "lifecycle: open/step, hook-before-submit, held close, drain retry, unpublish-first teardown, reopen");
    run(teardown_with_hook, "teardown: unarmed hook uninstalled before lifetime release");
    run(refused_open, "refused open: acquire/bind refusal retires and releases");
    run(refused_load, "refused load: persist failure retains, campaign failed");
    run(refused_drain, "refused drain: hook stays installed, mapping retained, campaign failed");
    run(observe, "observe: resident staging through the published entry, installed-only, fail-stop retains");
    run(fail_stop_idle, "fail-stop while idle: refused, nothing mapped");
    puts("STORY CAMPAIGN HOST PASSED");
    return 0;
}
#endif
