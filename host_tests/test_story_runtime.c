#include <string.h>
#include <stdio.h>
#include "src/story/sc_runtime_probe.c"

static unsigned failure, writes;
static int fail_final;
static int native_result = 1;
static int launch_outcome = RL_BATTLE_WIN;
static int fail_initialization;
static int bridge_case;
static unsigned char saved[SC_SAVE_SIZE];
static RlBattleSide sides[2];
static unk_D_800AE540_0004 teams[2];

#define check(condition) do { if (!(condition)) { failure = __LINE__; return -1; } } while (0)

void osSyncPrintf(const char *format, ...) { (void)format; }
static int cold_calls, cold_active;
static int fail_cold;
int sc_cold_call(ScColdRequest *request) {
    int status;
    if (cold_active || gScProbeActive || fail_cold) return 0x100 + SC_INVALID;
    cold_active = 1;
    cold_calls++;
    status = sc_cold_entry(0, request);
    cold_active = 0;
    return status;
}

int sc_pak_probe_load(unsigned controller, const ScCatalog *catalog, ScSnapshot *snapshot) {
    RlU32 generation;
    (void)controller;
    return sc_save_decode(snapshot, catalog, &generation, saved, sizeof(saved));
}

int sc_pak_probe_store(unsigned controller, const ScCatalog *catalog,
                       const ScSnapshot *snapshot, int create) {
    (void)controller; (void)create;
    writes++;
    if (fail_final && writes == 2) return SC_SAVE_IO;
    return sc_save_encode(snapshot, catalog, writes, saved);
}

