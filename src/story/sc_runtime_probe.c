#ifdef STORY_NATIVE_PROBE
#include "sc_runtime_probe.h"
#include "sc_cold.h"
#include "src/roguelike/rl_battle.h"
#include "src/29BA0.h"

volatile ScProbeMailbox gScProbe;
const RlU32 gScProbeCompiledSize = sizeof(gScProbe);
const RlU32 gScProbePartyOffset = (RlU32)&((ScState *)0)->party;
const RlU32 gScProbePartyCountOffset = (RlU32)&((ScState *)0)->party_count;
const RlU32 gScProbeMonSize = sizeof(RlMon);
const RlU32 gScBridgeProbeSize = sizeof(gScBridgeProbe);
int gScProbeActive;
volatile ScBridgeProbe gScBridgeProbe;
ScProgressProbe gScProgressProbe;
const RlU32 gScProgressProbeSize = sizeof(gScProgressProbe);
const RlU32 gScProgressMonOffset = (RlU32)&((ScProgressProbe *)0)->before_ko;
#ifdef STORY_KO_PROBE
const RlU32 gScProbeKoProfile = 1;
#endif
#ifdef STORY_EMBER_PROBE
const RlU32 gScProbeEmberProfile = 1;
#endif
static ScState resident;
static ScBridge bridge;
static ScCombat combat;
static RlMon opponents[6];
static RlU8 mapping[6];
static unsigned last_active = 255;
static int deferred_result;
static int deferred_frame;
static int failed;
static unsigned pending_outcome = 255;

static int terminal_matches(const RlMon *a, const RlMon *b) {
    unsigned i;
    if (a->species != b->species || a->level != b->level || a->exp != b->exp ||
        a->dvs != b->dvs || a->cur_hp != b->cur_hp || a->max_hp != b->max_hp ||
        a->status != b->status || a->fainted != b->fainted) return 0;
    for (i = 0; i < 5; i++)
        if (a->current_stats[i] != b->current_stats[i] || a->stat_exp[i] != b->stat_exp[i])
            return 0;
    for (i = 0; i < 4; i++)
        if (a->moves[i] != b->moves[i] || a->pp[i] != b->pp[i] || a->max_pp[i] != b->max_pp[i])
            return 0;
    return 1;
}

static unsigned staged(unsigned value, unsigned stage) {
    static const unsigned numerator[13] = {25,28,33,40,50,66,100,150,200,250,300,350,400};
    if (stage < 1 || stage > 13) return 0;
    value = value * numerator[stage - 1] / 100;
    if (!value) value = 1;
    return value > 999 ? 999 : value;
}

static void report(void) {
    gScProbe.phase = resident.phase;
    gScProbe.exp = resident.party[0].exp;
    gScProbe.level = resident.party[0].level;
    gScProbe.hp = resident.party[0].cur_hp;
    gScProbe.max_hp = resident.party[0].max_hp;
    gScProbe.dvs = resident.party[0].dvs;
    gScProbe.state_address = (RlU32)&resident;
    gScProbe.state_size = sizeof(resident);
    gScProbe.completed_crc = sc_save_crc(resident.completed, SC_BITS);
    gScProbe.combat_address = (RlU32)&combat;
    gScProbe.combat_size = sizeof(combat);
    gScProbe.combat_crc = sc_save_crc((const RlU8 *)&combat, sizeof(combat));
}

