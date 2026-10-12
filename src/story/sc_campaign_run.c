#ifdef STORY_NATIVE_PROBE
#include "sc_campaign_battle.h"
#ifdef SC_CAMPAIGN_BATTLE_WIRED
/* Resident campaign runner: Game_Thread state, Menu Select entry, battle
   launcher and the fragment-62 frame tick for the installed campaign. It only
   reads the resident installer control and the admitted DATA lease through
   it; every mutation of campaign values goes through sc_campaign_* entries. */
#include "sc_native_campaign.h"
#include "sc_native_loop.h"
#include "sc_runtime_probe.h"
#include "sc_battle_service.h"
#include "sc_batl_loader.h"
#include "src/roguelike/rl_battle.h"
#include "src/controller.h"
#include "src/29BA0.h"
#include "src/1CF30.h"
#include <stdint.h>

int gScCampaignBattle;
ScCampaignRun gScCampaignRun;

static RlU8 battle_map[6];
static unsigned map_count, last_active, last_enemy, last_hp, observed_any;
static ScBattleSnapshot last_party[6];
static int tick_failed, deferred_result, deferred_frame;
static ScMaHandle battle_loader_handle;
static ScMaLease battle_loader_lease;
static void *battle_loader;
static int battle_loader_live;

const RlU8 *sc_story_battle_mapping(void) {
    extern RlU8 gScProbeSixMapping[6];
    return gScProbeActive ? gScProbeSixMapping : battle_map;
}

static const ScNativeCampaignBacking *lease(void) {
    const ScCampaignControl *c = sc_campaign_control();
    const ScNativeCampaignBacking *b = &c->lifetime.loop.backing;
    if (!sc_campaign_active() || !c->lifetime.loop.acquired || !b->ui || !b->request ||
        !b->driver) return 0;
    return b;
}

int sc_campaign_battle_mapping(const RlU8 indices[6], unsigned count) {
    const ScCampaignControl *c = sc_campaign_control();
    const ScNativeCampaignBacking *b = lease();
    const ScState *state;
    if (!gScCampaignBattle || !indices) return SC_INVALID;
    if (c->sequential_active && gScCampaignRun.stage == SC_RUN_BATTLE_ACTIVE)
        state = &c->sequential_state;
    else if (b) state = &b->ui->state;
    else return SC_INVALID;
    if (sc_native_mapping(state, count, indices, battle_map) != SC_OK) return SC_INVALID;
    map_count = count;
    return SC_OK;
}

static int held(const ScCampaignControl *c) {
    return c->operation.phase == SC_NATIVE_OPERATION_HELD || c->lifetime.loop.held_operation;
}

/* Consumes the event queued by an observation or presents/resumes a held
   learning frame. Inside BATL this opens the story display frame within the
   native battle frame (unverified native seam; see NATIVE-UI.md). */
static int step_consumed(void) {
    const ScCampaignControl *c = sc_campaign_control();
    int status = sc_campaign_step();
    if (status != SC_OK && status != SC_PENDING) return SC_INVALID;
    if (!sc_campaign_active() || c->operation.queued) return SC_INVALID;
    return SC_OK;
}

static int snapshots_equal(const ScBattleSnapshot *a, const ScBattleSnapshot *b, unsigned n) {
    unsigned i, j;
    for (i = 0; i < n; i++) {
        if (a[i].hp != b[i].hp || a[i].status != b[i].status) return 0;
        for (j = 0; j < 4; j++) if (a[i].pp[j] != b[i].pp[j]) return 0;
    }
    return 1;
}