int rl_start_battle(const RlMon *party, int party_n, const RlMon *enemy, int enemy_n,
                    const RlBattleRules *rules, int return_state, RlLaunchResult *result) {
    ScSnapshot checkpoint;
    RlBattleMon *live;
    RlU32 xp;
    RlU32 generation;
    unsigned i, j, base, calls_before = cold_calls;
    int terminal = 0;
    (void)rules; (void)return_state;
    check(party_n == 1 && enemy_n == 1);
    check(!cold_active && combat.ready && cold_calls > 0);
    check(party->max_hp == 22 && party->dvs == 0x8889);
    check(sc_save_decode(&checkpoint, &sc_catalog, &generation, saved, sizeof(saved)) == SC_SAVE_OK);
    check(checkpoint.interrupted && checkpoint.state.phase == SC_ROUTE);
    if (fail_initialization) return -1;
    memset(sides, 0, sizeof(sides)); memset(teams, 0, sizeof(teams));
    for (i = 0; i < 2; i++) {
        const RlMon *mon = i ? enemy : party;
        sides[i].unk_724 = &teams[i];
        teams[i].unk_002 = 1;
        teams[i].unk_01C[0].unk_00.unk_00 = mon->species;
        check(sc_native_record(mon, &teams[i].unk_01C[0]) == SC_OK);
        live = rl_battle_mon(&sides[i]);
        rl_battle_species(live) = mon->species;
        rl_battle_hp(live) = mon->cur_hp;
        rl_battle_status(live) = mon->status;
        live->unk_24 = mon->dvs;
        rl_battle_level(live) = mon->level;
        rl_battle_max_hp(live) = mon->max_hp;
        rl_battle_base_attack(live) = rl_battle_attack(live) = mon->current_stats[1];
        rl_battle_base_defense(live) = rl_battle_defense(live) = mon->current_stats[2];
        rl_battle_base_speed(live) = mon->current_stats[3];
        rl_battle_speed(live) = mon->current_stats[3] / ((mon->status & 0x40) ? 4 : 1);
        rl_battle_base_special(live) = rl_battle_special(live) = mon->current_stats[4];
        for (j = 0; j < 6; j++) rl_battle_stage(live, j) = 7;
        for (j = 0; j < 4; j++) {
            rl_battle_move(live, j) = mon->moves[j];
            rl_battle_packed_pp(live, j) = mon->pp[j];
        }
    }
    if (gScBridgeProbe.enabled) {
        if (bridge_case == 2) rl_battle_packed_pp(rl_battle_mon(&sides[0]), 0) = 35;
        if (bridge_case == 4) teams[0].unk_002 = 2;
        if (bridge_case == 5) teams[0].unk_01C[0].unk_02 = 22;
        if (bridge_case == 6) rl_battle_speed(rl_battle_mon(&sides[0])) = 11;
        check(sc_runtime_probe_gate(&sides[0], &sides[1], &terminal) == 1 && !terminal);
        check(!gScProbe.ticks && !gScProbe.ko_count && !gScProbe.logic_steps &&
              !gScProbe.presentation_steps);
        if (bridge_case == 2) {
            check(gScBridgeProbe.error && !gScBridgeProbe.boundary &&
                  gScBridgeProbe.initial_live.pp[0] == 35 && resident.party[0].pp[0] == 7);
            return -1;
        }
        if (bridge_case >= 4) {
            check(gScBridgeProbe.error && !gScBridgeProbe.boundary &&
                  resident.party[0].cur_hp == 17 && resident.party[0].pp[0] == 7);
            return -1;
        }
        check(gScBridgeProbe.boundary == 1 && !gScBridgeProbe.error &&
              resident.party[0].cur_hp == 17 && resident.party[0].status == 0x40);
        for (i = 0; i < 10; i++) {
            check(sc_runtime_probe_gate(&sides[0], &sides[1], &terminal) == 1 &&
                  !gScProbe.ticks && !gScProbe.ko_count && resident.party[0].pp[0] == 7);
        }
        gScBridgeProbe.action = bridge_case == 3 ? 2 : 1;
        check(sc_runtime_probe_gate(&sides[0], &sides[1], &terminal) == 1);
        if (bridge_case == 3) {
            check(gScBridgeProbe.error && gScBridgeProbe.boundary == 1 &&
                  resident.party[0].cur_hp == 17 && resident.party[0].pp[0] == 7);
            return -1;
        }
        check(gScBridgeProbe.boundary == 2 && !gScBridgeProbe.error &&
              resident.party[0].cur_hp == 16 && resident.party[0].pp[0] == 6 &&
              resident.party[0].status == 0x48 && resident.party[0].exp == 235 &&
              !resident.defeated_mask && !resident.learn_mask);
        for (i = 0; i < 10; i++)
            check(sc_runtime_probe_gate(&sides[0], &sides[1], &terminal) == 1 &&
                  resident.party[0].cur_hp == 16 && !gScProbe.ticks && !gScProbe.ko_count);
        check(sc_save_decode(&checkpoint, &sc_catalog, &generation, saved, sizeof(saved)) == SC_SAVE_OK &&
              checkpoint.state.party[0].cur_hp == 17 && checkpoint.state.party[0].pp[0] == 7 &&
              checkpoint.state.party[0].status == 0x40 && cold_calls == calls_before);
        gScBridgeProbe.action = 1;
        check(sc_runtime_probe_gate(&sides[0], &sides[1], &terminal) == 1 &&
              gScBridgeProbe.error && resident.party[0].cur_hp == 16);
        return -1;
    }
    check(!sc_runtime_probe_gate(&sides[0], &sides[1], &terminal));
    live = rl_battle_mon(&sides[0]);
    check(live->unk_28 == party->max_hp && live->unk_2A == party->current_stats[1]);
    check(live->unk_38 == party->current_stats[1] && live->unk_24 == party->dvs);
    rl_battle_stage(live, 0) = 8;
    rl_battle_hp(live) -= 2;
    teams[0].unk_01C[0].unk_20[0]--;
    rl_battle_hp(rl_battle_mon(&sides[1])) = 0;
    check(sc_runtime_probe_after(&sides[0], &sides[1], native_result) == 0);
    check(sc_runtime_probe_paused() && resident.phase == SC_LEARNING);
    check(resident.party[0].level == 7 && gScProbe.ko_count == 1);
    check(resident.party[0].max_hp == 24 && live->unk_28 == 24);
    check(resident.party[0].cur_hp == resident.party[0].max_hp - 2);
    check(rl_battle_stage(live, 0) == 8);
    base = resident.party[0].current_stats[1];
    check(live->unk_38 == base && live->unk_2A == base * 150 / 100);
    xp = resident.party[0].exp;
    for (i = 0; i < 10; i++) {
        check(sc_runtime_probe_gate(&sides[0], &sides[1], &terminal));
        check(!terminal && resident.party[0].exp == xp && gScProbe.ko_count == 1);
    }
    check(writes == 1);
    gScProbe.resume = 1;
    check(sc_runtime_probe_gate(&sides[0], &sides[1], &terminal) == 2 && terminal == native_result);
    check(sc_runtime_probe_gate(&sides[0], &sides[1], &terminal) == 0);
    check(!sc_runtime_probe_paused() && resident.phase == SC_BATTLE);
    check(teams[0].unk_01C[0].unk_09[3] == resident.party[0].moves[3]);
    check(live->unk_1F[3] == resident.party[0].moves[3]);
    check(live->unk_5C[0] == 8 && live->unk_38 == base);
    check(cold_calls == calls_before && !cold_active);
    check(gScProbe.ko_count == 1 && resident.party[0].exp == xp);
    /* Duplicate KO frames still carry later HP/PP; they never re-award XP. */
    rl_battle_hp(live)--;
    check(!sc_runtime_probe_tick(&sides[0], &sides[1]));
    check(resident.party[0].exp == xp && resident.party[0].cur_hp == live->unk_0C);
    /* Transform must not be overwritten by a canonical original-record refresh. */
    live->unk_4E |= 8; live->unk_38 = 501; live->unk_2A = 700;
    live->unk_1F[0] = 94; live->unk_32[0] = 5;
    check(publish_live(&sides[0]) == SC_OK);
    check(live->unk_38 == 501 && live->unk_2A == 700 &&
           live->unk_1F[0] == 94 && live->unk_32[0] == 5);
    check(teams[0].unk_01C[0].unk_09[0] == resident.party[0].moves[0]);
    /* The lower-stage refresh uses Stadium's 66/100 rounding, not 2/3. */
    live->unk_4E &= ~8; live->unk_5C[0] = 6;
    check(publish_live(&sides[0]) == SC_OK && live->unk_2A == base * 66 / 100);
    live->unk_5C[0] = 0;
    check(publish_live(&sides[0]) == SC_INVALID);
    memset(result, 0, sizeof(*result)); result->outcome = launch_outcome;
    return launch_outcome;
}