static void bridge_views(RlBattleSide *side, volatile ScBridgeView *party,
                         volatile ScBridgeView *view) {
    const RlBattlePartyMon *native = rl_battle_party_mon(side, 0);
    const RlBattleMon *live = rl_battle_mon(side);
    unsigned i;
    party->species = rl_battle_party_species(native);
    party->level = rl_battle_party_level(native);
    party->exp = native->unk_10;
    party->dvs = native->unk_1E;
    party->hp = rl_battle_party_hp(native);
    party->max_hp = rl_battle_party_max_hp(native);
    party->status = rl_battle_party_status(native);
    party->stats[0] = native->unk_26; party->stats[1] = native->unk_28;
    party->stats[2] = native->unk_2A; party->stats[3] = native->unk_2C;
    party->stats[4] = native->unk_2E;
    view->species = rl_battle_species(live);
    view->level = rl_battle_level(live);
    view->dvs = live->unk_24;
    view->hp = rl_battle_hp(live); view->max_hp = rl_battle_max_hp(live);
    view->status = rl_battle_status(live);
    view->stats[0] = rl_battle_max_hp(live); view->stats[1] = rl_battle_attack(live);
    view->stats[2] = rl_battle_defense(live); view->stats[3] = rl_battle_speed(live);
    view->stats[4] = rl_battle_special(live);
    view->base[0] = rl_battle_base_attack(live); view->base[1] = rl_battle_base_defense(live);
    view->base[2] = rl_battle_base_speed(live); view->base[3] = rl_battle_base_special(live);
    for (i = 0; i < 4; i++) {
        party->moves[i] = rl_battle_party_move(native, i);
        party->pp[i] = rl_battle_party_packed_pp(native, i);
        view->moves[i] = rl_battle_move(live, i);
        view->pp[i] = rl_battle_packed_pp(live, i);
    }
    for (i = 0; i < 6; i++) view->stages[i] = rl_battle_stage(live, i);
}

#ifndef STORY_KO_PROBE
static int bridge_matches(const volatile ScBridgeView *party,
                           const volatile ScBridgeView *live) {
    const RlMon *mon = &resident.party[0];
    unsigned i, value;
    if (party->species != mon->species || live->species != mon->species ||
        party->level != mon->level || live->level != mon->level ||
        party->exp != mon->exp || party->dvs != mon->dvs || live->dvs != mon->dvs ||
        party->hp != mon->cur_hp || live->hp != mon->cur_hp ||
        party->max_hp != mon->max_hp || live->max_hp != mon->max_hp ||
        party->status != mon->status || live->status != mon->status ||
        live->stats[0] != mon->max_hp) return 0;
    for (i = 0; i < 5; i++)
        if (party->stats[i] != mon->current_stats[i]) return 0;
    for (i = 0; i < 6; i++)
        if (live->stages[i] != RL_BATTLE_STAGE_NEUTRAL) return 0;
    for (i = 0; i < 4; i++) {
        if (party->moves[i] != mon->moves[i] || live->moves[i] != mon->moves[i] ||
            party->pp[i] != mon->pp[i] || live->pp[i] != mon->pp[i] ||
            live->base[i] != mon->current_stats[i + 1]) return 0;
        value = mon->current_stats[i + 1];
        if (i == 0 && (mon->status & RL_BATTLE_STATUS_BURN)) value /= 2;
        if (i == 2 && (mon->status & RL_BATTLE_STATUS_PARALYSIS)) value /= 4;
        if (live->stats[i + 1] != (value ? value : 1)) return 0;
    }
    return 1;
}
#endif

static int bridge_view_equal(const volatile ScBridgeView *a, const volatile ScBridgeView *b) {
    unsigned i;
    if (a->species != b->species || a->level != b->level || a->exp != b->exp ||
        a->dvs != b->dvs || a->hp != b->hp || a->max_hp != b->max_hp ||
        a->status != b->status) return 0;
    for (i = 0; i < 4; i++)
        if (a->moves[i] != b->moves[i] || a->pp[i] != b->pp[i] || a->base[i] != b->base[i])
            return 0;
    for (i = 0; i < 5; i++) if (a->stats[i] != b->stats[i]) return 0;
    for (i = 0; i < 6; i++) if (a->stages[i] != b->stages[i]) return 0;
    return 1;
}

