#include <stdio.h>
#include <string.h>
#include "src/story/sc_runtime_probe.c"

static RlU8 saved[SC_SAVE_SIZE];
static unsigned writes;
static int corrupt_terminal, fail_store;
static RlBattleSide sides[2];
static unk_D_800AE540_0004 teams[2];

#undef assert
#define assert(condition) do { if (!(condition)) { \
    printf("Six-party runtime assertion failed at line %u\n", __LINE__); return -1; \
} } while (0)

void osSyncPrintf(const char *format, ...) { (void)format; }

int sc_pak_probe_load(unsigned controller, const ScCatalog *catalog, ScSnapshot *snapshot) {
    RlU32 generation;
    (void)controller;
    return sc_save_decode(snapshot, catalog, &generation, saved, sizeof(saved));
}

int sc_pak_probe_store(unsigned controller, const ScCatalog *catalog,
                       const ScSnapshot *snapshot, int create) {
    (void)controller; (void)create;
    if (fail_store && writes) return SC_SAVE_IO;
    writes++;
    return sc_save_encode(snapshot, catalog, writes, saved);
}

int sc_cold_call(ScColdRequest *request) {
    return sc_cold_entry(0, request);
}

static void live_mon(unsigned side, unsigned slot, const RlMon *mon) {
    RlBattleMon *live = rl_battle_mon(&sides[side]);
    unsigned i;
    memset(live, 0, sizeof(*live));
    sides[side].unk_654.unk_08 = slot;
    rl_battle_species(live) = mon->species;
    live->unk_24 = mon->dvs;
    rl_battle_level(live) = mon->level;
    rl_battle_hp(live) = mon->cur_hp;
    rl_battle_max_hp(live) = mon->max_hp;
    rl_battle_status(live) = mon->status;
    for (i = 0; i < 6; i++) rl_battle_stage(live, i) = 7;
    for (i = 0; i < 4; i++) {
        rl_battle_move(live, i) = mon->moves[i];
        rl_battle_packed_pp(live, i) = mon->pp[i];
    }
}

int rl_start_battle(const RlMon *party, int party_n, const RlMon *enemy, int enemy_n,
                    const RlBattleRules *rules, int return_state, RlLaunchResult *result) {
    unsigned i, slot, active;
    ScState before;
    (void)return_state;
    assert(party_n == 6 && enemy_n == 2 && rules->player_count == 0 && rules->use_state);
    memset(sides, 0, sizeof(sides)); memset(teams, 0, sizeof(teams));
    result->player_count = 6; result->enemy_count = 2;
    for (i = 0; i < 6; i++) result->player_index[i] = (RlU8)(5 - i);
    assert(sc_runtime_probe_mapping(result->player_index, 6) == SC_OK);
    for (i = 0; i < 2; i++) {
        sides[i].unk_724 = &teams[i];
        teams[i].unk_002 = i ? 2 : 6;
    }
    for (i = 0; i < 6; i++) {
        teams[0].unk_01C[i].unk_00.unk_00 = party[mapping[i]].species;
        assert(sc_native_record(&party[mapping[i]], &teams[0].unk_01C[i]) == SC_OK);
    }
    for (i = 0; i < 2; i++) {
        teams[1].unk_01C[i].unk_00.unk_00 = enemy[i].species;
        assert(sc_native_record(&enemy[i], &teams[1].unk_01C[i]) == SC_OK);
    }
    for (i = 0; i < 2; i++) {
        active = i ? 2 : 0;
        slot = mapping[active];
        live_mon(0, active, &resident.party[slot]);
        live_mon(1, i, &enemy[i]);
        before = resident;
        assert(sc_runtime_probe_tick(&sides[0], &sides[1]) == 0);
        assert(bridge.participants[i] == (1U << slot));
        assert(!memcmp(&before, &resident, sizeof(resident)));
        rl_battle_hp(rl_battle_mon(&sides[1])) = 0;
        assert(sc_runtime_probe_tick(&sides[0], &sides[1]) == 0);
        assert(gScProgressProbe.ko_captures == i + 1 && !gScProgressProbe.error);
        assert(resident.party[slot].exp > before.party[slot].exp);
        assert(gScProgressProbe.before_ko.species == resident.party[slot].species);
        before = resident;
        assert(sc_runtime_probe_tick(&sides[0], &sides[1]) == 0);
        assert(gScProgressProbe.ko_captures == i + 1 && !memcmp(&before, &resident, sizeof(before)));
    }
    for (i = 0; i < 6; i++) result->player[i] = resident.party[mapping[i]];
    if (corrupt_terminal) result->player[4].moves[0] = 1;
    result->outcome = RL_BATTLE_WIN;
    return RL_BATTLE_WIN;
}

int main(void) {
    ScSnapshot snapshot;
    ScState before;
    RlU32 generation;
    unsigned scenario;
    for (scenario = 0; scenario < 3; scenario++) {
        writes = 0; pending_outcome = 255;
        corrupt_terminal = scenario == 1;
        fail_store = scenario == 2;
        memset((void *)&gScProbe, 0, sizeof(gScProbe));
        gScProbe.command = 3; gScProbe.seed = 42;
        assert(sc_runtime_probe_poll() == 1);
        if (!scenario) {
            assert(!gScProbe.status && resident.phase == SC_REWARD && writes == 2);
            assert(gScProgressProbe.terminal && gScProgressProbe.ko_captures == 2 &&
                   !gScProgressProbe.error && !gScProgressProbe.injected);
            assert(sc_save_decode(&snapshot, &sc_catalog, &generation, saved, sizeof(saved)) == SC_SAVE_OK);
            assert(!snapshot.interrupted && !memcmp(&snapshot.state, &resident, sizeof(resident)));
            before = resident;
            gScProbe.command = 1;
            assert(sc_runtime_probe_poll() && !gScProbe.status &&
                   !memcmp(&resident, &before, sizeof(resident)) && writes == 2);
        } else {
            assert(gScProbe.status && writes == 1);
            assert(sc_save_decode(&snapshot, &sc_catalog, &generation, saved, sizeof(saved)) == SC_SAVE_OK);
            assert(snapshot.interrupted && snapshot.state.phase == SC_ROUTE);
        }
    }
    puts("Story six-party cold/launcher mapping/per-enemy KO/all-terminal/save refusals: OK (host mock, not native wins)");
    return 0;
}
