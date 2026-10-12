/* Connected campaign-runner HOST test: actual sc_campaign_run.c (Game_Thread
   state, Menu Select hook, launcher, fragment-62 gate/after/paused tick) on
   the actual installer sc_campaign.c, owner adapter sc_native_campaign.c,
   persistent loader sc_persist.c and arena sc_module_arena.c.

   SUBSTITUTED SEAMS (each has its own actual-unit runner):
   - loop: recorded stub driven by loop_script() below, which drives the real
     owner callbacks (submit/poll/resume) the way sc_native_loop_step does
     (actual loop: run_story_campaign_lifetime.sh);
   - lifetime / operation / terminal install/drain: recorded stubs from
     test_story_campaign.c, with scripted RUNNING/HELD/IDLE transitions
     (actual: run_story_native_operation.sh, terminal binding tests);
   - rl_start_battle: battle() below maps, arms/drains/aborts the installed
     hook binding as hook_arm/hook_drain/hook_abort do and runs scripted
     gate/after frames on host battle sides (actual: six-runtime runner);
   - fragment-62 records, controller, Game_Thread state, mapping/capture/
     publish and the service arena: story_campaign_run_seams.h;
   - learning/terminal/save: the drained poll clears the launched driver and
     counts a save (actual: ui_driver / native_operation runners). */
#define SC_CAMPAIGN_RUN_TEST
#include "test_story_campaign.c"
#include "src/story/sc_campaign_battle.h"

int gScProbeActive;
RlU8 gScProbeSixMapping[6];
static Controller pad;
Controller *gPlayer1Controller = &pad;
s32 gCurrentGameState;
u32 osGetCount(void) { return 77; }
u32 osMemSize = 0x800000;
static char hint[64];
static int print_open;
void func_8001F3F4(void) { assert(!print_open); print_open = 1; }
void func_8001EBE0(s32 font, s32 arg1) { assert(print_open && font == 0x10 && !arg1); }
void func_8001F324(s32 r, s32 g, s32 b, s32 a) { assert(print_open && r && g && b && a == 0xFF); }
void func_8001F1E8(s32 x, s32 y, const char *fmt, ...) {
    assert(print_open && x == 0x50 && y == 0x13A && strlen(fmt) < sizeof(hint));
    strcpy(hint, fmt);
}
void func_8001F444(void) { assert(print_open); print_open = 0; }
static unsigned prints;
void osSyncPrintf(const char *format, ...) { (void)format; ++prints; }
static ScMaArena *arena_ptr = &arena;
ScMaArena *sc_service_native_arena(void) { return arena_ptr; }

/* ---- admitted backing (substituted DATA lease) ---- */
static ScUi ui;
static ScUiDriver driver;
static ScUiBattleRequest request;

/* ---- native probe helpers (substituted) ---- */
static int mapping_status = SC_OK;
static unsigned publishes;
int sc_native_mapping(const ScState *state, unsigned count, const RlU8 indices[6],
                      RlU8 output[6]) {
    unsigned i;
    if (mapping_status != SC_OK || count != state->party_count) return SC_INVALID;
    for (i = 0; i < count; ++i) output[i] = indices[i];
    return SC_OK;
}
int sc_native_capture(const ScState *state, const unk_func_80026268_arg0 *party,
                      unsigned count, const RlU8 mapping[6], unsigned active,
                      ScBattleSnapshot snapshots[6]) {
    unsigned i;
    assert(count == state->party_count && active < count);
    memset(snapshots, 0, 6 * sizeof(*snapshots));
    for (i = 0; i < count; ++i) {
        snapshots[mapping[i]].hp = party[i].unk_02;
        snapshots[mapping[i]].status = party[i].unk_05;
    }
    return SC_OK;
}
int sc_story_publish_live(const ScState *state, const RlU8 map[6], RlBattleSide *side) {
    assert(state == &ui.state && map && side);
    ++publishes;
    return SC_OK;
}

