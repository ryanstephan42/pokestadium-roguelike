#include <stdio.h>
#include <string.h>
#include "src/story/sc_runtime_probe.c"
#include "src/roguelike/rl_battle.h"
#include "src/22630.h"
#include "src/232C0.h"
#include "src/2C1C0.h"
#include "src/373A0.h"
#include "src/memory.h"
static int native_battle(void);
#undef FRAGMENT_LOAD
#undef FRAGMENT_LOAD_AND_CALL
#define FRAGMENT_LOAD(fragment) 0
#define FRAGMENT_LOAD_AND_CALL(fragment, arg0, arg1) native_battle()
#define ROGUELIKE
#include "src/roguelike/rl_battle.c"

static RlU8 saved[SC_SAVE_SIZE];
static unsigned writes, refusal_logs;
static int corrupt_terminal, fail_store, permute_party;
static RlBattleSide sides[2];
static unk_D_800AE540_0004 teams[2];
PokemonStats D_80070F84[152];
unk_D_80072B00 D_80072B00[165];
unk_D_800AE540 D_800AE540;
s32 gCurrentGameState;

#undef assert
#define assert(condition) do { if (!(condition)) { \
    printf("Six-party runtime assertion failed at line %u scenario %u phase %u error %u status %u\n", \
           __LINE__, (unsigned)corrupt_terminal, resident.phase, gScSixControl.error, gScProbe.status); return -1; \
} } while (0)

void osSyncPrintf(const char *format, ...) {
    if (!strcmp(format, "story six probe: RL relic/custom battle test payload unavailable\n"))
        refusal_logs++;
}

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
    int status = sc_cold_entry(0, request);
    if (status == SC_OK && request->command == SC_COLD_PREPARE && corrupt_terminal == 16) {
        request->state->party[0].level = 14;
        request->state->party[0].exp = sc_xp(15, request->state->party[0].growth) - 1;
        sc_recalculate(&request->state->party[0], 0);
        request->state->party[1].level = 14;
        request->state->party[1].exp = sc_xp(15, request->state->party[1].growth) - 1;
        sc_recalculate(&request->state->party[1], 0);
    }
    if (status == SC_OK && request->command == SC_COLD_PREPARE && permute_party) {
        unsigned i;
        for (i = 0; i < 3; i++) {
            RlMon mon = request->state->party[i];
            request->state->party[i] = request->state->party[5 - i];
            request->state->party[5 - i] = mon;
        }
        status = sc_combat_prepare(request->combat, request->state, &sc_catalog);
    }
    return status;
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

u32 func_800224B8(s32 species, u16 level) { (void)species; return level * level * level; }
void func_80022338(unk_func_80026268_arg0 *mon) { (void)mon; }
u8 func_80022A38(s32 species) { (void)species; return 0x83; }
void func_800228B0(unk_func_80026268_arg0 *mon) { (void)mon; }
char *func_80021CA4(char *name, s32 species) { (void)species; strcpy(name, "NATIVE"); return name; }
unk_func_80026268_arg0 *func_8002B924(unk_func_80026268_arg0 *mon, u8 species, s16 level) {
    mon->unk_00.unk_00 = species; mon->unk_24 = level;
    func_80021CA4(mon->unk_30, species);
    return mon;
}
char *func_8002311C(s32 value) { (void)value; return ""; }
void func_8002B5EC(s32 mode, s32 stadium, s32 gym) {
    (void)mode; (void)stadium; (void)gym;
    memset(sides, 0, sizeof(sides)); memset(teams, 0, sizeof(teams));
    memset(&D_800AE540, 0, sizeof(D_800AE540));
}
unk_D_800AE540_0004 *func_8002B700(s32 cpu, s16 value, char *name, char *trainer) {
    (void)cpu; (void)value; (void)trainer;
    return &teams[!strcmp(name, "COM")];
}
void func_8002B840(s32 side, unk_D_800AE540_0004 *team) { sides[side].unk_724 = team; }
void func_8002B888(unk_D_800AE540_0004 *team, unk_func_80026268_arg0 *mon) {
    team->unk_01C[team->unk_002++] = *mon;
}
void rl_native_relics_reset(unsigned int mask) { (void)mask; }
u32 main_pool_push_state(u32 tag) { return tag; }
u32 main_pool_pop_state(u32 tag) {
    if (gScSixControl.role == SC_SIX_RAW) memset(teams, 0xDD, sizeof(teams));
    return tag;
}
s32 func_800367A0(u32 a, s32 b, s32 c) { (void)a; (void)b; (void)c; return 0; }