static int capture_sequential(RlBattleSide *player, RlBattleSide *enemy) {
    ScCampaignControl *c = (ScCampaignControl *)sc_campaign_control();
    const ScUiBattleRequest *request;
    ScBattleSnapshot party[6];
    RlBattleMon *live, *foe;
    unsigned active, opponent, count, hp;
    if (!c || !c->sequential_active || !player || !enemy ||
        !player->unk_724 || !enemy->unk_724) return SC_INVALID;
    request = &c->sequential_request;
    active = rl_battle_active_index(player);
    opponent = rl_battle_active_index(enemy);
    count = player->unk_724->unk_002;
    if (count != map_count || count > 6 || active >= count ||
        opponent >= request->enemy_count || opponent >= enemy->unk_724->unk_002)
        return SC_INVALID;
    live = rl_battle_mon(player);
    foe = rl_battle_mon(enemy);
    rl_battle_party_hp(rl_battle_party_mon(player, active)) = rl_battle_hp(live);
    rl_battle_party_status(rl_battle_party_mon(player, active)) = rl_battle_status(live);
    if (sc_native_capture(&c->sequential_state, player->unk_724->unk_01C,
            count, battle_map, active, party) != SC_OK) return SC_INVALID;
    hp = rl_battle_hp(foe);
    if (sc_combat_capture_ko(&c->sequential_bridge, &c->sequential_ledger,
            &request->progression, party, battle_map[active], opponent, hp) != SC_OK)
        return SC_INVALID;
    gScCampaignRun.observations++;
    return SC_OK;
}

static int tick(RlBattleSide *player, RlBattleSide *enemy) {
    const ScCampaignControl *c = sc_campaign_control();
    const ScNativeCampaignBacking *b;
    const ScState *state;
    ScBattleSnapshot party[6];
    RlBattleMon *live, *foe;
    unsigned active, opponent, count, hp, story_active, i;
    if (!gScCampaignBattle) return 0;
    if (tick_failed) return 1;
    if (gScCampaignRun.stage == SC_RUN_BATTLE_ACTIVE) {
        if (capture_sequential(player, enemy) != SC_OK) goto invalid;
        return 0;
    }
    b = lease();
    if (!b || !c->installed || !c->terminal.armed || !player || !enemy ||
        !player->unk_724 || !enemy->unk_724) goto invalid;
    state = &b->ui->state;
    active = rl_battle_active_index(player);
    opponent = rl_battle_active_index(enemy);
    count = player->unk_724->unk_002;
    if (count != map_count || count > 6 || active >= count ||
        opponent >= b->request->enemy_count || opponent >= enemy->unk_724->unk_002) goto invalid;
    live = rl_battle_mon(player);
    foe = rl_battle_mon(enemy);
    if (held(c)) {
        gScCampaignRun.pauses++;
        if (step_consumed() != SC_OK) goto invalid;
        if (held(c)) return 1;
        if (sc_story_publish_live(state, battle_map, player) != SC_OK) goto invalid;
        gScCampaignRun.publishes++;
        last_active = active;
    }
    if (last_active != active) {
        if (sc_story_publish_live(state, battle_map, player) != SC_OK) goto invalid;
        gScCampaignRun.publishes++;
        last_active = active;
    }
    rl_battle_party_hp(rl_battle_party_mon(player, active)) = rl_battle_hp(live);
    rl_battle_party_status(rl_battle_party_mon(player, active)) = rl_battle_status(live);
    if (sc_native_capture(state, player->unk_724->unk_01C, count, battle_map, active,
                          party) != SC_OK) goto invalid;
    story_active = battle_map[active];
    hp = (state->defeated_mask & (1U << opponent)) ? 1 : rl_battle_hp(foe);
    if (observed_any && story_active == last_enemy >> 8 && opponent == (last_enemy & 0xFF) &&
        hp == last_hp && snapshots_equal(party, last_party, state->party_count))
        return 0;
    if (sc_campaign_observe(party, story_active, opponent, hp) != SC_OK) goto invalid;
    gScCampaignRun.observations++;
    if (step_consumed() != SC_OK) goto invalid;
    observed_any = 1;
    last_enemy = (story_active << 8) | opponent;
    last_hp = hp;
    for (i = 0; i < 6; i++) last_party[i] = party[i];
    if (!hp && !held(c)) {
        if (sc_story_publish_live(state, battle_map, player) != SC_OK) goto invalid;
        gScCampaignRun.publishes++;
    }
    return held(c);
invalid:
    tick_failed = 1;
    gScCampaignRun.error = 1;
    osSyncPrintf("story campaign: invalid native observation; battle paused\n");
    return 1;
}

int sc_campaign_battle_gate(RlBattleSide *player, RlBattleSide *enemy, int *result) {
    if (!gScCampaignBattle) return 0;
    if (tick(player, enemy)) { *result = 0; return 1; }
    if (deferred_frame) {
        *result = deferred_result;
        deferred_frame = 0;
        return 2;
    }
    return 0;
}