static RlU8 loader_span[64] __attribute__((aligned(64)));
static int loader_bound, loader_refuse_bind, loader_pinned;
static unsigned loader_binds, loader_unbinds;
static void bind_backing(void) {
    ScNativeCampaignBacking *b = &ctl()->lifetime.loop.backing;
    b->ui = &ui; b->driver = &driver; b->request = &request;
    b->batl_loader = loader_span;
}
int sc_batl_loader_bind(void *base, u32 bytes) {
    assert(base == loader_span && bytes == SC_NATIVE_BATL_LOADER_BYTES && !gScCampaignBattle);
    ++loader_binds;
    if (loader_refuse_bind || loader_bound) return 0;
    loader_bound = 1;
    return 1;
}
int sc_batl_loader_unbind(void) {
    assert(!gScCampaignBattle);
    ++loader_unbinds;
    if (!loader_bound || loader_pinned) return 0;
    loader_bound = 0;
    return 1;
}

/* ---- scripted loop: drives the real owner callbacks ---- */
static int want_launch, want_exit, hold_on_ko, hold_pending, presented;
static unsigned saves, opid;
static int loop_script(void) {
    ScNativeLoop *loop = &ctl()->lifetime.loop;
    ScNativeCampaignBacking *b = &loop->backing;
    int status;
    bind_backing();
    if (ctl()->operation.queued) return poll_now();
    if (loop->held_operation) {
        if (!presented) { presented = 1; return SC_OK; }
        status = owner()->resume(owner()->context, b, loop->held_operation,
                                 loop->held_generation, &ui.state, &request.progression);
        if (status == SC_OK) loop->held_operation = loop->held_generation = presented = 0;
        return status;
    }
    if (want_launch && !driver.launched) {
        want_launch = 0;
        driver.launched = driver.bound = 1;
        driver.operation_id = request.operation_id = ++opid;
        status = owner()->submit(owner()->context, b, &request);
        if (status != SC_OK) driver.launched = 0;
        return status;
    }
    status = poll_now();
    if (status == SC_OK && want_exit && !driver.launched) loop->exited = 1;
    return status == SC_OK && driver.launched ? SC_PENDING : status;
}
static void scripted_submit(void) {
    ctl()->operation.operation = request.operation_id;
    ctl()->operation.phase = SC_NATIVE_OPERATION_RUNNING;
    ctl()->operation.queued = 0;
}
static void scripted_observe(void) {
    ctl()->operation.queued = 1;
    if (!observed_values[2] && !observed_values[1] && hold_on_ko) hold_pending = 1;
}
static void scripted_poll(void) {
    ScNativeLoop *loop = &ctl()->lifetime.loop;
    if (ctl()->operation.queued) {
        ctl()->operation.queued = 0;
        if (!observed_values[2]) ui.state.defeated_mask |= (RlU8)(1U << observed_values[1]);
        if (hold_pending) {
            hold_pending = 0;
            ctl()->operation.phase = SC_NATIVE_OPERATION_HELD;
            loop->held_operation = ctl()->operation.operation;
            loop->held_generation = 1;
        }
        return;
    }
    /* drained terminal settled and uninstalled: finish -> checkpoint save */
    if (driver.launched && ctl()->binding.drained && !ctl()->installed) {
        driver.launched = 0;
        ctl()->operation.phase = SC_NATIVE_OPERATION_IDLE;
        ++saves;
    }
}
static void scripted_resume(void) { ctl()->operation.phase = SC_NATIVE_OPERATION_RUNNING; }

/* ---- rl_start_battle (substituted launcher seam) ---- */
enum { BATTLE_WIN, BATTLE_PREARM, BATTLE_ABORT, BATTLE_BAD_FRAME, BATTLE_PENDING_DRAIN };
static int battle_kind;
static unsigned battles, paused_frames, deferred_seen;
static RlBattleSide sides[2];
static RlBattleTeam teams[2];