#ifndef STORY_KO_PROBE
static int bridge_hold(RlBattleSide *player, RlBattleSide *enemy) {
    unsigned i, error = 1;
    if (!player || !enemy || !player->unk_724 || !enemy->unk_724 ||
        resident.party_count != 1 || player->unk_724->unk_002 != 1 ||
        enemy->unk_724->unk_002 != 1 || rl_battle_active_index(player) != 0 ||
        rl_battle_active_index(enemy) != 0 || !rl_battle_hp(rl_battle_mon(enemy)))
        goto invalid;
    bridge_views(player, &gScBridgeProbe.current_party, &gScBridgeProbe.current_live);
    if (gScBridgeProbe.boundary) {
        const volatile ScBridgeView *party = gScBridgeProbe.boundary == 1 ?
            &gScBridgeProbe.initial_party : &gScBridgeProbe.carry_party;
        const volatile ScBridgeView *live = gScBridgeProbe.boundary == 1 ?
            &gScBridgeProbe.initial_live : &gScBridgeProbe.carry_live;
        error = 5;
        if (gScBridgeProbe.boundary > 2 ||
            !bridge_view_equal(&gScBridgeProbe.current_party, party) ||
            !bridge_view_equal(&gScBridgeProbe.current_live, live)) goto invalid;
    }
    if (!gScBridgeProbe.boundary) {
        bridge_views(player, &gScBridgeProbe.initial_party, &gScBridgeProbe.initial_live);
        error = 2;
        if (gScBridgeProbe.action ||
            !bridge_matches(&gScBridgeProbe.initial_party, &gScBridgeProbe.initial_live))
            goto invalid;
        gScBridgeProbe.boundary = 1;
    } else if (gScBridgeProbe.action) {
        error = 3;
        if (gScBridgeProbe.boundary != 1 || gScBridgeProbe.action != 1) goto invalid;
        gScBridgeProbe.action = 0;
        /* Synthetic carry changes persistence only; paralysis and all stages stay intact. */
        rl_battle_party_hp(rl_battle_party_mon(player, 0)) = rl_battle_hp(rl_battle_mon(player)) = 16;
        rl_battle_party_packed_pp(rl_battle_party_mon(player, 0), 0) =
            rl_battle_packed_pp(rl_battle_mon(player), 0) = 6;
        rl_battle_party_status(rl_battle_party_mon(player, 0)) =
            rl_battle_status(rl_battle_mon(player)) = 0x48;
        bridge_views(player, &gScBridgeProbe.carry_party, &gScBridgeProbe.carry_live);
        error = 4;
        if (sc_native_observe_combat(&resident, &bridge, &combat,
                player->unk_724->unk_01C, 1, mapping, 0, 0,
                rl_battle_hp(rl_battle_mon(enemy))) != SC_OK ||
            !bridge_matches(&gScBridgeProbe.carry_party, &gScBridgeProbe.carry_live) ||
            resident.party[0].cur_hp != 16 || resident.party[0].pp[0] != 6 ||
            resident.party[0].status != 0x48 || resident.party[0].exp != 235 ||
            resident.phase != SC_BATTLE || resident.defeated_mask || resident.learn_mask)
            goto invalid;
        for (i = 1; i < 4; i++)
            if (resident.party[0].pp[i] != gScBridgeProbe.initial_party.pp[i]) goto invalid;
        gScBridgeProbe.boundary = 2;
        bridge_views(player, &gScBridgeProbe.current_party, &gScBridgeProbe.current_live);
    }
    report();
    return 1;
invalid:
    gScBridgeProbe.error = error;
    failed = 1;
    gScProbe.status = 0x100 + SC_INVALID;
    osSyncPrintf("story probe: bridge boundary error %u; battle held before publication\n", error);
    report();
    return 1;
}
#endif

static int publish_live(RlBattleSide *side) {
    RlBattleMon *live = rl_battle_mon(side);
    unsigned index = rl_battle_active_index(side), i, value;
    const RlMon *mon;
    RlU16 *base[4], *stats[4];
    if (index >= side->unk_724->unk_002 || index >= 6) return SC_INVALID;
    mon = &resident.party[mapping[index]];
    if (rl_battle_species(live) != mon->species) return SC_INVALID;
    for (i = 0; i < 4; i++)
        if (rl_battle_stage(live, i) < 1 || rl_battle_stage(live, i) > 13) return SC_INVALID;
    if (sc_native_publish(&resident, side->unk_724->unk_01C, side->unk_724->unk_002,
                          mapping) != SC_OK) return SC_INVALID;
    rl_battle_level(live) = mon->level;
    rl_battle_hp(live) = mon->cur_hp;
    rl_battle_max_hp(live) = mon->max_hp;
    if (live->unk_4E & 8) return SC_OK; /* Transform owns its copied stats, moves and PP. */
    base[0] = &rl_battle_base_attack(live); base[1] = &rl_battle_base_defense(live);
    base[2] = &rl_battle_base_speed(live); base[3] = &rl_battle_base_special(live);
    stats[0] = &rl_battle_attack(live); stats[1] = &rl_battle_defense(live);
    stats[2] = &rl_battle_speed(live); stats[3] = &rl_battle_special(live);
    for (i = 0; i < 4; i++) {
        *base[i] = mon->current_stats[i + 1];
        value = staged(*base[i], rl_battle_stage(live, i));
        if (!value) return SC_INVALID;
        if (i == 0 && (mon->status & RL_BATTLE_STATUS_BURN)) value /= 2;
        if (i == 2 && (mon->status & RL_BATTLE_STATUS_PARALYSIS)) value /= 4;
        *stats[i] = value ? value : 1;
    }
    for (i = 0; i < 4; i++) {
        rl_battle_move(live, i) = mon->moves[i];
        rl_battle_packed_pp(live, i) = mon->pp[i];
    }
    return SC_OK;
}