int main(void) {
    ScSnapshot snapshot;
    RlMon checkpoint;
    RlU32 before;
    RlU32 generation;
    unsigned i;
    check(sizeof(ScProbeMailbox) == 53 * 4);
    gScProbe.command = 3; gScProbe.seed = 42; gScProbe.create = 1;
    if (!sc_runtime_probe_poll() || failure || gScProbe.status != SC_SAVE_OK) {
        printf("story runtime host failure: line %u, status %u\n", failure, gScProbe.status);
        return 1;
    }
    check(writes == 2 && resident.phase == SC_REWARD);
    check(gScProbe.post_pool_crc == gScProbe.completed_crc);
    check(sc_save_decode(&snapshot, &sc_catalog, &generation, saved, sizeof(saved)) == SC_SAVE_OK);
    check(!snapshot.interrupted && snapshot.state.party[0].level == 7);
    before = resident.party[0].exp;
    memset(&resident, 0, sizeof(resident)); memset(&bridge, 0, sizeof(bridge));
    gScProbe.command = 1;
    check(sc_runtime_probe_poll() && gScProbe.status == SC_SAVE_OK);
    check(resident.party[0].exp == before && resident.phase == SC_REWARD && writes == 2);
    check(staged(100, 1) == 25 && staged(100, 13) == 400 &&
           staged(1, 1) == 1 && staged(999, 13) == 999 && !staged(5, 0));
    writes = 0; fail_final = 1; native_result = 0;
    gScProbe.command = 3; gScProbe.create = 0;
    check(sc_runtime_probe_poll() && gScProbe.status == SC_SAVE_IO && !failure);
    check(resident.phase == SC_BATTLE && resident.party[0].level == 7);
    check(sc_save_decode(&snapshot, &sc_catalog, &generation, saved, sizeof(saved)) == SC_SAVE_OK);
    check(snapshot.interrupted && snapshot.state.phase == SC_ROUTE &&
           snapshot.state.party[0].level == 6);
    before = resident.party[0].exp;
    fail_cold = 1; gScProbe.command = 4;
    check(sc_runtime_probe_poll() && gScProbe.status == 0x100 + SC_INVALID &&
           resident.party[0].exp == before && resident.phase == SC_BATTLE && writes == 2);
    fail_cold = 0;
    gScProbe.command = 4;
    check(sc_runtime_probe_poll() && gScProbe.status == SC_SAVE_OK &&
           resident.phase == SC_REWARD && writes == 3);
    writes = 0; fail_final = 0; launch_outcome = RL_LAUNCH_DRAW;
    gScProbe.command = 3;
    check(sc_runtime_probe_poll() && !failure && gScProbe.status == 0x100 + SC_PENDING);
    check(writes == 1 && resident.phase == SC_BATTLE && pending_outcome == SC_DRAW);
    gScProbe.command = 4;
    check(sc_runtime_probe_poll() && gScProbe.status == 0x100 + SC_INVALID && writes == 1);
    gScProbe.command = 1;
    check(sc_runtime_probe_poll() && gScProbe.status == SC_SAVE_OK &&
           resident.phase == SC_ROUTE && resident.party[0].level == 6 && gScProbe.interrupted);
    writes = 0; fail_initialization = 1; gScProbe.command = 3;
    check(sc_runtime_probe_poll() && gScProbe.status == 0x100 + SC_INVALID &&
           writes == 1 && !gScProbeActive && !gScProbe.active);
    check(sc_save_decode(&snapshot, &sc_catalog, &generation, saved, sizeof(saved)) == SC_SAVE_OK &&
           snapshot.interrupted && snapshot.state.phase == SC_ROUTE &&
           snapshot.state.party[0].level == 6);
    gScProbe.command = 5;
    check(sc_runtime_probe_poll() && gScProbe.status == SC_SAVE_OK);
    check(resident.party[0].cur_hp == 17 && resident.party[0].pp[0] == 7 &&
          resident.party[0].status == 4 && resident.party[0].level == 6);
    check(sc_save_decode(&snapshot, &sc_catalog, &generation, saved, sizeof(saved)) == SC_SAVE_OK &&
          snapshot.interrupted && snapshot.state.phase == SC_ROUTE &&
          snapshot.state.party[0].cur_hp == 17 && snapshot.state.party[0].pp[0] == 7 &&
          snapshot.state.party[0].status == 4);
    checkpoint = snapshot.state.party[0];
    memset(&resident, 0, sizeof(resident));
    before = writes; gScProbe.command = 1;
    check(sc_runtime_probe_poll() && gScProbe.status == SC_SAVE_OK &&
          gScProbe.interrupted && writes == before &&
          resident.party[0].cur_hp == 17 && resident.party[0].max_hp == 22 &&
          resident.party[0].pp[0] == 7 && resident.party[0].max_pp[0] == 35 &&
          resident.party[0].status == 4 && resident.party[0].moves[0] == 33 &&
          resident.party[0].dvs == 0x8889 && resident.party[0].exp == 235 &&
          !resident.party[0].fainted && resident.party[0].level == 6);
    for (i = 0; i < 4; i++)
        check(resident.party[0].moves[i] == checkpoint.moves[i] &&
              resident.party[0].pp[i] == checkpoint.pp[i] &&
              resident.party[0].max_pp[i] == checkpoint.max_pp[i]);
    fail_initialization = 0;
    for (bridge_case = 1; bridge_case <= 6; bridge_case++) {
        writes = 0; gScProbe.command = 6;
        check(sc_runtime_probe_poll() && gScProbe.status == 0x100 + SC_INVALID &&
              writes == 1 && !gScProbeActive && !gScProbe.active);
    }
    puts("Story runtime host scheduling/stat/pause/codec scaffold: OK (not emulator proof)");
    return 0;
}