int sc_campaign_battle_after(RlBattleSide *player, RlBattleSide *enemy, int result) {
    if (!gScCampaignBattle) return result;
    if (tick(player, enemy)) {
        deferred_result = result;
        deferred_frame = 1;
        return 0;
    }
    return result;
}

void sc_campaign_battle_fault(void) {
    if (!gScCampaignBattle) return;
    tick_failed = 1;
    gScCampaignRun.error = 1;
    osSyncPrintf("story campaign: unbound effect dispatch; battle paused\n");
}

int sc_campaign_battle_paused(void) {
    return gScCampaignBattle && (tick_failed || held(sc_campaign_control()));
}

/* A battle is launched once per RUNNING operation, only after the loop
   submitted it and installed a fresh, unarmed hook binding. */
static int launchable(const ScCampaignControl *c, const ScNativeCampaignBacking *b) {
    RlU32 id = c->operation.operation;
    return b && c->installed && id && id != gScCampaignRun.launched &&
        c->operation.phase == SC_NATIVE_OPERATION_RUNNING && !c->operation.queued &&
        !c->terminal.armed && !c->binding.drained && c->binding.status == SC_PENDING &&
        !c->lifetime.loop.held_operation && b->driver->launched &&
        b->driver->operation_id == id && b->request->operation_id == id;
}

static void failed(int status) {
    if (gScCampaignRun.stage != SC_RUN_FAILED)
        osSyncPrintf("story campaign: fail-stop %d; mapping and backing retained\n", status);
    gScCampaignRun.error = 1;
    gScCampaignRun.last_status = status;
    gScCampaignRun.stage = SC_RUN_FAILED;
}

/* An armed battle that aborted or whose drain was refused settles to a
   permanent owner-poll refusal with the hook retained: fail-stop instead of
   re-polling it every frame. */
static int settle_refused(const ScCampaignControl *c) {
    return c->installed && c->binding.drained && c->binding.status != SC_OK &&
        c->binding.status != SC_PENDING;
}

static void leave(void) {
    if (sc_campaign_control()->sequential_active &&
        gScCampaignRun.stage == SC_RUN_CLOSING) {
        gCurrentGameState = STATE_STORY_CAMPAIGN;
        return;
    }
    gScCampaignRun.stage = SC_RUN_ENTRY;
    gCurrentGameState = STATE_MENU_SELECT;
}

static void phase_stage(int status) {
    switch (sc_campaign_phase()) {
    case SC_CAMPAIGN_ACTIVE: gScCampaignRun.stage = SC_RUN_ACTIVE; break;
    case SC_CAMPAIGN_CLOSING: case SC_CAMPAIGN_RELEASING:
        gScCampaignRun.stage = SC_RUN_CLOSING; break;
    case SC_CAMPAIGN_FAILED: failed(status); break;
    default: gScCampaignRun.refusals++; leave(); break;
    }
}

int sc_campaign_state_poll(void) {
    const ScCampaignControl *c = sc_campaign_control();
    const ScNativeCampaignBacking *b;
    ScMaArena *arena;
    int status;
    switch (gScCampaignRun.stage) {
    case SC_RUN_ENTRY:
        arena = sc_service_native_arena();
        if (!arena) {
            osSyncPrintf("story campaign: requires the 8 MiB expansion; not opened\n");
            gScCampaignRun.refusals++;
            leave();
            return 0;
        }
        /* Operation ids restart with each admitted lifetime. */
        gScCampaignRun.launched = 0;
        status = sc_campaign_open(arena, osGetCount());
        gScCampaignRun.last_status = status;
        phase_stage(status);
        return 0;
    case SC_RUN_ACTIVE:
        if (!sc_campaign_active()) { phase_stage(SC_INVALID); return 0; }
        b = lease();
        if (launchable(c, b)) return 1;
        status = sc_campaign_step();
        gScCampaignRun.last_status = status;
        if (!sc_campaign_active()) { phase_stage(status); return 0; }
        if (settle_refused(c)) { sc_campaign_fail_stop(); failed(status); return 0; }
        if (c->lifetime.loop.exited) gScCampaignRun.stage = SC_RUN_CLOSING;
        return 0;
    case SC_RUN_CLOSING:
        status = sc_campaign_close();
        gScCampaignRun.last_status = status;
        if (status == SC_OK) {
            if (c->sequential_active && !c->sequential_result_ready) {
                gScCampaignRun.stage = SC_RUN_BATTLE_READY;
                gCurrentGameState = STATE_STORY_CAMPAIGN;
            } else leave();
            return 0;
        }
        if (status != SC_PENDING) { failed(status); return 0; }
        /* Still live while ACTIVE: keep stepping so held/queued/drained work
           settles; CLOSING/RELEASING retries only the retirement. */
        if (sc_campaign_active() && !c->lifetime.loop.exited) {
            status = sc_campaign_step();
            if (!sc_campaign_active()) phase_stage(status);
            else if (settle_refused(c)) { sc_campaign_fail_stop(); failed(status); }
        }
        return 0;
    case SC_RUN_BATTLE_READY:
        return 1;
    case SC_RUN_REOPEN:
        arena = sc_service_native_arena();
        if (!arena) { failed(SC_UNAVAILABLE); return 0; }
        status = sc_campaign_open(arena, c->seed);
        gScCampaignRun.last_status = status;
        phase_stage(status);
        return 0;
    case SC_RUN_FAILED:
    default:
        return 0;
    }
}