int sc_runtime_probe_tick(RlBattleSide *player, RlBattleSide *enemy) {
    RlBattleMon *live, *foe;
    unsigned active, opponent, count, i;
    int status;
    if (!gScProbeActive) return 0;
    if (failed) return 1;
#ifndef STORY_KO_PROBE
    if (gScBridgeProbe.enabled) return bridge_hold(player, enemy);
#endif
    gScProbe.ticks++;
    if (!player || !enemy || !player->unk_724 || !enemy->unk_724) goto invalid;
    active = rl_battle_active_index(player);
    opponent = rl_battle_active_index(enemy);
    count = player->unk_724->unk_002;
    if (active >= count || count > 6 || opponent >= bridge.enemy_count) goto invalid;
    live = rl_battle_mon(player); foe = rl_battle_mon(enemy);
    if (last_active != active) {
        if (publish_live(player) != SC_OK) goto invalid;
        last_active = active;
    }
    if (resident.phase == SC_LEARNING) {
        gScProbe.pause_ticks++;
        bridge_views(player, &gScBridgeProbe.current_party, &gScBridgeProbe.current_live);
        if (!bridge_view_equal(&gScBridgeProbe.current_party, &gScBridgeProbe.initial_party) ||
            !bridge_view_equal(&gScBridgeProbe.current_live, &gScBridgeProbe.initial_live))
            goto invalid;
        if (!gScProbe.resume) { report(); return 1; }
        if (gScProgressProbe.resumes) goto invalid;
        gScProbe.resume = 0;
        for (i = 0; i < resident.party_count; i++)
            if (resident.learn_mask & (1U << i))
                if (!combat.ready || sc_learn_pp(&resident, combat.pp, i, 3, 1) != SC_OK)
                    goto invalid;
        if (publish_live(player) != SC_OK) goto invalid;
        gScProgressProbe.resumes++;
        gScProgressProbe.after_resume = resident.party[0];
        for (i = 0; i < 6; i++) gScProgressProbe.resume_stages[i] = rl_battle_stage(live, i);
        bridge_views(player, &gScBridgeProbe.carry_party, &gScBridgeProbe.carry_live);
    }
    if (gScProbe.force_ko) {
#ifdef STORY_EMBER_PROBE
        goto invalid;
#else
        gScProgressProbe.injected++;
        rl_battle_stage(live, 0) = 8; /* Synthetic +1 stage exercises KO refresh. */
        rl_battle_hp(foe) = 0;
        rl_battle_party_hp(rl_battle_party_mon(enemy, opponent)) = 0;
        gScProbe.force_ko = 0;
#endif
    }
    rl_battle_party_hp(rl_battle_party_mon(player, active)) = rl_battle_hp(live);
    rl_battle_party_status(rl_battle_party_mon(player, active)) = rl_battle_status(live);
    {
        unsigned already = resident.defeated_mask & (1U << opponent);
        if (!already && !rl_battle_hp(foe)) {
            if (gScProgressProbe.ko_captures || count != 1 || active || opponent) goto invalid;
            gScProgressProbe.before_ko = resident.party[0];
            gScProgressProbe.before_ko.cur_hp = rl_battle_hp(live);
            gScProgressProbe.before_ko.status = rl_battle_status(live);
            for (i = 0; i < 4; i++)
                gScProgressProbe.before_ko.pp[i] =
                    rl_battle_party_packed_pp(rl_battle_party_mon(player, active), i) & 63;
            gScProgressProbe.award = (RlU32)combat.enemy_species[0].yield *
                combat.team.mons[0].level / 7;
            if (combat.kind == SC_TRAINER || combat.kind == SC_RIVAL)
                gScProgressProbe.award += gScProgressProbe.award / 2;
        }
        status = sc_native_observe_combat(&resident, &bridge, &combat,
                                   player->unk_724->unk_01C, count, mapping,
                                   active, opponent, already ? 1 : rl_battle_hp(foe));
        if (status != SC_OK) goto invalid;
        if (!already && !rl_battle_hp(foe)) {
            gScProbe.ko_count++;
            if (publish_live(player) != SC_OK) goto invalid;
            gScProgressProbe.ko_captures++;
            gScProgressProbe.after_ko = resident.party[0];
            gScProgressProbe.learn_move = resident.learn_moves[0];
            for (i = 0; i < 6; i++) gScProgressProbe.ko_stages[i] = rl_battle_stage(live, i);
            bridge_views(player, &gScBridgeProbe.initial_party, &gScBridgeProbe.initial_live);
            bridge_views(player, &gScBridgeProbe.current_party, &gScBridgeProbe.current_live);
            if (resident.party[0].exp != gScProgressProbe.before_ko.exp + gScProgressProbe.award)
                goto invalid;
            for (i = 0; i < 5; i++)
                if (resident.party[0].stat_exp[i] !=
                    gScProgressProbe.before_ko.stat_exp[i] + combat.enemy_species[0].base[i])
                    goto invalid;
        }
    }
    gScProbe.live_hp = rl_battle_hp(live);
    gScProbe.live_max_hp = rl_battle_max_hp(live);
    gScProbe.live_attack = rl_battle_attack(live);
    gScProbe.live_base_attack = rl_battle_base_attack(live);
    gScProbe.attack_stage = rl_battle_stage(live, 0);
    gScProbe.native_max_hp = rl_battle_party_max_hp(rl_battle_party_mon(player, active));
    gScProbe.native_dvs = rl_battle_party_mon(player, active)->unk_1E;
    gScProbe.live_dvs = live->unk_24;
    report();
    return resident.phase == SC_LEARNING;
invalid:
    gScProgressProbe.error = 1;
    failed = 1;
    gScProbe.status = 0x100 + SC_INVALID;
    osSyncPrintf("story probe: invalid native observation; battle paused\n");
    return 1;
}