static int native_battle(void) {
    unsigned i, slot, active;
    ScState before;
    assert(teams[0].unk_002 == 6 && teams[1].unk_002 == 2 && gRlBattleActive);
    for (i = 0; i < 6; i++) assert(mapping[i] == i && gRlLastBattleResult.player_index[i] == i);
    for (i = 0; i < 2; i++) {
        active = corrupt_terminal == 16 ? i : (i ? 3 : 5);
        slot = mapping[active];
        live_mon(0, active, &resident.party[slot]);
        live_mon(1, i, &opponents[i]);
        before = resident;
        assert(sc_runtime_probe_tick(&sides[0], &sides[1]) == 0);
        assert(bridge.participants[i] == (1U << slot));
        assert(!memcmp(&before, &resident, sizeof(resident)));
        rl_battle_hp(rl_battle_mon(&sides[1])) = 0;
        teams[1].unk_01C[i].unk_02 = 0;
        assert(sc_runtime_probe_tick(&sides[0], &sides[1]) ==
               (corrupt_terminal == 16 ? 1 : 0));
        assert(gScSixControl.events == i + 1 && !gScSixControl.error);
        assert(resident.party[slot].exp > before.party[slot].exp);
        assert(gScSixProgressStorage.event.before[slot].species == resident.party[slot].species);
        if (corrupt_terminal == 16) {
            ScState hold = resident;
            unsigned ticks;
            assert(gScSixControl.pause_id == i + 1 && gScSixControl.resumes == i);
            for (ticks = 0; ticks < 60; ticks++)
                assert(sc_runtime_probe_tick(&sides[0], &sides[1]) == 1);
            assert(!memcmp(&hold, &resident, sizeof(hold)));
            gScProbe.resume = 1;
            assert(sc_runtime_probe_tick(&sides[0], &sides[1]) == 0);
            assert(gScSixControl.resumes == i + 1 && resident.phase == SC_BATTLE &&
                   resident.party[slot].moves[3] == (i ? 55 : 43));
        }
        before = resident;
        assert(sc_runtime_probe_tick(&sides[0], &sides[1]) == 0);
        assert(gScSixControl.events == i + 1 && !memcmp(&before, &resident, sizeof(before)));
    }
    switch (corrupt_terminal) {
        case 1: teams[0].unk_01C[4].unk_09[0] = 1; break;
        case 2: teams[0].unk_01C[4].unk_02--; break;
        case 3: teams[0].unk_01C[4].unk_20[0]--; break;
        case 4: teams[0].unk_01C[4].unk_28++; break;
        case 5: teams[0].unk_01C[4].unk_10++; break;
        case 6: teams[0].unk_01C[4].unk_1E++; break;
        case 7: teams[0].unk_01C[4].unk_05 = 4; break;
        case 8: teams[0].unk_01C[4].unk_24++; break;
        case 9: teams[0].unk_01C[4].unk_00.unk_00 = 4; break;
        case 10: teams[0].unk_01C[4].unk_30[0] = 'X'; break;
        case 11: mapping[4] = mapping[3]; break;
        case 12: teams[1].unk_01C[0].unk_02 = 1; break;
        case 13: return -1;
    }
    D_800AE540.unk_1194[0].unk_1C = 1;
    if (corrupt_terminal == 14) D_800AE540.unk_1194[1].unk_1C = 1;
    if (corrupt_terminal == 15) D_800AE540.unk_1194[0].unk_1C = 0;
    return 0;
}