static void frame(int logic_result) {
    int result = -1, gate = sc_campaign_battle_gate(&sides[0], &sides[1], &result);
    if (gate == 1) { ++paused_frames; assert(sc_campaign_battle_paused()); return; }
    if (gate == 2) { ++deferred_seen; assert(result == logic_result); return; }
    assert(sc_campaign_battle_after(&sides[0], &sides[1], logic_result) == logic_result ||
           sc_campaign_battle_paused());
}
static void hook_arm(void) {
    ScNativeTerminalBinding *b = &ctl()->binding;
    assert(gScNativeLaunchHook == &b->hook && !b->drained);
    b->terminal->armed = 1; b->status = SC_OK;
}
static void hook_drain(unsigned player, unsigned enemy) {
    ScNativeTerminalBinding *b = &ctl()->binding;
    b->drained = 1; b->player_alive = player; b->enemy_alive = enemy;
    b->status = sc_native_terminal_drain(b->terminal, b->operation, b->backing, player, enemy);
}
static void hook_abort(void) {
    ScNativeTerminalBinding *b = &ctl()->binding;
    b->drained = 1; b->terminal->armed = 0; b->terminal->failed = 1; b->status = SC_INVALID;
}
int rl_start_battle(const RlMon *party, int party_n, const RlMon *enemy, int enemy_n,
                    const RlBattleRules *rules, int return_state, int unused) {
    static const RlU8 identity[6] = {0, 1, 2, 3, 4, 5};
    unsigned i, guard;
    (void)unused;
    ++battles;
    assert(gScCampaignBattle && return_state == STATE_STORY_CAMPAIGN && loader_bound);
    assert(party == request.party && enemy == request.enemy);
    assert(party_n == request.party_count && enemy_n == request.enemy_count);
    assert(rules->use_state == 1 && rules->stadium == 3 && !rules->player_cpu && !rules->relics);
    assert(!ctl()->terminal.armed && ctl()->installed);
    /* rl_battle.c: campaign mapping runs pre-arm; a refusal returns -1 */
    if (sc_campaign_battle_mapping(identity, (unsigned)party_n) != SC_OK) return -1;
    if (battle_kind == BATTLE_PREARM) return -1;
    hook_arm();
    if (battle_kind == BATTLE_ABORT) { hook_abort(); return -1; }
    memset(sides, 0, sizeof(sides)); memset(teams, 0, sizeof(teams));
    teams[0].unk_002 = (u8)party_n; teams[1].unk_002 = (u8)enemy_n;
    for (i = 0; i < (unsigned)party_n; ++i) teams[0].unk_01C[i].unk_02 = party[i].cur_hp;
    sides[0].unk_724 = &teams[0]; sides[1].unk_724 = &teams[1];
    sides[0].unk_654.unk_38.unk_0C = party[0].cur_hp;
    sides[1].unk_654.unk_38.unk_0C = 30;
    if (battle_kind == BATTLE_BAD_FRAME) {
        sides[1].unk_654.unk_08 = (u8)enemy_n;
        for (i = 0; i < 8; ++i) frame(0);
        assert(sc_campaign_battle_paused() && paused_frames == 8 && gScCampaignRun.error);
        return 0; /* battle hangs on target: no drain */
    }
    frame(0);                               /* first observation */
    assert(gScCampaignRun.observations == 1);
    frame(0);                               /* unchanged: no observation */
    assert(gScCampaignRun.observations == 1);
    sides[0].unk_654.unk_38.unk_0C -= 5;    /* player damaged */
    frame(0);
    assert(gScCampaignRun.observations == 2 && teams[0].unk_01C[0].unk_02 == party[0].cur_hp - 5);
    sides[1].unk_654.unk_38.unk_0C = 0;     /* KO: learning hold through after() */
    assert(sc_campaign_battle_after(&sides[0], &sides[1], 7) == (hold_on_ko ? 0 : 7));
    if (hold_on_ko) {
        assert(sc_campaign_battle_paused() && ctl()->lifetime.loop.held_operation);
        /* presented, then resumed: the resuming gate runs the deferred
           post-logic frame with the retained result */
        for (guard = 0; !deferred_seen && guard < 8; ++guard) frame(7);
        assert(!sc_campaign_battle_paused() && deferred_seen == 1 && paused_frames >= 1);
    }
    assert(ui.state.defeated_mask & 1);
    sides[1].unk_654.unk_08 = 1; sides[1].unk_654.unk_38.unk_0C = 25;
    frame(0);
    sides[1].unk_654.unk_38.unk_0C = 0;
    frame(0);
    assert(ui.state.defeated_mask == 3 && !sc_campaign_battle_paused());
    hook_drain(1, 0);
    return 1;
}