int sc_runtime_probe_gate(RlBattleSide *player, RlBattleSide *enemy, int *result) {
    if (!gScProbeActive) return 0;
    if (sc_runtime_probe_tick(player, enemy)) { *result = 0; return 1; }
    if (deferred_frame) {
        *result = deferred_result;
        deferred_frame = 0;
        return 2;
    }
    return 0;
}

int sc_runtime_probe_after(RlBattleSide *player, RlBattleSide *enemy, int result) {
    if (!gScProbeActive) return result;
    if (sc_runtime_probe_tick(player, enemy)) {
        deferred_result = result;
        deferred_frame = 1;
        return 0;
    }
    return result;
}

int sc_runtime_probe_paused(void) {
    return gScProbeActive && (failed || gScBridgeProbe.enabled || resident.phase == SC_LEARNING);
}

static int commit_result(void) {
    ScColdRequest request = {0};
    int status;
    if (pending_outcome > SC_WIN) return 0x100 + SC_INVALID;
    request.state = &resident;
    request.command = SC_COLD_COMMIT;
    request.controller = gScProbe.controller;
    request.outcome = pending_outcome;
    status = sc_cold_call(&request);
    if (status == SC_SAVE_OK) pending_outcome = 255;
    return status;
}

int sc_runtime_probe_poll(void) {
    ScColdRequest request = {0};
    RlBattleRules rules = {3,0,0x20,0,1,0,1,0};
    static RlLaunchResult result;
    unsigned command = gScProbe.command, i;
    int status;
    if (!command) return 0;
#ifdef STORY_KO_PROBE
    if (command == 6) {
        osSyncPrintf("story probe: bridge command unavailable in KO profile\n");
        gScProbe.status = 0x100 + SC_INVALID;
        gScProbe.command = 0;
        return 1;
    }
#endif
    gScProbe.stage = 1;
    request.state = &resident;
    request.controller = gScProbe.controller;
    request.create = gScProbe.create;
    request.seed = gScProbe.seed;
    if (command == 1) {
        request.command = SC_COLD_LOAD;
        status = sc_cold_call(&request);
        if (status == SC_SAVE_OK || status == SC_SAVE_RECOVERED) {
            pending_outcome = 255;
            combat.ready = 0;
            gScProbe.interrupted = request.interrupted;
        }
    } else if (command == 2 || command == 3 || command == 5 || command == 6) {
        if (pending_outcome != 255) { status = 0x100 + SC_PENDING; goto end; }
        combat.ready = 0;
        request.command = command == 2 ? SC_COLD_NEW : (command == 5 ? SC_COLD_CHECKPOINT :
            (command == 6 ? SC_COLD_BRIDGE : SC_COLD_PREPARE));
        request.combat = &combat;
        request.opponents = opponents;
        status = sc_cold_call(&request);
        gScProbe.stage = 5;
        if (status != SC_SAVE_OK || (command != 3 && command != 6)) goto end;
        {
            volatile unsigned char *values = (volatile unsigned char *)&gScBridgeProbe;
            for (i = 0; i < sizeof(gScBridgeProbe); i++) values[i] = 0;
        }
        {
            unsigned char *values = (unsigned char *)&gScProgressProbe;
            for (i = 0; i < sizeof(gScProgressProbe); i++) values[i] = 0;
        }
        gScBridgeProbe.enabled = command == 6;
        for (i = 0; i < 6; i++) { mapping[i] = (RlU8)i; bridge.participants[i] = 0; }
        bridge.enemy_count = request.enemy_count;
        gScProbe.stage = 6;
        last_active = 255;
        deferred_result = deferred_frame = failed = 0;
        gScProbe.resume = gScProbe.force_ko = 0;
        gScProbe.ticks = gScProbe.ko_count = gScProbe.pause_ticks = 0;
        gScProbe.logic_steps = gScProbe.presentation_steps = 0;
        gScProbeActive = 1; gScProbe.active = 1;
        status = rl_start_battle(resident.party, resident.party_count, opponents, request.enemy_count,
                                 &rules, STATE_MENU_SELECT, &result);
        if (status >= 0 && command == 3) {
            gScProgressProbe.terminal_resident = resident.party[0];
            gScProgressProbe.terminal_native = result.player[0];
            gScProgressProbe.terminal = 1;
            if (failed || result.player_count != 1 || result.player_index[0] != 0 ||
                !terminal_matches(&gScProgressProbe.terminal_resident,
                                  &gScProgressProbe.terminal_native)) {
                gScProgressProbe.error = 2;
                status = -1;
                osSyncPrintf("story probe: terminal native/resident carry mismatch before commit\n");
            }
        }
        gScProbeActive = 0; gScProbe.active = 0;
        combat.ready = 0;
        if (status < 0) { status = 0x100 + SC_INVALID; goto end; }
        if (result.outcome == RL_LAUNCH_DRAW) {
            pending_outcome = SC_DRAW;
            status = 0x100 + SC_PENDING;
            osSyncPrintf("story probe: native draw unresolved; reload prebattle checkpoint to retry\n");
            goto end;
        }
        pending_outcome = result.outcome == RL_BATTLE_WIN ? SC_WIN : SC_LOSS;
        status = commit_result();
        gScProbe.post_pool_crc = sc_save_crc(resident.completed, SC_BITS);
    } else if (command == 4) {
        status = commit_result();
        gScProbe.post_pool_crc = sc_save_crc(resident.completed, SC_BITS);
    } else status = 0x100 + SC_INVALID;
end:
    report();
    gScProbe.status = status;
    gScProbe.command = 0;
    return 1;
}
#endif
