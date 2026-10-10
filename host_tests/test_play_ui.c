#include <string.h>
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

void osSyncPrintf(const char* format, ...) {}
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
const char* rl_ui_species_name(unsigned species) { return "CHARMANDER"; }
void rl_ui_text(s16 x, s16 y, const char* text, Color_RGBA8 color) {
    if (!strcmp(text, "THUNDERBOLT")) sMoveNamesDrawn++;
    if (!strncmp(text, "PP ", 3)) sPpDrawn++;
    if (!strcmp(text, "CHARMANDER")) sRosterNamesDrawn++;
    if (text[0] >= '1' && text[0] <= '3' && text[1] == ' ') sOrderDrawn++;
}
void rl_ui_text_wrap(s16 x, s16 y, s16 width, unsigned lines,
                     const char* text, Color_RGBA8 color) {
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

int main(void) {
    RunState before;
    RlMon actual[6];
    unsigned char ai;
    unsigned char duplicate_slots[3] = {0, 0, 1};
    unsigned i;
    unsigned lane;
    int count;
    RlBattleRules rules;

    for (i = 0; i < 165; i++) D_80072B00[i].unk_05 = 15;
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
    return 0;
}
