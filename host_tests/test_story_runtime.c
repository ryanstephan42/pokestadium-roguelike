#include <string.h>
#include <stdio.h>
#include "src/story/sc_runtime_probe.c"

static unsigned failure, writes;
static int fail_final;
static int native_result = 1;
static unsigned char saved[SC_SAVE_SIZE];
static RlBattleSide sides[2];
static unk_D_800AE540_0004 teams[2];

#define check(condition) do { if (!(condition)) { failure = __LINE__; return -1; } } while (0)

void osSyncPrintf(const char *format, ...) { (void)format; }
int sc_runtime_catalog_load(void) { return SC_OK; }

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
    unsigned i, j, base;
    int terminal = 0;
    (void)rules; (void)return_state;
    check(party_n == 1 && enemy_n == 1);
    check(party->max_hp == 22 && party->dvs == 0x8889);
    check(sc_save_decode(&checkpoint, &sc_catalog, &generation, saved, sizeof(saved)) == SC_SAVE_OK);
    check(checkpoint.interrupted && checkpoint.state.phase == SC_ROUTE);
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
        for (j = 0; j < 4; j++) rl_battle_stage(live, j) = 7;
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
    memset(result, 0, sizeof(*result)); result->outcome = RL_BATTLE_WIN;
    return RL_BATTLE_WIN;
}

int main(void) {
    ScSnapshot snapshot;
    RlU32 before;
    RlU32 generation;
    check(sizeof(ScProbeMailbox) == 32 * 4);
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
    gScProbe.command = 4;
    check(sc_runtime_probe_poll() && gScProbe.status == SC_SAVE_OK &&
           resident.phase == SC_REWARD && writes == 3);
    puts("Story runtime host scheduling/stat/pause/codec scaffold: OK (not emulator proof)");
    return 0;
}