static int nested_status;
static ScState nested_output, nested_before;
static void reenter(const char *operation, unsigned index, const ScState *state,
                     int status, void *context) {
    (void)operation; (void)index; (void)state; (void)status; (void)context;
    nested_status = sc_six_checkpoint(&nested_output, &sc_catalog, 0, 0);
}

int main(void) {
    ScSnapshot snapshot;
    ScState before;
    RlU32 generation;
    unsigned scenario;
    unsigned i, j;
    ScState fixture;
    RlMon returned, launched, invalid;
    unk_func_80026268_arg0 native;
    for (scenario = 0; scenario < 4; scenario++) {
        unsigned count = gRlBattleCount;
        gRlBattleTestRequest = scenario;
        gCurrentGameState = STATE_MENU_SELECT;
        rl_battle_test_entry();
        assert(!gRlBattleTestRequest && gCurrentGameState == STATE_TITLE_SCREEN &&
               gRlBattleCount == count && !writes && refusal_logs == scenario + 1);
    }
    memset(&nested_output, 0x55, sizeof(nested_output));
    nested_before = nested_output;
    assert(sc_six_checkpoint(&fixture, &sc_catalog, reenter, 0) == SC_OK);
    assert(nested_status == SC_INVALID && !memcmp(&nested_output, &nested_before, sizeof(nested_output)));
    assert(sc_six_checkpoint(&fixture, 0, 0, 0) == SC_INVALID);
    assert(sc_six_checkpoint(&fixture, &sc_catalog, 0, 0) == SC_OK);
    for (i = 0; i < 6; i++) {
        const RlMon *mon = &fixture.party[i];
        PokemonStats *stats = &D_80070F84[mon->species];
        stats->baseHP = mon->base_stats[0]; stats->baseAttack = mon->base_stats[1];
        stats->baseDefense = mon->base_stats[2]; stats->baseSpeed = mon->base_stats[3];
        stats->baseSpecial = mon->base_stats[4];
        for (j = 0; j < 4; j++)
            if (mon->moves[j]) D_80072B00[mon->moves[j] - 1].unk_05 = mon->max_pp[j];
    }
    D_80072B00[110].unk_05 = 40; D_80072B00[102].unk_05 = 40; D_80072B00[116].unk_05 = 10;
    D_80072B00[42].unk_05 = 30; D_80072B00[54].unk_05 = 25;
    launched = fixture.party[0];
    gScProbeActive = 1;
    assert(launched.base_stats[0] == 39 && launched.base_stats[1] == 52 &&
           launched.base_stats[2] == 43 && launched.base_stats[3] == 65 &&
           launched.base_stats[4] == 50 && !launched.nickname[0]);
    assert(rl_to_native(&launched, &native, 1) == 0);
    returned = launched;
    rl_from_native(&native, &returned);
    assert(returned.base_stats[1] == 50 && returned.base_stats[4] == 52 &&
           !strcmp(returned.nickname, "NATIVE"));
    invalid = returned; invalid.base_stats[1]++;
    before = fixture;
    assert(sc_native_return_identity(&invalid, &launched) == SC_INVALID);
    assert(invalid.base_stats[1] == 51 && !memcmp(&before, &fixture, sizeof(before)));
    assert(sc_native_return_identity(&returned, &launched) == SC_OK &&
           !memcmp(&returned, &launched, sizeof(returned)));
    strcpy(launched.nickname, "CUSTOM");
    assert(rl_to_native(&launched, &native, 1) == 0);
    returned = launched;
    rl_from_native(&native, &returned);
    invalid = returned; invalid.nickname[0] = 'X';
    assert(sc_native_return_identity(&invalid, &launched) == SC_INVALID);
    assert(sc_native_return_identity(&returned, &launched) == SC_OK &&
           !memcmp(&returned, &launched, sizeof(returned)));
    for (scenario = 0; scenario < 19; scenario++) {
        writes = 0; pending_outcome = 255;
        corrupt_terminal = scenario > 2 ? scenario - 2 : 0;
        fail_store = scenario == 1;
        permute_party = scenario == 2;
        memset((void *)&gScProbe, 0, sizeof(gScProbe));
        gScProbe.command = 3; gScProbe.seed = 42;
        assert(sc_runtime_probe_poll() == 1);
        if (!scenario || scenario == 2 || scenario == 18) {
            assert(!gScProbe.status && resident.phase == SC_REWARD && writes == 2);
            assert(gScSixControl.role == SC_SIX_TERMINAL && gScSixControl.events == 2 &&
                   !gScSixControl.error && gScSixBridgeStorage.terminal.valid);
            assert(sc_save_decode(&snapshot, &sc_catalog, &generation, saved, sizeof(saved)) == SC_SAVE_OK);
            assert(!snapshot.interrupted && !memcmp(&snapshot.state, &resident, sizeof(resident)));
        } else {
            assert(gScProbe.status && writes == 1);
            assert(sc_save_decode(&snapshot, &sc_catalog, &generation, saved, sizeof(saved)) == SC_SAVE_OK);
            assert(snapshot.interrupted && snapshot.state.phase == SC_ROUTE);
            if (scenario == 1) {
                ScSixTerminal terminal = gScSixBridgeStorage.terminal;
                ScState precommit = gScSixCombatStorage.precommit;
                assert(gScSixControl.role == SC_SIX_TERMINAL && terminal.valid);
                fail_store = 0;
                gScProbe.command = 4;
                assert(sc_runtime_probe_poll() && gScProbe.status && writes == 1);
                gScProbe.command = 1;
                assert(sc_runtime_probe_poll() && gScProbe.status && writes == 1);
                assert(!memcmp(&precommit, &gScSixCombatStorage.precommit, sizeof(precommit)) &&
                       !memcmp(&terminal, (const void *)&gScSixBridgeStorage.terminal, sizeof(terminal)));
            } else assert(gScSixControl.role == SC_SIX_INVALID);
        }
    }
    {
        ScSixBridgeStorage raw;
        ScSixCombatStorage proof;
        memset((void *)&gScSixBridgeStorage, 0x55, sizeof(gScSixBridgeStorage));
        memset(&gScSixCombatStorage, 0x66, sizeof(gScSixCombatStorage));
        raw = gScSixBridgeStorage; proof = gScSixCombatStorage;
        gScSixControl.role = SC_SIX_RAW; failed = 0; gScProbeActive = 1;
        report();
        assert(!gScProbe.combat_crc && sc_runtime_probe_paused());
        assert(publish_live((RlBattleSide *)1) == SC_INVALID);
        assert(six_before_award((RlBattleSide *)1, 6, 0, 0) == SC_INVALID);
        bridge_views((RlBattleSide *)1, &gScBridgeProbe.initial_party, &gScBridgeProbe.initial_live);
        assert(!bridge_view_equal(&gScBridgeProbe.initial_party, &gScBridgeProbe.initial_live));
        assert(sc_runtime_probe_tick(&sides[0], &sides[1]) == 1);
        assert(sc_runtime_probe_terminal(&teams[0], &teams[1], RL_BATTLE_WIN) == SC_INVALID);
        assert(!memcmp(&raw, (const void *)&gScSixBridgeStorage, sizeof(raw)) &&
               !memcmp(&proof, &gScSixCombatStorage, sizeof(proof)));
    }
    puts("Story six-party actual launcher/conversion/identity/dynamic guards/cold ownership: OK (host native-engine stub, not native wins)");
    return 0;
}