/* ---- connected scenarios ---- */
static void start(void) {
    reset();
    loader_bound = loader_refuse_bind = loader_pinned = 0;
    loader_binds = loader_unbinds = 0;
    on_submit = scripted_submit; on_poll = scripted_poll;
    on_resume = scripted_resume; on_observe = scripted_observe;
    during_step = loop_script;
    memset(&ui, 0, sizeof(ui)); memset(&driver, 0, sizeof(driver));
    memset(&request, 0, sizeof(request));
    ui.state.party_count = request.party_count = 2;
    request.enemy_count = 2;
    request.party[0].cur_hp = 40; request.party[1].cur_hp = 35;
    gCurrentGameState = STATE_STORY_CAMPAIGN;
}
static void state(void) { *trace = 0; sc_campaign_state(); }

/* Visible fragment57 entry: R request rides the vanilla B exit (state 2). */
static int menu_select_r(void) {
    /* fragment57 R branch: accepted -> index 7 (title exit); refused -> stay */
    return sc_campaign_menu_request() ? STATE_TITLE_SCREEN : -1;
}

static void menu_hook(void) {
    start();
    osMemSize = 0x800000;
    sc_campaign_menu_hint();
    assert(!strcmp(hint, "R: STORY CAMPAIGN") && !print_open);
    /* no request: every vanilla/RL result passes through unchanged */
    assert(sc_campaign_menu_hook(STATE_TITLE_SCREEN) == STATE_TITLE_SCREEN);
    assert(sc_campaign_menu_hook(5) == 5 && sc_campaign_menu_hook(0) == 0);
    pad.buttonDown = R_TRIG;   /* holding R no longer binds anything */
    assert(sc_campaign_menu_hook(5) == 5);
    pad.buttonDown = 0;
    /* visible R: accepted, consumed once */
    assert(sc_campaign_menu_hook(menu_select_r()) == STATE_STORY_CAMPAIGN);
    assert(sc_campaign_menu_hook(STATE_TITLE_SCREEN) == STATE_TITLE_SCREEN);
    /* a request is consumed even if fragment57 returned another state */
    assert(sc_campaign_menu_request());
    assert(sc_campaign_menu_hook(5) == 5 && sc_campaign_menu_hook(STATE_TITLE_SCREEN) == 2);
    /* cancel: the story UI exits, closes and returns to Menu Select; re-enter */
    gCurrentGameState = sc_campaign_menu_hook(menu_select_r());
    assert(gCurrentGameState == STATE_STORY_CAMPAIGN);
    state();
    assert(gScCampaignRun.stage == SC_RUN_ACTIVE && loads == 1);
    /* not offered while a lifetime is live */
    assert(menu_select_r() == -1 && sc_campaign_menu_hook(STATE_TITLE_SCREEN) == 2);
    want_exit = 1;
    state();
    assert(ctl()->lifetime.loop.exited && gScCampaignRun.stage == SC_RUN_CLOSING);
    state();
    assert(gCurrentGameState == STATE_MENU_SELECT && gScCampaignRun.stage == SC_RUN_ENTRY);
    assert(!mapped && sc_campaign_phase() == SC_CAMPAIGN_IDLE && !sc_persist_holds_fragment());
    want_exit = 0; opid = 0; ctl()->lifetime.loop.exited = 0;
    gCurrentGameState = sc_campaign_menu_hook(menu_select_r());
    assert(gCurrentGameState == STATE_STORY_CAMPAIGN);
    state();
    assert(gScCampaignRun.stage == SC_RUN_ACTIVE && loads == 2);
}

static void menu_no_pak(void) {
    start();
    osMemSize = 0x400000;
    sc_campaign_menu_hint();
    assert(!strcmp(hint, "STORY CAMPAIGN NEEDS EXPANSION PAK") && !print_open);
    /* refused before leaving Menu Select: no state, no load, no allocation */
    assert(menu_select_r() == -1);
    assert(sc_campaign_menu_hook(STATE_TITLE_SCREEN) == STATE_TITLE_SCREEN);
    assert(loads == 0 && !mapped && sc_campaign_phase() == SC_CAMPAIGN_IDLE);
    osMemSize = 0x800000;
}