static int acquire_battle_loader(ScMaArena *arena) {
    RlU32 address, aligned, offset;
    unsigned char *mapped;
    if (!arena || battle_loader_live ||
        !sc_ma_alloc(arena, SC_NATIVE_BATL_LOADER_BYTES + 64U, &battle_loader_handle))
        return 0;
    battle_loader_lease.ticket = 0;
    if (!sc_ma_data(arena, battle_loader_handle, &battle_loader_lease)) {
        if (!sc_ma_release(arena, battle_loader_handle)) battle_loader_live = 1;
        return 0;
    }
    battle_loader_live = 1;
    address = sc_ma_address(arena, battle_loader_handle);
    aligned = (address + 63U) & ~63U;
    if (address < 0x80400000U || aligned < address ||
        aligned > 0x807da7e0U - SC_NATIVE_BATL_LOADER_BYTES)
        return 0;
    offset = aligned - address;
    mapped = sc_ma_map(arena, &battle_loader_lease, offset,
        SC_NATIVE_BATL_LOADER_BYTES, SC_MA_READ | SC_MA_WRITE);
    if (!mapped || ((uintptr_t)mapped & 63U)) return 0;
    battle_loader = mapped;
    return 1;
}

static int release_battle_loader(ScMaArena *arena) {
    if (!arena || !battle_loader_live || sc_batl_loader_live()) return 0;
    if ((battle_loader_lease.ticket && !sc_ma_end(arena, &battle_loader_lease)) ||
        !sc_ma_release(arena, battle_loader_handle)) return 0;
    battle_loader = 0;
    battle_loader_live = 0;
    return 1;
}

static void sequential_fail(int status, const char *message) {
    osSyncPrintf("%s\n", message);
    sc_campaign_fail_stop();
    gScCampaignRun.last_status = status;
    gScCampaignRun.error = 1;
    gScCampaignRun.stage = SC_RUN_FAILED;
}

/* The UI request is copied to resident values, detached, and fully retired
   before the standard battle runs. Only the BATL DATA span is reacquired for
   the battle; the framebuffers, UI slab and shell image are not retained. */
