#ifdef STORY_NATIVE_PROBE
#include "sc_runtime_probe.h"
#include "sc_cold.h"
#include "src/roguelike/rl_battle.h"
#include "src/29BA0.h"

volatile ScProbeMailbox gScProbe;
const RlU32 gScProbeCompiledSize = sizeof(gScProbe);
int gScProbeActive;
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
        if (!gScProbe.resume) { report(); return 1; }
        gScProbe.resume = 0;
        for (i = 0; i < resident.party_count; i++)
            if (resident.learn_mask & (1U << i))
                if (!combat.ready || sc_learn_pp(&resident, combat.pp, i, 3, 1) != SC_OK)
                    goto invalid;
        if (publish_live(player) != SC_OK) goto invalid;
    }
    if (gScProbe.force_ko) {
        rl_battle_stage(live, 0) = 8; /* Synthetic +1 stage exercises KO refresh. */
        rl_battle_hp(foe) = 0;
        rl_battle_party_hp(rl_battle_party_mon(enemy, opponent)) = 0;
        gScProbe.force_ko = 0;
    }
    rl_battle_party_hp(rl_battle_party_mon(player, active)) = rl_battle_hp(live);
    rl_battle_party_status(rl_battle_party_mon(player, active)) = rl_battle_status(live);
    {
        unsigned already = resident.defeated_mask & (1U << opponent);
        status = sc_native_observe_combat(&resident, &bridge, &combat,
                                   player->unk_724->unk_01C, count, mapping,
                                   active, opponent, already ? 1 : rl_battle_hp(foe));
        if (status != SC_OK) goto invalid;
        if (!already && !rl_battle_hp(foe)) {
            gScProbe.ko_count++;
            if (publish_live(player) != SC_OK) goto invalid;
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
    return gScProbeActive && (failed || resident.phase == SC_LEARNING);
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
    } else if (command == 2 || command == 3) {
        if (pending_outcome != 255) { status = 0x100 + SC_PENDING; goto end; }
        combat.ready = 0;
        request.command = command == 2 ? SC_COLD_NEW : SC_COLD_PREPARE;
        request.combat = &combat;
        request.opponents = opponents;
        status = sc_cold_call(&request);
        gScProbe.stage = 5;
        if (status != SC_SAVE_OK || command == 2) goto end;
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