static void refusal_4mib(void) {
    start();
    arena_ptr = NULL;
    state();
    assert(gCurrentGameState == STATE_MENU_SELECT && gScCampaignRun.stage == SC_RUN_ENTRY);
    assert(gScCampaignRun.refusals == 1 && loads == 0 && !mapped && seen(""));
    assert(sc_campaign_phase() == SC_CAMPAIGN_IDLE);
}

static void canceled_admission(void) {
    start();
    acquire_status = SC_UNAVAILABLE;
    state();
    assert(gScCampaignRun.stage == SC_RUN_CLOSING && sc_campaign_phase() == SC_CAMPAIGN_CLOSING);
    assert(gCurrentGameState == STATE_STORY_CAMPAIGN && mapped);
    state();
    assert(gCurrentGameState == STATE_MENU_SELECT && gScCampaignRun.stage == SC_RUN_ENTRY);
    assert(!mapped && sc_campaign_phase() == SC_CAMPAIGN_IDLE && !sc_persist_holds_fragment());
    /* re-entry admits a fresh lifetime */
    acquire_status = SC_OK; gCurrentGameState = STATE_STORY_CAMPAIGN;
    state();
    assert(gScCampaignRun.stage == SC_RUN_ACTIVE && loads == 2);
}

static void failed_admission(void) {
    start();
    load_ok = 0;
    state();
    assert(gScCampaignRun.stage == SC_RUN_FAILED && gScCampaignRun.error);
    assert(sc_campaign_phase() == SC_CAMPAIGN_FAILED && sc_persist_holds_fragment());
    *trace = 0;
    state(); state();
    assert(gScCampaignRun.stage == SC_RUN_FAILED && seen("") && loads == 1);
    assert(gCurrentGameState == STATE_STORY_CAMPAIGN);
}

static void full_cycle(void) {
    unsigned i;
    start();
    hold_on_ko = 1;
    state();
    assert(gScCampaignRun.stage == SC_RUN_ACTIVE && !gScCampaignRun.launched);
    state();                                  /* nothing requested: plain STEP */
    assert(battles == 0 && !ctl()->installed);
    for (i = 0; i < 2; ++i) {
        want_launch = 1;
        state();                              /* loop submits: hook installed */
        assert(ctl()->installed && ctl()->operation.operation == i + 1 && battles == i);
        state();                              /* launcher */
        assert(battles == i + 1 && gScCampaignRun.launched == i + 1 && !gScCampaignBattle &&
               loader_binds == i + 1 && loader_unbinds == i + 1 && !loader_bound);
        assert(gScCampaignRun.last_outcome == 1 && ctl()->binding.drained);
        state();                              /* settle: uninstall, finish, save */
        assert(!ctl()->installed && !gScNativeLaunchHook && saves == i + 1 && !driver.launched);
        assert(!sc_persist_state()->pins[SC_PERSIST_PIN_HOOK]);
        state();                              /* no relaunch of the same id */
        assert(battles == i + 1);
        ui.state.defeated_mask = 0; deferred_seen = 0;
        gScCampaignRun.observations = 0;
    }
    assert(gScCampaignRun.battles == 2 && drains == 2 && publishes >= 4);
    want_exit = 1;
    state();
    assert(ctl()->lifetime.loop.exited && gScCampaignRun.stage == SC_RUN_CLOSING);
    state();
    assert(gCurrentGameState == STATE_MENU_SELECT && gScCampaignRun.stage == SC_RUN_ENTRY);
    assert(!mapped && sc_campaign_phase() == SC_CAMPAIGN_IDLE && !sc_persist_holds_fragment());
    /* reopen in the same boot restarts operation ids and the launch guard */
    gCurrentGameState = STATE_STORY_CAMPAIGN; want_exit = 0; opid = 0;
    ctl()->lifetime.loop.exited = 0;
    state();
    assert(gScCampaignRun.stage == SC_RUN_ACTIVE && !gScCampaignRun.launched);
    want_launch = 1; state(); state();
    assert(battles == 3 && gScCampaignRun.launched == 1);
}