void sc_campaign_state_launch(void) {
    /* Single Game_Thread launcher scratch; battle callbacks never reenter it. */
    static const ScCampaignControl *c;
    static const ScNativeCampaignBacking *b;
    static const RlBattleRules rules = {3, 0, 0x20, 0, 0, 0, 1, 0, 0};
    static ScCampaignControl *mutable;
    static const ScUiBattleRequest *request;
    static ScMaArena *arena;
    static int status;
    c = sc_campaign_control();
    b = lease();
    mutable = (ScCampaignControl *)c;
    if (gScCampaignRun.stage == SC_RUN_ACTIVE && sc_campaign_active() &&
        launchable(c, b)) {
        mutable->sequential_request = *b->request;
        mutable->sequential_state = b->ui->state;
        mutable->sequential_bridge = b->ui->bridge;
        sc_ko_ledger_init(&mutable->sequential_ledger);
        if (sc_ko_ledger_bind(&mutable->sequential_ledger,
                &mutable->sequential_request.progression,
                mutable->sequential_request.operation_id) != SC_OK) {
            sequential_fail(SC_INVALID, "story campaign: KO ledger binding refused");
            return;
        }
        mutable->sequential_active = 1;
        mutable->sequential_result_ready = 0;
        gScCampaignRun.stage = SC_RUN_CLOSING;
        if (sc_campaign_detach_battle() != SC_OK)
            sequential_fail(SC_INVALID, "story campaign: battle request detach refused");
        return;
    }
    if (gScCampaignRun.stage != SC_RUN_BATTLE_READY ||
        !mutable->sequential_active || mutable->sequential_result_ready)
        return;
    arena = sc_service_native_arena();
    if (!arena || !acquire_battle_loader(arena) ||
        !sc_batl_loader_bind(battle_loader, SC_NATIVE_BATL_LOADER_BYTES)) {
        if (battle_loader_live && !sc_batl_loader_live() &&
            !release_battle_loader(arena)) {
            sequential_fail(SC_UNAVAILABLE, "story campaign: unbound BATL rollback refused");
            return;
        }
        sequential_fail(SC_UNAVAILABLE, "story campaign: battle BATL storage refused");
        return;
    }
    request = &mutable->sequential_request;
    gScCampaignRun.launched = request->operation_id;
    gScCampaignRun.battles++;
    map_count = 0;
    last_active = 255;
    observed_any = 0;
    tick_failed = deferred_frame = deferred_result = 0;
    gScCampaignRun.stage = SC_RUN_BATTLE_ACTIVE;
    gScCampaignBattle = 1;
    status = rl_start_battle(request->party, request->party_count, request->enemy,
        request->enemy_count, &rules, STATE_STORY_CAMPAIGN, &mutable->sequential_result);
    gScCampaignBattle = 0;
    gScCampaignRun.last_outcome = status;
    if (!sc_batl_loader_unbind()) {
        sequential_fail(SC_UNAVAILABLE, "story campaign: BATL loader remains pinned");
        return;
    }
    if (!release_battle_loader(arena)) {
        sequential_fail(SC_UNAVAILABLE, "story campaign: BATL allocation retirement refused");
        return;
    }
    if (status < 0 || tick_failed || status > RL_LAUNCH_DRAW ||
        mutable->sequential_result.outcome != (unsigned)status) {
        sequential_fail(SC_INVALID, "story campaign: battle did not return a valid result");
        return;
    }
    mutable->sequential_outcome = status == RL_BATTLE_WIN ? SC_WIN :
        status == RL_BATTLE_LOSS ? SC_LOSS : SC_DRAW;
    mutable->sequential_result_ready = 1;
    gScCampaignRun.stage = SC_RUN_REOPEN;
    gCurrentGameState = STATE_STORY_CAMPAIGN;
}


void sc_campaign_state(void) {
    if (sc_campaign_state_poll()) sc_campaign_state_launch();
}

/* Visible Menu Select entry (docs/story/NATIVE-UI.md "Menu Select entry").
   R on the top page requests the campaign only on an 8 MiB console; the
   request rides fragment57's vanilla B exit and is consumed here. */
static int menu_request;

static int menu_available(void) {
    return osMemSize == 0x800000 && gScCampaignRun.stage == SC_RUN_ENTRY &&
           sc_campaign_phase() == SC_CAMPAIGN_IDLE;
}

int sc_campaign_menu_request(void) {
    menu_request = menu_available();
    return menu_request;
}

void sc_campaign_menu_hint(void) {
    func_8001F3F4();
    func_8001EBE0(0x10, 0);
    if (osMemSize == 0x800000) {
        func_8001F324(0xFF, 0xF0, 0x64, 0xFF);
        func_8001F1E8(0x50, 0x13A, "R: STORY CAMPAIGN");
    } else {
        func_8001F324(0xC8, 0xC8, 0xC8, 0xFF);
        func_8001F1E8(0x50, 0x13A, "STORY CAMPAIGN NEEDS EXPANSION PAK");
    }
    func_8001F444();
}

int sc_campaign_menu_hook(int state) {
    int requested = menu_request;
    menu_request = 0;
    if (requested && state == STATE_TITLE_SCREEN && menu_available()) return STATE_STORY_CAMPAIGN;
    return state;
}
#endif
#endif
