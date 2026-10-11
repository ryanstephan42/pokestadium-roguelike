#include <string.h>
#include <stddef.h>
#include "src/roguelike/rl_main.c"
#include "src/roguelike/rl_rosters.c"
#include "src/fragments/rl/rl_play.c"

#undef assert
#define assert(condition) do { if (!(condition)) return __LINE__; } while (0)

s32 gCurrentGameState;
unk_D_80072B00 D_80072B00[165];
static unsigned sMoveNamesDrawn;
static unsigned sPpDrawn;
static unsigned sRosterNamesDrawn;
static unsigned sOrderDrawn;
static unsigned sDrawnMoves[4];
static unsigned sSpeciesSeen[152];
static char sSpeciesNames[152][16];
static char sDetailText[16][128];
static unsigned sDetailCount;
static unsigned sSuspendDrawn, sNodeStatusDrawn;

void osSyncPrintf(const char* format, ...) {}
int rl_battle_apply_pp_pouch(RunState* run) {
    run->consumed_relics |= 1U << RL_PP_POUCH;
    return 1;
}
int rl_prepare_mon(RlMon* mon, int full_heal) {
    mon->max_hp = 200;
    if (full_heal) mon->cur_hp = mon->max_hp;
    return 0;
}
int rl_start_battle(const RlMon* party, int count, const RlMon* enemy, int enemy_count,
                    const RlBattleRules* rules, int state, RlLaunchResult* result) {
    int i;
    bzero(result, sizeof(*result));
    result->player_count = count;
    for (i = 0; i < count; i++) {
        result->player_index[i] = (unsigned char)(count - 1 - i);
        result->player[i] = party[count - 1 - i];
        result->player[i].cur_hp = 10 + i;
    }
    return RL_BATTLE_LOSS;
}
s32 rl_ui_list(RlUiList* list, u16 pressed) { return rl_ui_list_step(list, pressed); }
s32 rl_ui_init(void) { return RL_UI_NONE; }
const char* rl_ui_move_name(unsigned move) {
    sDrawnMoves[(sMoveNamesDrawn + 4) % 4] = move;
    return move ? "THUNDERBOLT" : "--";
}
const char* rl_ui_species_name(unsigned species) {
    if (species < 1 || species > 151) return "Invalid Pokemon";
    sSpeciesSeen[species]++;
    return sSpeciesNames[species];
}
void rl_ui_text(s16 x, s16 y, const char* text, Color_RGBA8 color) {
    if (!strcmp(text, "START: suspend to Controller Pak")) {
        if (y != 416) __builtin_trap();
        sSuspendDrawn++;
    }
    if (!strcmp(text, "THUNDERBOLT")) sMoveNamesDrawn++;
    if (!strncmp(text, "PP ", 3)) sPpDrawn++;
    if (!strncmp(text, "SPECIES", 7)) sRosterNamesDrawn++;
    if (x >= 410 && y >= 80 && y <= 300 && sDetailCount < 16)
        strcpy(sDetailText[sDetailCount++], text);
    if (text[0] >= '1' && text[0] <= '3' && text[1] == ' ') sOrderDrawn++;
}
void rl_ui_text_wrap(s16 x, s16 y, s16 width, unsigned lines,
                     const char* text, Color_RGBA8 color) {
    if (!strcmp(text, "Node save status")) {
        if (x != 42 || y != 438 || width != 556 || lines != 1) __builtin_trap();
        sNodeStatusDrawn++;
    }
    if (x == 410) {
        if (width != 200 || (lines != 1 && lines != 2)) __builtin_trap();
        rl_ui_text(x, y, text, color);
    }
}
void rl_ui_window(s16 x, s16 y, s16 w, s16 h) {}
void rl_ui_list_draw(const RlUiList* list, s16 x, s16 y, s16 w) {}
s32 rl_ui_mon_icon(s32 species, s16 x, s16 y) { return RL_UI_NONE; }
void rl_ui_hp_bar(s16 x, s16 y, s16 w, u16 hp, u16 max_hp) {}
void rl_scene_nodes_draw(void) {}
RlSaveStatus rl_save_load(unsigned port, RlMeta* meta, RunState* run, RlU8* has_run) {
    return RL_SAVE_NOT_FOUND;
}
RlSaveStatus rl_save_store(unsigned port, const RlMeta* meta, const RunState* run, int create) {
    return RL_SAVE_NO_DEVICE;
}
const char* rl_save_status_text(RlSaveStatus status) { return "Storage unavailable"; }

