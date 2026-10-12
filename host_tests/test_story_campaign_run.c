/* HOST connected sequential runner: actual installer, owner adapter, persist,
   arena and KO capture. Loop/lifetime/display, driver detach, result UI and
   standard battle are recorded seams, not native retirement evidence. */
#define SC_CAMPAIGN_RUN_TEST
#include "test_story_campaign.c"
#include "src/story/sc_campaign_battle.h"

int gScProbeActive;
RlU8 gScProbeSixMapping[6];
static Controller pad;
Controller *gPlayer1Controller = &pad;
s32 gCurrentGameState;
u32 osMemSize = 0x800000;
u32 osGetCount(void) { return 77; }
void func_8001F3F4(void) {}
void func_8001EBE0(s32 font, s32 arg1) { (void)font; (void)arg1; }
void func_8001F324(s32 r, s32 g, s32 b, s32 a) {
    (void)r; (void)g; (void)b; (void)a;
}
void func_8001F1E8(s32 x, s32 y, const char *fmt, ...) {
    (void)x; (void)y; (void)fmt;
}
void func_8001F444(void) {}
void osSyncPrintf(const char *format, ...) { (void)format; }
ScMaArena *sc_service_native_arena(void) { return &arena; }

static ScUi ui;
static ScUiDriver driver;
static ScUiBattleRequest request;
static ScNativePak pak;
static ScState before;
static unsigned detached, reopened, battles, loader_binds, loader_unbinds;
static int loader_bound, loader_refuse, loader_pinned, bad_frame, battle_loss;
static int want_launch, want_exit;
static ScMaHandle shell_handle;
static int shell_live;
static unsigned opid;

int sc_native_mapping(const ScState *state, unsigned count, const RlU8 indices[6],
                      RlU8 output[6]) {
    unsigned i;
    if (count != state->party_count) return SC_INVALID;
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
    (void)state; (void)map; (void)side;
    assert(!"sequential capture must not publish progression");
    return SC_INVALID;
}
int sc_ui_driver_detach_battle(ScUiDriver *d) {
    assert(d == &driver && d->launched && mapped && shell_live && !loader_bound);
    ++detached;
    d->launched = 0;
    ctl()->lifetime.loop.exited = 1;
    return SC_OK;
}
int sc_ui_resume_postbattle(ScUi *target, const ScUiBattleRequest *r,
                            const ScState *state, const RlLaunchResult *result,
                            const ScKoLedger *ledger, unsigned outcome) {
    assert(target == &ui && mapped && shell_live && !loader_bound);
    assert(pak_session);
    assert(!memcmp(state, &before, sizeof(before)));
    assert(r->operation_id == opid && ledger->operation_id == opid);
    assert(ledger->count == 2 && ledger->next == 0);
    assert(ledger->events[0].enemy_slot == 1 && ledger->events[1].enemy_slot == 0);
    assert(ledger->events[0].recipients == 1 && ledger->events[1].recipients == 2);
    assert(result->player[0].cur_hp == 0 && result->player[1].cur_hp == 30);
    assert(outcome == (battle_loss ? SC_LOSS : SC_WIN));
    ui.operation_id = r->operation_id;
    ++reopened;
    return SC_OK;
}

int sc_batl_loader_bind(void *base, u32 bytes) {
    unsigned i, live = 0;
    assert(base && !((uintptr_t)base & 63U) && bytes == SC_NATIVE_BATL_LOADER_BYTES);
    assert(!mapped && !shell_live && !gScNativeLaunchHook && !loader_bound);
    assert(sc_campaign_phase() == SC_CAMPAIGN_IDLE);
    for (i = 0; i < SC_MA_SLOTS; ++i) live += !!arena.blocks[i].live;
    assert(live == 1);
    ++loader_binds;
    if (loader_refuse) return 0;
    loader_bound = 1;
    return 1;
}
int sc_batl_loader_live(void) { return loader_pinned; }
int sc_batl_loader_unbind(void) {
    assert(!gScCampaignBattle && loader_bound);
    ++loader_unbinds;
    if (loader_pinned) return 0;
    loader_bound = 0;
    return 1;
}

static void bind_backing(void) {
    ScNativeCampaignBacking *b = &ctl()->lifetime.loop.backing;
    b->ui = &ui; b->driver = &driver; b->request = &request;
    b->pak = &pak;
}
static void acquire_shell(void) {
    assert(mapped && !shell_live && !loader_bound);
    assert(sc_ma_alloc(&arena, 0x70000U, &shell_handle));
    shell_live = 1;
    bind_backing();
}
static void release_shell(void) {
    assert(mapped && shell_live && !loader_bound && !gScNativeLaunchHook);
    assert(sc_ma_release(&arena, shell_handle));
    shell_live = 0;
}
static void scripted_submit(void) {
    ctl()->operation.operation = request.operation_id;
    ctl()->operation.phase = SC_NATIVE_OPERATION_RUNNING;
    ctl()->operation.queued = 0;
}
static int loop_script(void) {
    bind_backing();
    if (want_launch) {
        want_launch = 0;
        driver.launched = driver.bound = 1;
        driver.operation_id = request.operation_id = ++opid;
        request.progression.operation_id = opid;
        return owner()->submit(owner()->context, &ctl()->lifetime.loop.backing, &request);
    }
    if (want_exit) ctl()->lifetime.loop.exited = 1;
    return SC_OK;
}
static void state(void) {
    unsigned i;
    *trace = 0;
    sc_campaign_state();
    if (gScCampaignRun.stage == SC_RUN_REOPEN) {
        for (i = 0; i < SC_MA_SLOTS; ++i) assert(!arena.blocks[i].live);
    }
}