static void pending_drain(void) {
    start();
    state();
    want_launch = 1; state();
    drain_status = SC_PENDING;
    state();
    assert(battles == 1 && ctl()->binding.drained && ctl()->binding.status == SC_PENDING);
    /* raw flags retained; retries never relaunch and keep the hook pinned */
    state(); state();
    assert(battles == 1 && ctl()->installed && sc_persist_state()->pins[SC_PERSIST_PIN_HOOK]);
    assert(drains == 3 && saves == 0);
    /* exit request while PENDING: close is held, nothing unmapped */
    want_exit = 1;
    gScCampaignRun.stage = SC_RUN_CLOSING;
    state();
    assert(gScCampaignRun.stage == SC_RUN_CLOSING && sc_campaign_active() && mapped);
    drain_status = SC_OK;
    state();                                  /* CLOSING keeps stepping while ACTIVE */
    assert(!ctl()->installed && saves == 1 && ctl()->lifetime.loop.exited);
    /* retained teardown: lifetime release PENDING, then quiescence */
    release_status = SC_PENDING;
    state();
    assert(gScCampaignRun.stage == SC_RUN_CLOSING && sc_campaign_phase() == SC_CAMPAIGN_CLOSING);
    assert(mapped && gCurrentGameState == STATE_STORY_CAMPAIGN);
    release_status = SC_OK; quiescent = 0;
    state();
    assert(sc_campaign_phase() == SC_CAMPAIGN_RELEASING && mapped);
    quiescent = 1;
    state();
    assert(gCurrentGameState == STATE_MENU_SELECT && !mapped);
}

static void prearm_refusal(void) {
    start();
    state();
    want_launch = 1; state();
    battle_kind = BATTLE_PREARM;
    state();
    assert(battles == 1 && !ctl()->terminal.armed && !ctl()->binding.drained);
    assert(gScCampaignRun.stage == SC_RUN_FAILED && sc_campaign_phase() == SC_CAMPAIGN_FAILED);
    assert(mapped && sc_persist_holds_fragment() && sc_campaign_close() == SC_INVALID);
    state();
    assert(battles == 1 && gCurrentGameState == STATE_STORY_CAMPAIGN);
}

static void loader_bind_refusal(void) {
    start();
    state();
    want_launch = 1; state();
    loader_refuse_bind = 1;
    state();
    assert(loader_binds == 1 && !loader_unbinds && battles == 0 && !loader_bound);
    assert(gScCampaignRun.stage == SC_RUN_FAILED && sc_campaign_phase() == SC_CAMPAIGN_FAILED);
    assert(mapped && sc_persist_holds_fragment() && sc_campaign_close() == SC_INVALID);
}

static void loader_pinned_refusal(void) {
    start();
    state();
    want_launch = 1; state();
    loader_pinned = 1;
    state();
    assert(loader_binds == 1 && loader_unbinds == 1 && battles == 1 && loader_bound);
    assert(gScCampaignRun.stage == SC_RUN_FAILED && sc_campaign_phase() == SC_CAMPAIGN_FAILED);
    assert(mapped && sc_persist_holds_fragment() && sc_campaign_close() == SC_INVALID);
}

static void mapping_refusal(void) {
    start();
    state();
    want_launch = 1; state();
    mapping_status = SC_INVALID;
    state();
    assert(battles == 1 && !ctl()->terminal.armed && gScCampaignRun.stage == SC_RUN_FAILED);
    assert(mapped && sc_campaign_phase() == SC_CAMPAIGN_FAILED);
}

static void init_abort(void) {
    start();
    state();
    want_launch = 1; state();
    battle_kind = BATTLE_ABORT;
    state();
    /* armed then aborted: settles through the owner poll, never relaunched */
    assert(battles == 1 && ctl()->binding.drained && gScCampaignRun.stage == SC_RUN_ACTIVE);
    state();
    assert(gScCampaignRun.stage == SC_RUN_FAILED && ctl()->installed && mapped);
    assert(sc_campaign_phase() == SC_CAMPAIGN_FAILED && sc_persist_holds_fragment());
    assert(sc_persist_state()->pins[SC_PERSIST_PIN_HOOK] && sc_campaign_close() == SC_INVALID);
    *trace = 0;
    state();
    assert(battles == 1 && seen(""));
}