extern int printf(const char* format, ...);

int main(int argc, char** argv) {
    RunState before;
    RlMon actual[6];
    unsigned char ai;
    unsigned char duplicate_slots[3] = {0, 0, 1};
    unsigned i;
    unsigned lane;
    int count;
    unsigned seed, recruits;
    char label[48];
    RlBattleRules rules;

    if (argc == 2 && !strcmp(argv[1], "--layout")) {
        printf("{\"mon_size\":%u,\"reward_size\":%u,\"reward_mon\":%u,\"rules_size\":%u,\"relics\":%u,"
               "\"pending_size\":%u,\"rules\":%u,\"player_slots\":%u,"
               "\"trainer\":%u,\"trainer_count\":%u}\n",
               (unsigned)sizeof(RlMon), (unsigned)sizeof(RlReward),
               (unsigned)offsetof(RlReward, mon),
               (unsigned)sizeof(RlBattleRules),
               (unsigned)offsetof(RlBattleRules, relics),
               (unsigned)sizeof(RlPendingBattle),
               (unsigned)offsetof(RlPendingBattle, rules),
               (unsigned)offsetof(RlPendingBattle, player_slots),
               (unsigned)offsetof(RlPendingBattle, trainer),
               (unsigned)offsetof(RlPendingBattle, trainer_count));
        return 0;
    }

    for (i = 0; i < 165; i++) D_80072B00[i].unk_05 = 15;
    for (i = 1; i <= 151; i++) {
        /* Distinct text fixtures catch accidental lookup of a different mon. */
        sprintf(sSpeciesNames[i], "SPECIES%03u", i);
    }
    rl_meta_init(&sPlayMeta);
    rl_run_new(&gRlRun, 3, 0);
    gRlRun.phase = RL_MAP;
    before = gRlRun;
    rl_play_make_map();
    for (lane = 0; lane < gRlRun.map.lanes[0]; lane++) {
        sPlayList.cursor = lane;
        rl_play_preview_lane();
        assert(!memcmp(&gRlRun, &before, sizeof(before)));
        if (gRlRun.map.nodes[0][lane].type == RL_BATTLE) {
            sPlayPreviewRun.row = 0;
            sPlayPreviewRun.lane = lane;
            count = rl_load_encounter(&sPlayPreviewRun, actual, 6, &ai);
            assert(count == sPlayPreviewCount && count == 6);
            assert(!memcmp(actual, sPlayPreviewTeam, count * sizeof(RlMon)));
        }
    }
    sPlayList.cursor = 2;
    rl_play_preview_lane();
    assert(sPlayPreviewCount == 6);
    rl_play_draw();
    assert(sRosterNamesDrawn == 6);

    gRlRun.party_count = 5;
    for (i = 0; i < 5; i++) {
        gRlRun.party[i] = actual[0];
        gRlRun.party[i].species = i < 2 ? 25 : 1 + i;
        gRlRun.party[i].cur_hp = 100 + i;
        gRlRun.party[i].moves[0] = 85;
        gRlRun.party[i].pp[0] = 10 + i;
    }
    gRlRun.party[2].cur_hp = 0;
    gRlRun.party[2].fainted = 1;
    before = gRlRun;
    sPlayChosenLane = 2;
    rl_play_make_party();
    assert(sPlayActiveCount == 3);
    assert(sPlayActiveSlots[0] == 0 && sPlayActiveSlots[1] == 1 && sPlayActiveSlots[2] == 3);
    sPlayList.cursor = 3;
    rl_play_update_party(RL_UI_LEFT);
    rl_play_update_party(RL_UI_LEFT);
    assert(sPlayActiveSlots[0] == 3 && sPlayActiveSlots[1] == 0 && sPlayActiveSlots[2] == 1);
    sPlayList.cursor = 0;
    rl_play_update_party(RL_UI_A);
    rl_play_update_party(RL_UI_A);
    assert(sPlayActiveSlots[0] == 3 && sPlayActiveSlots[1] == 1 && sPlayActiveSlots[2] == 0);
    assert(!memcmp(&gRlRun, &before, sizeof(before)));
    rl_play_draw();
    assert(sOrderDrawn == 3 && sMoveNamesDrawn == 4 && sPpDrawn == 4);
    bzero(&rules, sizeof(rules));
    assert(!rl_main_queue_battle(gRlRun.party, 3, actual, 3, &rules,
                                  duplicate_slots, actual, 6));
    sPlayList.cursor = gRlRun.party_count;
    rl_play_update_party(RL_UI_A);
    assert(gRlPendingBattle.pending && gRlPendingBattle.player_slots[0] == 3);
    assert(gRlPendingBattle.player_slots[1] == 1 && gRlPendingBattle.player_slots[2] == 0);
    assert(gRlPendingBattle.player[0].cur_hp == before.party[3].cur_hp);
    assert(gRlPendingBattle.player[1].species == gRlPendingBattle.player[2].species);
    assert(gRlPendingBattle.trainer_count == 6);
    assert(!memcmp(gRlPendingBattle.trainer, sPlayPreviewTeam, 6 * sizeof(RlMon)));
    rl_main_run_pending_battle();
    assert(!gRlPendingBattle.error);
    assert(gRlRun.party[0].cur_hp == 10 && gRlRun.party[1].cur_hp == 11 &&
           gRlRun.party[3].cur_hp == 12);
    assert(!memcmp(&gRlRun.party[2], &before.party[2], sizeof(RlMon)));
    assert(!memcmp(&gRlRun.party[4], &before.party[4], sizeof(RlMon)));
    sPlayDraft[0] = before.party[0];
    sPlayDraft[0].moves[1] = 85;
    sPlayDraft[0].moves[2] = 85;
    sPlayDraft[0].moves[3] = 85;
    sPlayList.cursor = 0;
    sMoveNamesDrawn = sPpDrawn = 0;
    gRlPlayScene = RL_PLAY_DRAFT;
    rl_play_draw();
    assert(sMoveNamesDrawn == 4 && sPpDrawn == 4);
    for (i = 0; i < 4; i++) assert(sDrawnMoves[i] == sPlayDraft[0].moves[i]);
    gRlRun.relics = 1U << RL_EARTH;
    rl_play_make_rewards();
    assert(sPlayRewardCount == 4 && sPlayList.count == 4);
    sPlayList.cursor = 3;
    rl_ui_list(&sPlayList, RL_UI_DOWN);
    assert(sPlayList.cursor == 3);
    recruits = 0;
    gRlRun.party_count = 2;
    for (seed = 1; seed <= 100; seed++) {
        gRlRun.rng = seed;
        rl_play_make_rewards();
        for (i = 0; i < (unsigned)sPlayRewardCount; i++) {
            if (sPlayRewards[i].kind != RL_REWARD_RECRUIT) continue;
            recruits++;
            sprintf(label, "Recruit %u %s", i + 1,
                    sSpeciesNames[sPlayRewards[i].mon.species]);
            assert(!strcmp(sPlayItems[i].text, label));
            sPlayList.cursor = i;
            sDetailCount = sMoveNamesDrawn = sPpDrawn = 0;
            rl_play_draw();
            assert(sDetailCount == 11 && sMoveNamesDrawn == 4 && sPpDrawn == 4);
            assert(!strcmp(sDetailText[0], sSpeciesNames[sPlayRewards[i].mon.species]));
            sprintf(label, "Level %03u", sPlayRewards[i].mon.level);
            assert(!strcmp(sDetailText[1], label));
            sprintf(label, "HP %03u/%03u", sPlayRewards[i].mon.cur_hp,
                    sPlayRewards[i].mon.max_hp);
            assert(!strcmp(sDetailText[2], label));
            for (lane = 0; lane < 4; lane++) {
                assert(sDrawnMoves[lane] == sPlayRewards[i].mon.moves[lane]);
                sprintf(label, "PP %02u/%02u", sPlayRewards[i].mon.pp[lane],
                        sPlayRewards[i].mon.max_pp[lane]);
                assert(!strcmp(sDetailText[4 + lane * 2], label));
            }
        }
    }
    assert(recruits > 0);
    gRlPlayScene = RL_PLAY_NODE;
    sPlayMessage = "Node save status";
    rl_play_draw();
    assert(sSuspendDrawn == 1 && sNodeStatusDrawn == 1);
    return 0;
}