int rl_start_battle(const RlMon *party, int party_n, const RlMon *enemy, int enemy_n,
                    const RlBattleRules *rules, int return_state, RlLaunchResult *result) {
    static const RlU8 identity[6] = {0, 1, 2, 3, 4, 5};
    RlBattleSide sides[2];
    RlBattleTeam teams[2];
    unsigned i;
    int frame_result = 0;
    assert(!mapped && !shell_live && loader_bound && !ctl()->installed);
    assert(!gScNativeLaunchHook && gScCampaignBattle && return_state == STATE_STORY_CAMPAIGN);
    assert(party == ctl()->sequential_request.party && enemy == ctl()->sequential_request.enemy);
    assert(party_n == 2 && enemy_n == 2 && rules->use_state == 1);
    assert(sc_campaign_battle_mapping(identity, (unsigned)party_n) == SC_OK);
    ++battles;
    memset(sides, 0, sizeof(sides)); memset(teams, 0, sizeof(teams));
    for (i = 0; i < 2; ++i) {
        teams[i].unk_002 = 2;
        sides[i].unk_724 = &teams[i];
        teams[0].unk_01C[i].unk_02 = party[i].cur_hp;
    }
    sides[0].unk_654.unk_38.unk_0C = party[0].cur_hp;
    sides[1].unk_654.unk_08 = bad_frame ? 2 : 1;
    sides[1].unk_654.unk_38.unk_0C = 25;
    if (bad_frame) {
        assert(sc_campaign_battle_gate(&sides[0], &sides[1], &frame_result) == 1);
        assert(sc_campaign_battle_paused());
        return -1;
    }
    assert(sc_campaign_battle_gate(&sides[0], &sides[1], &frame_result) == 0);
    sides[1].unk_654.unk_38.unk_0C = 0;
    assert(sc_campaign_battle_after(&sides[0], &sides[1], 7) == 7);
    assert(!sc_campaign_battle_paused());
    assert(sc_campaign_battle_after(&sides[0], &sides[1], 7) == 7);
    assert(ctl()->sequential_ledger.count == 1);
    /* The first recipient faints after earning its KO; second earns the next. */
    teams[0].unk_01C[0].unk_02 = 0;
    sides[0].unk_654.unk_08 = 1;
    sides[0].unk_654.unk_38.unk_0C = 30;
    sides[1].unk_654.unk_08 = 0;
    sides[1].unk_654.unk_38.unk_0C = 20;
    assert(sc_campaign_battle_gate(&sides[0], &sides[1], &frame_result) == 0);
    sides[1].unk_654.unk_38.unk_0C = 0;
    assert(sc_campaign_battle_after(&sides[0], &sides[1], 7) == 7);
    assert(!memcmp(&ctl()->sequential_state, &before, sizeof(before)));
    assert(!memcmp(&ui.state, &before, sizeof(before)));
    memset(result, 0, sizeof(*result));
    result->player_count = result->enemy_count = 2;
    result->player[0] = party[0]; result->player[0].cur_hp = 0;
    result->player[1] = party[1]; result->player[1].cur_hp = 30;
    result->player_index[0] = 0; result->player_index[1] = 1;
    result->outcome = battle_loss ? RL_BATTLE_LOSS : RL_BATTLE_WIN;
    return result->outcome;
}

static void start(void) {
    reset();
    during_step = loop_script; on_submit = scripted_submit;
    on_lifetime_acquire = acquire_shell; on_lifetime_release = release_shell;
    memset(&ui, 0, sizeof(ui)); memset(&driver, 0, sizeof(driver));
    memset(&request, 0, sizeof(request));
    ui.state.party_count = request.party_count = 2;
    ui.state.phase = SC_BATTLE;
    ui.state.encounter = request.encounter = request.progression.encounter = 9;
    ui.state.party[0].species = request.party[0].species = request.progression.party_ids[0] = 1;
    ui.state.party[1].species = request.party[1].species = request.progression.party_ids[1] = 4;
    ui.state.party[0].cur_hp = request.party[0].cur_hp = 40;
    ui.state.party[1].cur_hp = request.party[1].cur_hp = 35;
    request.enemy_count = request.progression.team.count = ui.bridge.enemy_count = 2;
    request.progression.ready = 1;
    before = ui.state;
    gCurrentGameState = STATE_STORY_CAMPAIGN;
}