static void refused_drain_run(void) {
    /* drained but refused by the terminal: same permanent fail-stop */
    start();
    state();
    want_launch = 1; state();
    drain_status = SC_INVALID;
    state();
    assert(battles == 1 && ctl()->binding.status == SC_INVALID);
    state();
    assert(gScCampaignRun.stage == SC_RUN_FAILED && ctl()->installed && mapped);
    assert(sc_campaign_phase() == SC_CAMPAIGN_FAILED);
}

static void bad_frame(void) {
    start();
    state();
    want_launch = 1; state();
    battle_kind = BATTLE_BAD_FRAME;
    state();
    assert(battles == 1 && gScCampaignRun.observations == 0 && ctl()->terminal.armed);
    /* outside the launch the predicate is clear and the tick is inert */
    assert(!gScCampaignBattle && !sc_campaign_battle_paused());
    assert(sc_campaign_battle_after(&sides[0], &sides[1], 9) == 9);
}

static void observe_refusal(void) {
    start();
    state();
    want_launch = 1; state();
    observe_status = SC_INVALID;
    battle_kind = BATTLE_BAD_FRAME;
    /* reuse the bad-frame launcher with a valid opponent so only the
       operation observation is refused */
    hold_on_ko = 0;
    {
        static const RlU8 identity[6] = {0, 1, 2, 3, 4, 5};
        int result = 0;
        gScCampaignBattle = 1;
        assert(sc_campaign_battle_mapping(identity, 2) == SC_OK);
        hook_arm();
        teams[0].unk_002 = 2; teams[1].unk_002 = 2;
        sides[0].unk_724 = &teams[0]; sides[1].unk_724 = &teams[1];
        assert(sc_campaign_battle_gate(&sides[0], &sides[1], &result) == 1);
        assert(sc_campaign_battle_paused() && gScCampaignRun.error);
        gScCampaignBattle = 0;
    }
}

int main(void) {
    /* shared installer/adapter harness scenarios (hooks unset) */
    run(lifecycle, "harness: installer lifecycle");
    run(teardown_with_hook, "harness: teardown with hook");
    run(refused_open, "harness: refused open");
    run(refused_load, "harness: refused load");
    run(refused_drain, "harness: refused drain");
    run(observe, "harness: observe");
    run(fail_stop_idle, "harness: fail-stop idle");
    run(menu_hook, "visible menu entry: hint, R request, consume once, cancel/close, re-entry");
    run(menu_no_pak, "4 MiB menu: visible refusal hint, R refused before any state change/load");
    run(refusal_4mib, "4 MiB: no arena refuses before any load and returns to Menu Select");
    run(canceled_admission, "canceled admission: refused acquire closes, returns, re-entry admits");
    run(failed_admission, "failed admission: load failure fail-stops and retains");
    run(full_cycle, "menu->admission->submit->launch->observe/KO hold/resume->drain->save x2->close->reopen");
    run(pending_drain, "PENDING drain: retained retry, held close, release PENDING/quiescence teardown");
    run(prearm_refusal, "pre-arm refusal: fail-stop, nothing armed, retention");
    run(mapping_refusal, "mapping refusal: pre-arm fail-stop");
    run(loader_bind_refusal, "BATL loader span refused: fail-stop before battle, retained");
    run(loader_pinned_refusal, "BATL loader span still pinned after battle: fail-stop, retained");
    run(init_abort, "init abort after arm: settles by poll, fail-stop retains hook/mapping");
    run(refused_drain_run, "refused drain after arm: fail-stop retains hook/mapping");
    run(bad_frame, "invalid frame: tick fail-stops and pauses the battle; inert outside launch");
    run(observe_refusal, "refused observation: tick fail-stop pause");
    puts("STORY CAMPAIGN RUN HOST PASSED");
    return 0;
}
