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
static int sStorageEnabled, sUncertainWrite, sWriteCount;
static RlSaveStatus sReadFailure;
static RlMeta sDiskMeta;
static RunState sDiskRun;
static RlU8 sDiskHasRun;

void osSyncPrintf(const char* format, ...) {}
s32 MathUtil_Random16(void) { return 123; }
int rl_battle_apply_pp_pouch(RunState* run) {
    run->consumed_relics |= 1U << RL_PP_POUCH;
    return 1;
}
int rl_prepare_mon(RlMon* mon, int full_heal) {
    unsigned i;
    for (i = 0; i < 5; i++) mon->base_stats[i] = 40 + i * 10;
    mon->max_hp = rl_mon_stat_value(mon, 0, mon->level);
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
    if (sStorageEnabled) {
        if (sReadFailure != RL_SAVE_OK) return sReadFailure;
        *meta = sDiskMeta;
        if (sDiskHasRun) *run = sDiskRun;
        *has_run = sDiskHasRun;
        return RL_SAVE_OK;
    }
    return RL_SAVE_NOT_FOUND;
}
RlSaveStatus rl_save_store(unsigned port, const RlMeta* meta, const RunState* run, int create) {
    if (sStorageEnabled) {
        sWriteCount++;
        if (sUncertainWrite < 0) return RL_SAVE_IO;
        sDiskMeta = *meta;
        sDiskHasRun = run != NULL;
        if (run) sDiskRun = *run;
        return sUncertainWrite ? RL_SAVE_IO : RL_SAVE_OK;
    }
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
    RlBattleRules rules;
    RlMeta profile_before;
    RlMon draft_before[6];
    unsigned old_count;

    if (argc == 2 && !strcmp(argv[1], "--layout")) {
        printf("{\"mon_size\":%u,\"rules_size\":%u,\"relics\":%u,"
               "\"pending_size\":%u,\"rules\":%u,\"player_slots\":%u,"
               "\"trainer\":%u,\"trainer_count\":%u}\n",
               (unsigned)sizeof(RlMon), (unsigned)sizeof(RlBattleRules),
               (unsigned)offsetof(RlBattleRules, relics),
               (unsigned)sizeof(RlPendingBattle),
               (unsigned)offsetof(RlPendingBattle, rules),
               (unsigned)offsetof(RlPendingBattle, player_slots),
               (unsigned)offsetof(RlPendingBattle, trainer),
               (unsigned)offsetof(RlPendingBattle, trainer_count));
        return 0;
    }

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

    /* Actual scene draft uses registered weights, themes and budgeted kits. */
    rl_play_init();
    rl_meta_init(&sPlayMeta);
    gRlPlayTestMode = 0;
    gRlPlayTestSeed = 123;
    sPlayStarterPool = RL_META_POOL_STARTERS;
    sPlayAscension = 0;
    assert(rl_play_begin_new());
    memcpy(draft_before, sPlayDraft, sizeof(draft_before));
    for (i = 0; i < 6; i++) {
        unsigned j;
        assert(rl_meta_species_unlocked(&sPlayMeta, sPlayDraft[i].species));
        assert(sPlayDraft[i].level == 30 && sPlayDraft[i].dvs == 0x9999);
        assert(sPlayDraft[i].held_relic == RL_NO_SLOT);
        for (j = 0; j < 5; j++) assert(!sPlayDraft[i].stat_exp[j]);
        for (j = 0; j < 4; j++)
            assert(sPlayDraft[i].max_pp[j] == (sPlayDraft[i].moves[j] ? 15 : 0));
    }
    assert(rl_play_begin_new());
    assert(!memcmp(draft_before, sPlayDraft, sizeof(draft_before)));
    sPlayMeta.registered_species[(140 - 1) >> 3] |= 1U << ((140 - 1) & 7);
    count = 0;
    for (lane = 1; lane <= 128; lane++) {
        gRlPlayTestSeed = lane;
        assert(rl_play_begin_new());
        for (i = 0; i < 6; i++) {
            unsigned j;
            assert(rl_meta_species_unlocked(&sPlayMeta, sPlayDraft[i].species));
            if (sPlayDraft[i].species == 140) count++;
            for (j = 0; j < i; j++)
                assert(sPlayDraft[i].species != sPlayDraft[j].species);
        }
    }
    assert(count > 0);
    gRlPlayTestSeed = 123;
    sPlayMeta.options = (1U << RL_META_THEME_1) | (1U << RL_META_KIT_POTION) |
        (1U << RL_META_MOVESET_2);
    gRlPlayProfile.theme = RL_META_THEME_FOREST;
    gRlPlayProfile.kit = RL_META_KIT_POTION;
    assert(rl_play_begin_new());
    assert(gRlRun.money == 100 && gRlRun.bag[RL_POTION] == 1);
    for (i = 0; i < 6; i++) {
        unsigned j;
        for (j = 0; j < 24; j++)
            if (sPlayDraft[i].species == sPlayThemed[j]) break;
        assert(j < 24);
    }
    before = gRlRun;
    for (i = 0; i < 6; i++) {
        RlMon mon_before;
        mon_before = sPlayDraft[i];
        assert(rl_play_cycle_variant(i));
        assert(sPlayDraft[i].species == mon_before.species);
        assert(sPlayDraft[i].dvs == mon_before.dvs &&
               !memcmp(sPlayDraft[i].stat_exp, mon_before.stat_exp,
                       sizeof(mon_before.stat_exp)));
        assert(!memcmp(&before, &gRlRun, sizeof(before)));
    }

    /* UI edits retain a suspended run and retry the exact encoded candidate. */
    sStorageEnabled = 1;
    rl_meta_init(&sPlayMeta);
    sPlayMeta.runs = 2;
    sPlayMeta.currency = 100;
    sPlayMeta.pending_registration_count = 2;
    sPlayMeta.pending_species[(150 - 1) >> 3] |= 1U << ((150 - 1) & 7);
    sPlayMeta.pending_species[(149 - 1) >> 3] |= 1U << ((149 - 1) & 7);
    sPlayMeta.pending_species[(148 - 1) >> 3] |= 1U << ((148 - 1) & 7);
    sDiskMeta = sPlayMeta;
    rl_run_new(&sDiskRun, 88, 0);
    sDiskRun.party[0] = sPlayDraft[0];
    sDiskRun.party_count = 1;
    sDiskRun.phase = RL_MAP;
    sDiskRun.party[0].cur_hp -= 3;
    sDiskRun.party[0].pp[0]--;
    sDiskRun.party[0].status = 8;
    sDiskHasRun = 1;
    before = sDiskRun;
    profile_before = sPlayMeta;
    sUncertainWrite = 1;
    assert(rl_play_edit(0, RL_META_MOVESET_2) == RL_SAVE_IO);
    assert(gRlPlayProfile.edit_pending && sPlayMeta.currency == 100);
    assert(sDiskMeta.currency == 70 && sWriteCount == 1);
    assert(!memcmp(&before, &sDiskRun, sizeof(before)));
    assert(rl_play_commit_edit() == RL_SAVE_OK);
    assert(!gRlPlayProfile.edit_pending && sPlayMeta.currency == 70);
    assert(sWriteCount == 1 && sPlayMeta.runs == profile_before.runs);
    assert(rl_play_edit(2, 0) == RL_SAVE_IO);
    assert(sDiskMeta.pending_registration_count == 1);
    assert(rl_play_commit_edit() == RL_SAVE_OK);
    assert(sPlayMeta.pending_registration_count == 1 && sWriteCount == 2);
    assert(rl_play_edit(1, 150) == RL_SAVE_IO);
    assert(rl_play_commit_edit() == RL_SAVE_OK);
    assert(rl_meta_species_unlocked(&sPlayMeta, 150));
    assert(sPlayMeta.pending_registration_count == 0 && sWriteCount == 3);
    assert(!memcmp(&before, &sDiskRun, sizeof(before)));
    old_count = sWriteCount;
    assert(rl_play_edit(1, 150) == RL_SAVE_OK);
    assert(sWriteCount == (int)old_count && sPlayMeta.pending_registration_count == 0);
    assert(rl_play_edit(0, RL_META_EVENT_PACK_1) == RL_SAVE_INVALID);
    rl_play_meta_menu();
    sPlayList.cursor = 2;
    old_count = sWriteCount;
    rl_play_update_meta(RL_UI_A);
    assert(sPlayMessage && !strcmp(sPlayMessage,
                                  "Already owned; permanent coins were not spent"));
    assert(sWriteCount == (int)old_count);

    /* New Run cannot bypass pending choices, including after a cold load. */
    sDiskMeta.pending_registration_count = 1;
    sDiskMeta.pending_species[(149 - 1) >> 3] |= 1U << ((149 - 1) & 7);
    rl_play_request_new();
    assert(gRlPlayScene == RL_PLAY_REGISTER && sPlayNewAfterRegistration);
    assert(!memcmp(&before, &sDiskRun, sizeof(before)));
    sUncertainWrite = 0;
    sPlayList.cursor = sPlayList.count - 2;
    gRlPlayProfile.theme = RL_NO_SLOT;
    gRlPlayProfile.kit = RL_META_KIT_DEFAULT;
    rl_play_update_meta(RL_UI_A);
    assert(gRlPlayScene == RL_PLAY_DRAFT && !sPlayMeta.pending_registration_count);

    sDiskMeta.pending_registration_count = 2;
    sDiskMeta.pending_species[(149 - 1) >> 3] |= 1U << ((149 - 1) & 7);
    sDiskMeta.pending_species[(148 - 1) >> 3] |= 1U << ((148 - 1) & 7);
    sPlayMeta = sDiskMeta;
    sUncertainWrite = -1;
    old_count = sWriteCount;
    assert(rl_play_edit(2, 0) == RL_SAVE_IO);
    assert(sDiskMeta.pending_registration_count == 2 &&
           gRlPlayProfile.edit_candidate.pending_registration_count == 1);
    sReadFailure = RL_SAVE_RECOVERED;
    assert(rl_play_commit_edit() == RL_SAVE_RECOVERED);
    assert(gRlPlayProfile.edit_pending && sPlayMeta.pending_registration_count == 2);
    sReadFailure = RL_SAVE_OK;
    sDiskRun.rng ^= 1;
    assert(rl_play_commit_edit() == RL_SAVE_DEVICE_CHANGED);
    assert(sWriteCount == (int)old_count + 1);
    sDiskRun.rng ^= 1;
    sUncertainWrite = 0;
    assert(rl_play_commit_edit() == RL_SAVE_OK);
    assert(sDiskMeta.pending_registration_count == 1 && sWriteCount == (int)old_count + 2);

    /* Terminal win recording remains once-only and distinct from profile edits. */
    profile_before = sPlayMeta;
    gRlRun = sDiskRun;
    gRlRun.act = 3;
    gRlRun.row = gRlRun.lane = 0;
    gRlRun.phase = RL_WON;
    gRlRun.party[0].species = 8;
    gRlRun.stats.battles = gRlRun.stats.wins = 1;
    rl_map_generate(&gRlRun.map, &gRlRun.rng, 3, 0);
    sPlaySummaryPrepared = sPlaySummarySaved = 0;
    sUncertainWrite = 1;
    old_count = sWriteCount;
    assert(rl_play_store(0) == RL_SAVE_IO);
    assert(sPlaySummaryPrepared && !sPlaySummarySaved && !sDiskHasRun);
    assert(sDiskMeta.runs == profile_before.runs + 1 &&
           sDiskMeta.wins == profile_before.wins + 1);
    assert(sDiskMeta.currency == profile_before.currency + 35);
    assert(sDiskMeta.pending_registration_count == profile_before.pending_registration_count + 1);
    assert(rl_play_store(0) == RL_SAVE_OK);
    assert(sWriteCount == (int)old_count + 1 && sPlaySummarySaved);
    assert(rl_play_edit(1, 8) == RL_SAVE_IO);
    assert(rl_play_commit_edit() == RL_SAVE_OK);
    assert(sPlayMeta.currency == profile_before.currency + 35 &&
           sPlayMeta.runs == profile_before.runs + 1);
    return 0;
}