static void ready(void) {
    start();
    state();
    assert(gScCampaignRun.stage == SC_RUN_ACTIVE && mapped && shell_live);
    want_launch = 1; state();
    assert(ctl()->installed && !detached && !battles);
    state();
    assert(detached == 1 && gScCampaignRun.stage == SC_RUN_CLOSING && mapped && shell_live);
    state();
    assert(gScCampaignRun.stage == SC_RUN_BATTLE_READY && !mapped && !shell_live);
    assert(!gScNativeLaunchHook && !ctl()->installed && !battles);
}

static void full_cycle(void) {
    unsigned i;
    ready();
    for (i = 0; i < 2; ++i) {
        state();
        assert(battles == i + 1 && gScCampaignRun.stage == SC_RUN_REOPEN);
        assert(loader_binds == i + 1 && loader_unbinds == i + 1 && !loader_bound);
        state();
        assert(reopened == i + 1 && gScCampaignRun.stage == SC_RUN_ACTIVE);
        assert(!ctl()->sequential_active && !ctl()->sequential_result_ready);
        state();
        assert(battles == i + 1);
        if (!i) {
            want_launch = 1; state(); state(); state();
            assert(gScCampaignRun.stage == SC_RUN_BATTLE_READY);
        }
    }
    assert(gScCampaignRun.launched == 2 && ui.operation_id == 2);
    want_exit = 1; state(); state();
    assert(gScCampaignRun.stage == SC_RUN_ENTRY && gCurrentGameState == STATE_MENU_SELECT);
    assert(!mapped && !shell_live);
}
static void loss_cycle(void) { battle_loss = 1; full_cycle(); }
static void refused_postbattle_pak(void) {
    ready(); state();
    assert(gScCampaignRun.stage == SC_RUN_REOPEN);
    pak_begin_status = SC_SAVE_IO;
    assert(sc_campaign_open(&arena, 77) == SC_UNAVAILABLE);
    assert(!pak_session && !reopened);
    assert(ctl()->sequential_active && ctl()->sequential_result_ready);
}
static void refused_postbattle_pak_end(void) {
    ready(); state();
    assert(gScCampaignRun.stage == SC_RUN_REOPEN);
    pak_end_status = SC_SAVE_IO;
    assert(sc_campaign_open(&arena, 77) == SC_INVALID);
    assert(!pak_session && reopened == 1);
    assert(ctl()->sequential_active && ctl()->sequential_result_ready);
}
static void close_pending(void) {
    start(); state(); want_launch = 1; state(); state();
    release_status = SC_PENDING;
    state(); state();
    assert(mapped && shell_live && !battles && !loader_binds);
    release_status = SC_OK; quiescent = 0;
    state();
    assert(gScCampaignRun.stage == SC_RUN_CLOSING && mapped && !shell_live);
    quiescent = 1; state();
    assert(gScCampaignRun.stage == SC_RUN_BATTLE_READY && !mapped);
    state();
    assert(battles == 1 && gScCampaignRun.stage == SC_RUN_REOPEN);
}
static void refused_loader(void) {
    unsigned i;
    ready(); loader_refuse = 1; state();
    assert(loader_binds == 1 && !battles && gScCampaignRun.stage == SC_RUN_FAILED);
    assert(!mapped && !shell_live);
    for (i = 0; i < SC_MA_SLOTS; ++i) assert(!arena.blocks[i].live);
    state(); assert(loader_binds == 1);
}
static void pinned_loader(void) {
    ready(); loader_pinned = 1; state();
    assert(battles == 1 && loader_bound && gScCampaignRun.stage == SC_RUN_FAILED);
    assert(!mapped && !reopened);
    state(); assert(battles == 1);
}
static void invalid_frame(void) {
    ready(); bad_frame = 1; state();
    assert(battles == 1 && !loader_bound && gScCampaignRun.stage == SC_RUN_FAILED);
    assert(!mapped && !reopened && !gScCampaignBattle);
    state(); assert(battles == 1);
}

int main(void) {
    run(lifecycle, "installer lifecycle");
    run(teardown_with_hook, "installer teardown with hook");
    run(refused_open, "installer refused open");
    run(refused_load, "installer refused load");
    run(refused_drain, "installer refused drain");
    run(observe, "installer observe");
    run(fail_stop_idle, "installer idle fail-stop");
    run(full_cycle, "close/unmap -> isolated BATL allocation -> capture -> release/reopen x2");
    run(loss_cycle, "loss result and ordered ledger handed to postbattle UI without mutation");
    run(refused_postbattle_pak, "postbattle Pak refusal retains unconsumed result");
    run(refused_postbattle_pak_end, "postbattle Pak release refusal retains unconsumed result");
    run(close_pending, "retirement/quiescence pending blocks battle allocation and launch");
    run(refused_loader, "refused BATL binding fail-stops without re-entry");
    run(pinned_loader, "pinned BATL storage prevents reopening");
    run(invalid_frame, "invalid capture pauses and refuses result handoff");
    puts("STORY CAMPAIGN RUN HOST PASSED");
    return 0;
}
