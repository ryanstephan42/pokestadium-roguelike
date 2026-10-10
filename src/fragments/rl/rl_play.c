#ifdef ROGUELIKE
#include "global.h"
#include "src/math_util.h"
#include "src/roguelike/rl_battle.h"
#include "src/roguelike/rl_main.h"
#include "src/roguelike/rl_rosters.h"
#include "src/roguelike/rl_save.h"
#include "src/roguelike/rl_text.h"
#include "rl_play.h"
#include "rl_ui.h"

#include "rl_gen/rl_rosters.h"

#define RL_PLAY_DRAFT_SIZE 6
#define RL_PLAY_REWARD_SIZE 3
#define RL_PLAY_ROUTE_SIZE 3
#define RL_PLAY_POOL_SIZE 256

enum {
    RL_PLAY_MENU,
    RL_PLAY_DRAFT,
    RL_PLAY_MAP,
    RL_PLAY_PARTY,
    RL_PLAY_REWARD,
    RL_PLAY_SUMMARY,
    RL_PLAY_ERROR,
    RL_PLAY_BATTLE
};

static const Color_RGBA8 sPlayWhite = { 255, 255, 255, 255 };
static const Color_RGBA8 sPlayGold = { 255, 224, 96, 255 };
static const Color_RGBA8 sPlayMuted = { 160, 176, 192, 255 };
static RlUiList sPlayList;
static RlUiItem sPlayItems[9];
static char sPlayLabels[9][36];
static RlMon sPlayPool[RL_PLAY_POOL_SIZE];
static RlPool sPlayRewardPool;
static RlMon sPlayDraft[RL_PLAY_DRAFT_SIZE];
static unsigned char sPlayDraftSelected[RL_PLAY_DRAFT_SIZE];
static unsigned char sPlayDraftOldSpecies[RL_PLAY_DRAFT_SIZE];
static RlReward sPlayRewards[RL_PLAY_REWARD_SIZE];
static unsigned char sPlayActiveSlots[3];
static unsigned char sPlayActiveCount;
static unsigned char sPlayChosenLane;
static unsigned char sPlayRerollUsed;
static int sPlayPoolCount;
static int sPlayRewardCount;
static const char* sPlayMessage;

static unsigned rl_play_active_selected(unsigned slot);
static void rl_play_make_summary(void);

static void rl_play_copy_text(char* output, const char* text) {
    unsigned i;
    if (output == NULL) {
        return;
    }
    if (text == NULL) {
        output[0] = '\0';
        return;
    }
    for (i = 0; i < 35 && text[i] != '\0'; i++) {
        output[i] = text[i];
    }
    output[i] = '\0';
}

static void rl_play_hex(char* output, unsigned value) {
    static const char digits[] = "0123456789ABCDEF";
    unsigned i;
    for (i = 0; i < 8; i++) {
        output[i] = digits[(value >> ((7 - i) * 4)) & 15];
    }
    output[8] = '\0';
}

static void rl_play_number(char* output, unsigned value, unsigned digits) {
    unsigned i;
    for (i = digits; i > 0; i--) {
        output[i - 1] = (char)('0' + value % 10);
        value /= 10;
    }
}

static void rl_play_mon_label(char* output, const RlMon* mon, int selected) {
    if (mon == NULL || mon->species == 0) {
        rl_play_copy_text(output, "Empty");
        return;
    }
    output[0] = selected ? '*' : ' ';
    output[1] = 'N';
    output[2] = 'o';
    output[3] = '.';
    rl_play_number(output + 4, mon->species, 3);
    output[7] = ' ';
    output[8] = 'L';
    rl_play_number(output + 9, mon->level, 2);
    output[11] = ' ';
    output[12] = 'H';
    output[13] = 'P';
    rl_play_number(output + 14, mon->cur_hp, 3);
    output[17] = '/';
    rl_play_number(output + 18, mon->max_hp, 3);
    output[21] = '\0';
}

static unsigned rl_play_count_selected(void) {
    unsigned i;
    unsigned count;
    count = 0;
    for (i = 0; i < RL_PLAY_DRAFT_SIZE; i++) {
        count += sPlayDraftSelected[i] != 0;
    }
    return count;
}

static unsigned rl_play_live_party(void) {
    unsigned i;
    unsigned count;
    count = 0;
    for (i = 0; i < gRlRun.party_count; i++) {
        if (gRlRun.party[i].species && !gRlRun.party[i].fainted &&
            gRlRun.party[i].cur_hp) {
            count++;
        }
    }
    return count;
}

static void rl_play_set_scene(unsigned scene) {
    gRlPlayScene = scene;
    sPlayMessage = NULL;
}

static int rl_play_init_list(const char* title, int count, int visible) {
    return rl_ui_list_init(&sPlayList, title, sPlayItems, count, visible);
}

static void rl_play_build_root(void) {
    sPlayItems[0].text = "New run - Poke Cup rentals";
    sPlayItems[0].disabled = 0;
    sPlayItems[1].text = "Resume from Controller Pak";
    sPlayItems[1].disabled = 0;
    sPlayItems[2].text = "Return to Menu Select";
    sPlayItems[2].disabled = 0;
    rl_play_init_list("ROGUELIKE RUN", 3, 3);
}

static void rl_play_make_draft(void) {
    unsigned indices[RL_PLAY_POOL_SIZE];
    unsigned i;
    unsigned j;
    unsigned available;
    unsigned pick;
    int avoid_previous;

    if (sPlayPoolCount <= 0 || sPlayPoolCount > RL_PLAY_POOL_SIZE) {
        sPlayMessage = "Rental pool is unavailable";
        gRlPlayScene = RL_PLAY_ERROR;
        return;
    }
    for (i = 0; i < RL_PLAY_DRAFT_SIZE; i++) {
        sPlayDraftOldSpecies[i] = sPlayDraft[i].species;
    }
    avoid_previous = 0;
    for (i = 0; i < RL_PLAY_DRAFT_SIZE; i++) {
        if (sPlayDraftOldSpecies[i] != 0) {
            avoid_previous = 1;
        }
    }
    for (i = 0; i < (unsigned)sPlayPoolCount; i++) {
        indices[i] = i;
    }
    available = (unsigned)sPlayPoolCount;
    for (i = 0; i < RL_PLAY_DRAFT_SIZE; i++) {
        if (avoid_previous && available > RL_PLAY_DRAFT_SIZE) {
            for (j = 0; j < available; ) {
                unsigned k;
                int old;
                old = 0;
                for (k = 0; k < RL_PLAY_DRAFT_SIZE; k++) {
                    if (sPlayPool[indices[j]].species == sPlayDraftOldSpecies[k]) {
                        old = 1;
                    }
                }
                if (old) {
                    indices[j] = indices[available - 1];
                    available--;
                } else {
                    j++;
                }
            }
        }
        if (available == 0) {
            sPlayMessage = "Not enough distinct rentals to reroll";
            gRlPlayScene = RL_PLAY_ERROR;
            return;
        }
        pick = rl_random_bounded(&gRlRun.rng, available);
        sPlayDraft[i] = sPlayPool[indices[pick]];
        gRlPlayDraftSpecies[i] = sPlayDraft[i].species;
        indices[pick] = indices[available - 1];
        available--;
    }
    for (i = 0; i < RL_PLAY_DRAFT_SIZE; i++) {
        sPlayDraftSelected[i] = 0;
    }
    for (i = 0; i < RL_PLAY_DRAFT_SIZE; i++) {
        rl_play_mon_label(sPlayLabels[i], &sPlayDraft[i], 0);
        sPlayItems[i].text = sPlayLabels[i];
        sPlayItems[i].disabled = 0;
    }
    sPlayItems[6].text = sPlayRerollUsed ? "Free reroll used" : "Use free reroll";
    sPlayItems[6].disabled = sPlayRerollUsed;
    sPlayItems[7].text = "Confirm 3 rentals";
    sPlayItems[7].disabled = 1;
    rl_play_init_list("Choose exactly 3 of 6", 8, 6);
}

static void rl_play_refresh_draft(void) {
    unsigned i;
    unsigned selected;
    selected = rl_play_count_selected();
    for (i = 0; i < RL_PLAY_DRAFT_SIZE; i++) {
        rl_play_mon_label(sPlayLabels[i], &sPlayDraft[i], sPlayDraftSelected[i]);
    }
    sPlayItems[6].text = sPlayRerollUsed ? "Free reroll used" : "Use free reroll";
    sPlayItems[6].disabled = sPlayRerollUsed;
    sPlayItems[7].text = "Confirm 3 rentals";
    sPlayItems[7].disabled = selected != 3;
}

static int rl_play_begin_new(void) {
    unsigned i;
    unsigned seed;

    seed = gRlPlayTestSeed;
    if (seed == 0) {
        seed = ((unsigned)MathUtil_Random16() << 16) | MathUtil_Random16();
    }
    rl_run_new(&gRlRun, seed, 0);
    sPlayRerollUsed = 0;
    sPlayChosenLane = 0;
    sPlayPoolCount = rl_load_rental_pool(RL_RENTAL_SET_POKE, sPlayPool,
                                         RL_PLAY_POOL_SIZE);
    if (sPlayPoolCount < RL_PLAY_DRAFT_SIZE) {
        sPlayMessage = "Could not load the Poke Cup rental pool";
        gRlPlayScene = RL_PLAY_ERROR;
        return 0;
    }
    if (rl_map_prepare_battle_route(&gRlRun.map, RL_PLAY_ROUTE_SIZE) != RL_OK) {
        sPlayMessage = "Could not prepare the trainer route";
        gRlPlayScene = RL_PLAY_ERROR;
        return 0;
    }
    for (i = 0; i < RL_PLAY_DRAFT_SIZE; i++) {
        sPlayDraft[i].species = 0;
        sPlayDraftSelected[i] = 0;
        sPlayDraftOldSpecies[i] = 0;
    }
    sPlayMessage = NULL;
    rl_play_make_draft();
    if (gRlPlayScene == RL_PLAY_ERROR) {
        return 0;
    }
    rl_play_set_scene(RL_PLAY_DRAFT);
    return 1;
}

static void rl_play_make_map(void) {
    unsigned row;
    unsigned lanes;
    unsigned lane;

    row = gRlRun.row == RL_NO_SLOT ? 0 : (unsigned)gRlRun.row + 1;
    if (row >= RL_PLAY_ROUTE_SIZE || gRlRun.phase != RL_MAP) {
        gRlRun.phase = RL_WON;
        rl_play_make_summary();
        return;
    }
    lanes = gRlRun.map.lanes[row];
    for (lane = 0; lane < lanes; lane++) {
        int reachable;
        reachable = gRlRun.row == RL_NO_SLOT ||
            (gRlRun.map.nodes[gRlRun.row][gRlRun.lane].edges & (1U << lane));
        sPlayLabels[lane][0] = 'T';
        sPlayLabels[lane][1] = 'r';
        sPlayLabels[lane][2] = 'a';
        sPlayLabels[lane][3] = 'i';
        sPlayLabels[lane][4] = 'n';
        sPlayLabels[lane][5] = 'e';
        sPlayLabels[lane][6] = 'r';
        sPlayLabels[lane][7] = ' ';
        sPlayLabels[lane][8] = 'r';
        sPlayLabels[lane][9] = 'o';
        sPlayLabels[lane][10] = 'u';
        sPlayLabels[lane][11] = 't';
        sPlayLabels[lane][12] = 'e';
        sPlayLabels[lane][13] = ' ';
        sPlayLabels[lane][14] = (char)('1' + lane);
        sPlayLabels[lane][15] = '\0';
        sPlayItems[lane].text = sPlayLabels[lane];
        sPlayItems[lane].disabled = !reachable;
    }
    sPlayItems[lanes].text = "Save run and return";
    sPlayItems[lanes].disabled = 0;
    rl_play_init_list("Choose a trainer route", (int)lanes + 1, 5);
    rl_play_set_scene(RL_PLAY_MAP);
}

static void rl_play_make_party(void) {
    unsigned i;
    unsigned live;
    unsigned required;
    unsigned selected;
    int result;

    live = rl_play_live_party();
    if (!live) {
        gRlRun.phase = RL_LOST;
        rl_play_make_summary();
        return;
    }
    required = live < 3 ? live : 3;
    sPlayActiveCount = 0;
    selected = 0;
    for (i = 0; i < gRlRun.party_count && selected < required; i++) {
        if (!gRlRun.party[i].fainted && gRlRun.party[i].cur_hp) {
            sPlayActiveSlots[selected++] = (unsigned char)i;
        }
    }
    sPlayActiveCount = (unsigned char)selected;
    for (i = 0; i < gRlRun.party_count; i++) {
        rl_play_mon_label(sPlayLabels[i], &gRlRun.party[i],
                          rl_play_active_selected(i));
        sPlayItems[i].text = sPlayLabels[i];
        sPlayItems[i].disabled = gRlRun.party[i].fainted ||
            gRlRun.party[i].cur_hp == 0;
    }
    sPlayItems[gRlRun.party_count].text = "Confirm active team";
    sPlayItems[gRlRun.party_count].disabled = sPlayActiveCount != required ||
        sPlayActiveCount == 0;
    result = rl_play_init_list("Select up to 3; fainted mons stay benched",
                               (int)gRlRun.party_count + 1, 6);
    if (result != RL_UI_NONE) {
        sPlayMessage = "Could not create active-team list";
        gRlPlayScene = RL_PLAY_ERROR;
        return;
    }
    rl_play_set_scene(RL_PLAY_PARTY);
}

static unsigned rl_play_active_required(void) {
    unsigned live;
    live = rl_play_live_party();
    return live < 3 ? live : 3;
}

static unsigned rl_play_active_selected(unsigned slot) {
    unsigned i;
    for (i = 0; i < sPlayActiveCount; i++) {
        if (sPlayActiveSlots[i] == slot) {
            return 1;
        }
    }
    return 0;
}

static int rl_play_active_toggle(unsigned slot) {
    unsigned i;
    unsigned required;
    required = rl_play_active_required();
    for (i = 0; i < sPlayActiveCount; i++) {
        if (sPlayActiveSlots[i] == slot) {
            for (; i + 1 < sPlayActiveCount; i++) {
                sPlayActiveSlots[i] = sPlayActiveSlots[i + 1];
            }
            sPlayActiveCount--;
            return 1;
        }
    }
    if (sPlayActiveCount >= required || sPlayActiveCount >= 3) {
        return 0;
    }
    sPlayActiveSlots[sPlayActiveCount++] = (unsigned char)slot;
    return 1;
}

static const char* rl_play_reward_label(const RlReward* reward) {
    switch (reward->kind) {
        case RL_REWARD_RECRUIT: return "Recruit from defeated trainer";
        case RL_REWARD_CANDY: return "Rare Candy";
        case RL_REWARD_VITAMIN: return "Vitamin";
        case RL_REWARD_RELIC: return "Run relic";
        case RL_REWARD_MONEY: return "Money";
        case RL_REWARD_TM: return "Move TM";
        default: return "Invalid reward";
    }
}

static int rl_play_reward_disabled(unsigned index) {
    if (index >= (unsigned)sPlayRewardCount) {
        return 1;
    }
    return sPlayRewards[index].kind == RL_REWARD_RECRUIT &&
        gRlRun.party_count >= RL_PARTY;
}

static void rl_play_make_rewards(void) {
    unsigned i;
    if (!gRlPendingBattle.trainer_count ||
        gRlPendingBattle.trainer_count > RL_BATTLE_MAX_MONS) {
        sPlayMessage = "Defeated trainer roster is unavailable";
        gRlPlayScene = RL_PLAY_ERROR;
        return;
    }
    sPlayRewardPool.mons = gRlPendingBattle.trainer;
    sPlayRewardPool.count = gRlPendingBattle.trainer_count;
    sPlayRewardPool.fixed_order = 0;
    sPlayRewardCount = (int)rl_rewards_generate(&gRlRun, &sPlayRewardPool, NULL,
                                                sPlayRewards,
                                                RL_PLAY_REWARD_SIZE);
    if (sPlayRewardCount != RL_PLAY_REWARD_SIZE) {
        sPlayMessage = "Reward generation failed";
        gRlPlayScene = RL_PLAY_ERROR;
        return;
    }
    for (i = 0; i < RL_PLAY_REWARD_SIZE; i++) {
        if (sPlayRewards[i].kind == RL_REWARD_RECRUIT) {
            char* label;
            label = sPlayLabels[i];
            rl_play_copy_text(label, "Trainer No.");
            rl_play_number(label + 11, sPlayRewards[i].mon.species, 3);
            label[14] = ' ';
            label[15] = 'L';
            rl_play_number(label + 16, sPlayRewards[i].mon.level, 2);
            label[18] = '\0';
            sPlayItems[i].text = label;
        } else {
            sPlayItems[i].text = rl_play_reward_label(&sPlayRewards[i]);
        }
        sPlayItems[i].disabled = rl_play_reward_disabled(i);
    }
    rl_play_init_list("Choose one reward", sPlayRewardCount, 3);
    rl_play_set_scene(RL_PLAY_REWARD);
}

static void rl_play_make_summary(void) {
    sPlayItems[0].text = "Save results (replaces Pak run)";
    sPlayItems[0].disabled = 0;
    sPlayItems[1].text = "Return without saving";
    sPlayItems[1].disabled = 0;
    rl_play_init_list("Run summary", 2, 2);
    rl_play_set_scene(RL_PLAY_SUMMARY);
}

static void rl_play_after_reward(void) {
    unsigned row;
    row = gRlRun.row;
    if (rl_run_finish_node(&gRlRun) != RL_OK) {
        sPlayMessage = "Could not finish the selected route";
        gRlPlayScene = RL_PLAY_ERROR;
        return;
    }
    if (row + 1 >= RL_PLAY_ROUTE_SIZE) {
        gRlRun.phase = RL_WON;
        rl_play_make_summary();
    } else {
        rl_play_make_map();
    }
}

static int rl_play_run_battle(void) {
    RlMon player[3];
    RlMon enemy[RL_BATTLE_MAX_MONS];
    RlBattleRules rules;
    unsigned char ai;
    unsigned i;
    int enemy_count;
    int chosen_trainer;
    unsigned trainer_count;

    if (!sPlayActiveCount || sPlayActiveCount > 3) {
        sPlayMessage = "No active Pokemon selected";
        gRlPlayScene = RL_PLAY_ERROR;
        return 0;
    }
    for (i = 0; i < sPlayActiveCount; i++) {
        player[i] = gRlRun.party[sPlayActiveSlots[i]];
        if (gRlPlayTestMode) {
            player[i].level = 100;
            player[i].exp = rl_xp_for_level(100, player[i].growth);
            player[i].dvs = 0xFFFF;
            player[i].stat_exp[0] = 65535;
            player[i].stat_exp[1] = 65535;
            player[i].stat_exp[2] = 65535;
            player[i].stat_exp[3] = 65535;
            player[i].stat_exp[4] = 65535;
            if (rl_prepare_mon(&player[i], 0) != 0) {
                sPlayMessage = "Could not prepare test Pokemon";
                gRlPlayScene = RL_PLAY_ERROR;
                return 0;
            }
            if (gRlPlayTestMode == 3) {
                player[i].moves[0] = 45;
                player[i].pp[0] = 40;
                player[i].max_pp[0] = 40;
                player[i].cur_hp = 1;
            } else if (gRlPlayTestMode == 4 &&
                       gRlRun.stats.battles == 0 && i == 0) {
                player[i].moves[0] = 45;
                player[i].pp[0] = 40;
                player[i].max_pp[0] = 40;
                player[i].cur_hp = player[i].max_hp;
            }
        }
    }
    trainer_count = rl_trainer_count(RL_TRAINER_SET_POKE_POKE);
    if (!trainer_count) {
        sPlayMessage = "Poke Cup trainer pool is unavailable";
        gRlPlayScene = RL_PLAY_ERROR;
        return 0;
    }
    chosen_trainer = (int)rl_random_bounded(&gRlRun.rng, trainer_count);
    enemy_count = rl_load_trainer(RL_TRAINER_SET_POKE_POKE,
                                  (unsigned)chosen_trainer, enemy,
                                  RL_BATTLE_MAX_MONS, &ai);
    if (enemy_count < 1) {
        sPlayMessage = "Could not load the selected trainer";
        gRlPlayScene = RL_PLAY_ERROR;
        return 0;
    }
    {
        int trainer_count;
        trainer_count = enemy_count;
        if (enemy_count > 3) enemy_count = 3;
        bzero(&rules, sizeof(rules));
        rules.stadium = 3;
        rules.ai_level = ai;
        rules.player_count = sPlayActiveCount;
        rules.player_cpu = gRlPlayTestMode == 1;
        rules.show_intro = 1;
        rules.use_state = 1;
        rules.relics = gRlRun.relics;
        gRlPlayScene = RL_PLAY_BATTLE;
        if (!rl_main_queue_battle(player, sPlayActiveCount, enemy, enemy_count,
                                  &rules, sPlayActiveSlots, enemy,
                                  (unsigned)trainer_count)) {
            sPlayMessage = "Could not queue the native battle";
            gRlPlayScene = RL_PLAY_ERROR;
            return 0;
        }
    }
    return 1;
}

static RlSaveStatus rl_play_store(int include_run) {
    RlMeta meta;
    RunState saved_run;
    RlU8 has_run;
    RlSaveStatus status;
    int create;
    unsigned i;

    for (i = 0; i < sizeof(meta); i++) {
        ((unsigned char*)&meta)[i] = 0;
    }
    status = rl_save_load(0, &meta, &saved_run, &has_run);
    if (status == RL_SAVE_NOT_FOUND) {
        for (i = 0; i < sizeof(meta); i++) {
            ((unsigned char*)&meta)[i] = 0;
        }
        create = 1;
    } else if (status == RL_SAVE_OK) {
        create = 0;
    } else {
        return status;
    }
    if (!include_run) {
        meta.runs++;
        if (gRlRun.phase == RL_WON) {
            meta.wins++;
        }
        if (gRlRun.stats.nodes > meta.best_floor) {
            meta.best_floor = gRlRun.stats.nodes;
        }
    }
    return rl_save_store(0, &meta, include_run ? &gRlRun : NULL, create);
}

static void rl_play_resume(void) {
    RlMeta meta;
    RunState run;
    RlU8 has_run;
    RlSaveStatus status;

    status = rl_save_load(0, &meta, &run, &has_run);
    if (status != RL_SAVE_OK && status != RL_SAVE_RECOVERED) {
        sPlayMessage = rl_save_status_text(status);
        gRlPlayScene = RL_PLAY_ERROR;
        return;
    }
    if (!has_run || run.phase != RL_MAP || run.party_count == 0) {
        sPlayMessage = "Controller Pak has no resumable run";
        gRlPlayScene = RL_PLAY_ERROR;
        return;
    }
    sPlayPoolCount = rl_load_rental_pool(RL_RENTAL_SET_POKE, sPlayPool,
                                         RL_PLAY_POOL_SIZE);
    if (sPlayPoolCount < RL_PLAY_DRAFT_SIZE) {
        sPlayMessage = "Could not load the Poke Cup rental pool";
        gRlPlayScene = RL_PLAY_ERROR;
        return;
    }
    gRlRun = run;
    gRlPlayBattleCount = run.stats.battles;
    if (status == RL_SAVE_RECOVERED) {
        sPlayMessage = rl_save_status_text(status);
    }
    rl_play_make_map();
    if (status == RL_SAVE_RECOVERED) {
        sPlayMessage = rl_save_status_text(status);
    }
}

static void rl_play_toggle_draft(unsigned index) {
    if (index >= RL_PLAY_DRAFT_SIZE) {
        return;
    }
    if (sPlayDraftSelected[index]) {
        sPlayDraftSelected[index] = 0;
    } else if (rl_play_count_selected() < 3) {
        sPlayDraftSelected[index] = 1;
    } else {
        sPlayMessage = "Select exactly three rentals";
    }
    rl_play_refresh_draft();
}

static void rl_play_confirm_draft(void) {
    unsigned i;
    if (rl_play_count_selected() != 3) {
        sPlayMessage = "Select exactly three rentals";
        return;
    }
    gRlRun.party_count = 0;
    for (i = 0; i < RL_PLAY_DRAFT_SIZE; i++) {
        if (sPlayDraftSelected[i] &&
            rl_party_recruit(&gRlRun, &sPlayDraft[i]) != RL_OK) {
            sPlayMessage = "Could not add selected starter";
            gRlPlayScene = RL_PLAY_ERROR;
            return;
        }
    }
    gRlRun.phase = RL_MAP;
    gRlRun.row = RL_NO_SLOT;
    gRlRun.lane = RL_NO_SLOT;
    gRlRun.stats.nodes = 0;
    gRlRun.stats.battles = 0;
    gRlRun.stats.wins = 0;
    gRlPlayBattleCount = 0;
    rl_play_make_map();
}

static void rl_play_update_root(u16 pressed) {
    int result;
    result = rl_ui_list(&sPlayList, pressed);
    if (result == 0) {
        rl_play_begin_new();
    } else if (result == 1) {
        rl_play_resume();
    } else if (result == 2 || result == RL_UI_CANCEL) {
        gRlMain.returnState = STATE_MENU_SELECT;
        gRlPlayScene = RL_PLAY_MENU;
        sPlayMessage = NULL;
        gRlPlayExit = 1;
    }
}

static void rl_play_update_draft(u16 pressed) {
    int result;
    rl_play_refresh_draft();
    result = rl_ui_list(&sPlayList, pressed);
    if (result >= 0 && result < RL_PLAY_DRAFT_SIZE) {
        rl_play_toggle_draft((unsigned)result);
    } else if (result == 6 && !sPlayRerollUsed) {
        sPlayRerollUsed = 1;
        rl_play_make_draft();
    } else if (result == 7) {
        rl_play_confirm_draft();
    } else if (result == RL_UI_CANCEL) {
        rl_play_build_root();
        rl_play_set_scene(RL_PLAY_MENU);
    }
}

static void rl_play_update_map(u16 pressed) {
    unsigned row;
    unsigned lane_count;
    int result;
    result = rl_ui_list(&sPlayList, pressed);
    row = gRlRun.row == RL_NO_SLOT ? 0 : (unsigned)gRlRun.row + 1;
    lane_count = gRlRun.map.lanes[row];
    if (result >= 0 && (unsigned)result < lane_count) {
        sPlayChosenLane = (unsigned char)result;
        rl_play_make_party();
    } else if (result == (int)lane_count) {
        RlSaveStatus status;
        status = rl_play_store(1);
        if (status == RL_SAVE_OK) {
            gRlMain.returnState = STATE_MENU_SELECT;
            gRlPlayExit = 1;
        } else {
            sPlayMessage = rl_save_status_text(status);
        }
    } else if (result == RL_UI_CANCEL) {
        sPlayMessage = "Choose Save run and return to suspend safely";
    }
}

static void rl_play_update_party(u16 pressed) {
    int result;
    unsigned slot;
    unsigned required;
    unsigned i;
    required = rl_play_active_required();
    for (i = 0; i < gRlRun.party_count; i++) {
        rl_play_mon_label(sPlayLabels[i], &gRlRun.party[i],
                          rl_play_active_selected(i));
        sPlayItems[i].text = sPlayLabels[i];
        sPlayItems[i].disabled = gRlRun.party[i].fainted ||
            gRlRun.party[i].cur_hp == 0;
    }
    sPlayItems[gRlRun.party_count].text = "Confirm active team";
    sPlayItems[gRlRun.party_count].disabled = sPlayActiveCount != required ||
        sPlayActiveCount == 0;
    result = rl_ui_list(&sPlayList, pressed);
    if (result >= 0 && (unsigned)result < gRlRun.party_count) {
        slot = (unsigned)result;
        if (!rl_play_active_toggle(slot)) {
            sPlayMessage = "Select no more than three active Pokemon";
        }
    } else if (result == (int)gRlRun.party_count) {
        if (rl_run_advance(&gRlRun, sPlayChosenLane) != RL_OK) {
            rl_play_make_map();
            sPlayMessage = "That route is no longer available";
            return;
        }
        if (rl_play_run_battle()) {
            gRlPlayExit = 1;
        }
    } else if (result == RL_UI_CANCEL) {
        rl_play_make_map();
    }
}

static void rl_play_update_reward(u16 pressed) {
    int result;
    result = rl_ui_list(&sPlayList, pressed);
    if (result >= 0 && result < sPlayRewardCount) {
        if (rl_reward_apply(&gRlRun, &sPlayRewards[result]) != RL_OK) {
            sPlayMessage = "Could not apply selected reward";
            return;
        }
        if (sPlayRewards[result].kind == RL_REWARD_RELIC &&
            sPlayRewards[result].detail == RL_PP_POUCH &&
            rl_battle_apply_pp_pouch(&gRlRun) < 0) {
            sPlayMessage = "Could not apply PP Pouch";
            gRlPlayScene = RL_PLAY_ERROR;
            return;
        }
        rl_play_after_reward();
    } else if (result == RL_UI_CANCEL) {
        sPlayMessage = "Choose a reward to continue";
    }
}

static void rl_play_update_summary(u16 pressed) {
    int result;
    result = rl_ui_list(&sPlayList, pressed);
    if (result == 0) {
        RlSaveStatus status;
        status = rl_play_store(0);
        if (status == RL_SAVE_OK) {
            gRlMain.returnState = STATE_MENU_SELECT;
            gRlPlayExit = 1;
        } else {
            sPlayMessage = rl_save_status_text(status);
        }
    } else if (result == 1 || result == RL_UI_CANCEL) {
        rl_play_build_root();
        rl_play_set_scene(RL_PLAY_MENU);
    }
}

s32 rl_play_init(void) {
    s32 result;
    gRlPlayExit = 0;
    sPlayMessage = NULL;
    result = rl_ui_init();
    if (result != RL_UI_NONE) {
        sPlayMessage = "UI resources unavailable";
        gRlPlayScene = RL_PLAY_ERROR;
        return result;
    }
    if (gRlPendingBattle.post_battle) {
        gRlPendingBattle.post_battle = 0;
        if (gRlPendingBattle.error) {
            sPlayMessage = gRlPendingBattle.error == 1
                ? "Native battle launch failed"
                : "Could not apply native battle results";
            gRlPendingBattle.error = 0;
            rl_play_build_root();
            gRlPlayScene = RL_PLAY_ERROR;
        } else if (gRlRun.phase == RL_LOST) {
            rl_play_make_summary();
        } else if (gRlPendingBattle.outcome == RL_BATTLE_WIN &&
                   gRlRun.phase == RL_REWARD) {
            sPlayPoolCount = rl_load_rental_pool(RL_RENTAL_SET_POKE, sPlayPool,
                                                 RL_PLAY_POOL_SIZE);
            if (sPlayPoolCount < RL_PLAY_DRAFT_SIZE) {
                sPlayMessage = "Could not load rewards after battle";
                gRlPlayScene = RL_PLAY_ERROR;
            } else {
                rl_play_make_rewards();
            }
        } else if (gRlRun.phase == RL_MAP) {
            rl_play_make_map();
            sPlayMessage = "Trainer won; survivors continue without a reward";
        } else {
            sPlayMessage = "Unexpected run state after native battle";
            gRlPlayScene = RL_PLAY_ERROR;
        }
        return RL_UI_NONE;
    }
    gRlPlayScene = RL_PLAY_MENU;
    rl_play_build_root();
    return RL_UI_NONE;
}

s32 rl_play_update(u16 pressed) {
    if (gRlPlayExit) {
        return RL_UI_CANCEL;
    }
    switch (gRlPlayScene) {
        case RL_PLAY_MENU:
            rl_play_update_root(pressed);
            break;
        case RL_PLAY_DRAFT:
            rl_play_update_draft(pressed);
            break;
        case RL_PLAY_MAP:
            rl_play_update_map(pressed);
            break;
        case RL_PLAY_PARTY:
            rl_play_update_party(pressed);
            break;
        case RL_PLAY_REWARD:
            rl_play_update_reward(pressed);
            break;
        case RL_PLAY_SUMMARY:
            rl_play_update_summary(pressed);
            break;
        case RL_PLAY_ERROR:
            if (pressed & (RL_UI_A | RL_UI_B)) {
                rl_play_build_root();
                rl_play_set_scene(RL_PLAY_MENU);
            }
            break;
        default:
            sPlayMessage = "Invalid roguelike scene";
            gRlPlayScene = RL_PLAY_ERROR;
            break;
    }
    return gRlPlayExit ? RL_UI_CANCEL : RL_UI_NONE;
}

static void rl_play_draw_mon_detail(const RlMon* mon, s16 x, s16 y) {
    if (mon == NULL || mon->species == 0) {
        return;
    }
    rl_ui_mon_icon(mon->species, x + 54, y);
    rl_play_mon_label(sPlayLabels[8], mon, 0);
    rl_ui_text(x, y + 52, sPlayLabels[8], sPlayWhite);
    rl_ui_hp_bar(x + 4, y + 78, 156, mon->cur_hp, mon->max_hp);
}

static void rl_play_draw_status(void) {
    if (sPlayMessage != NULL) {
        rl_ui_text(42, 400, sPlayMessage, sPlayGold);
    }
}

void rl_play_draw(void) {
    unsigned row;
    unsigned i;

    rl_ui_window(24, 24, 592, 432);
    switch (gRlPlayScene) {
        case RL_PLAY_MENU:
            rl_ui_text(44, 40, "Pokemon Stadium Roguelike", sPlayGold);
            rl_ui_list_draw(&sPlayList, 44, 84, 540);
            rl_ui_text(44, 376, "New runs draft three real Poke Cup rentals.", sPlayMuted);
            break;
        case RL_PLAY_DRAFT:
            rl_ui_text(44, 40, "Starter draft - select 3 of 6", sPlayGold);
            rl_ui_list_draw(&sPlayList, 44, 80, 360);
            if (sPlayList.cursor < RL_PLAY_DRAFT_SIZE) {
                rl_play_draw_mon_detail(&sPlayDraft[sPlayList.cursor], 410, 120);
            }
            rl_ui_text(410, 280, "One free reroll", sPlayWhite);
            rl_ui_text(44, 374, "Selected: ", sPlayWhite);
            rl_play_number(sPlayLabels[8], rl_play_count_selected(), 1);
            sPlayLabels[8][1] = '\0';
            rl_ui_text(142, 374, sPlayLabels[8], sPlayWhite);
            break;
        case RL_PLAY_MAP:
            rl_ui_text(44, 40, "Three trainer encounters - choose a route", sPlayGold);
            rl_ui_list_draw(&sPlayList, 44, 84, 360);
            row = gRlRun.row == RL_NO_SLOT ? 0 : (unsigned)gRlRun.row + 1;
            rl_ui_text(424, 110, "Encounter", sPlayWhite);
            rl_play_number(sPlayLabels[8], row + 1, 1);
            sPlayLabels[8][1] = '\0';
            rl_ui_text(530, 110, sPlayLabels[8], sPlayWhite);
            rl_ui_text(424, 150, "Run seed", sPlayWhite);
            rl_play_hex(sPlayLabels[8], gRlRun.seed);
            rl_ui_text(424, 176, sPlayLabels[8], sPlayWhite);
            rl_ui_text(424, 224, "Party", sPlayWhite);
            for (i = 0; i < gRlRun.party_count && i < 4; i++) {
                rl_play_mon_label(sPlayLabels[8], &gRlRun.party[i], 0);
                rl_ui_text(424, 250 + i * 26, sPlayLabels[8], sPlayMuted);
            }
            break;
        case RL_PLAY_PARTY:
            rl_ui_text(44, 40, "Select the active team", sPlayGold);
            rl_ui_list_draw(&sPlayList, 44, 80, 360);
            rl_ui_text(424, 96, "Active", sPlayWhite);
            rl_play_number(sPlayLabels[8], sPlayActiveCount, 1);
            sPlayLabels[8][1] = '\0';
            rl_ui_text(502, 96, sPlayLabels[8], sPlayWhite);
            if (sPlayList.cursor < gRlRun.party_count) {
                rl_play_draw_mon_detail(&gRlRun.party[sPlayList.cursor], 410, 136);
            }
            break;
        case RL_PLAY_REWARD:
            rl_ui_text(44, 40, "Victory - choose one reward", sPlayGold);
            rl_ui_list_draw(&sPlayList, 44, 84, 360);
            if (sPlayList.cursor < (unsigned)sPlayRewardCount &&
                sPlayRewards[sPlayList.cursor].kind == RL_REWARD_RECRUIT) {
                rl_play_draw_mon_detail(&sPlayRewards[sPlayList.cursor].mon, 410, 130);
            }
            rl_ui_text(424, 326, "Party slots", sPlayWhite);
            rl_play_number(sPlayLabels[8], gRlRun.party_count, 1);
            sPlayLabels[8][1] = '\0';
            rl_ui_text(530, 326, sPlayLabels[8], sPlayWhite);
            break;
        case RL_PLAY_SUMMARY:
            rl_ui_text(44, 40, gRlRun.phase == RL_WON ? "RUN COMPLETE" : "RUN ENDED",
                       sPlayGold);
            rl_ui_text(44, 78, "Three-encounter W7 slice; no boss or other node types.", sPlayWhite);
            rl_ui_list_draw(&sPlayList, 44, 116, 540);
            rl_ui_text(44, 330, "Battles", sPlayWhite);
            rl_play_number(sPlayLabels[8], gRlRun.stats.battles, 3);
            sPlayLabels[8][3] = '\0';
            rl_ui_text(142, 330, sPlayLabels[8], sPlayWhite);
            rl_ui_text(220, 330, "Wins", sPlayWhite);
            rl_play_number(sPlayLabels[8], gRlRun.stats.wins, 3);
            sPlayLabels[8][3] = '\0';
            rl_ui_text(286, 330, sPlayLabels[8], sPlayWhite);
            rl_ui_text(44, 362, "Seed", sPlayWhite);
            rl_play_hex(sPlayLabels[8], gRlRun.seed);
            rl_ui_text(104, 362, sPlayLabels[8], sPlayWhite);
            break;
        case RL_PLAY_ERROR:
            rl_ui_text(44, 40, "Roguelike action unavailable", sPlayGold);
            rl_ui_text(44, 110, sPlayMessage == NULL ? "Press A or B to return" :
                       sPlayMessage, sPlayWhite);
            rl_ui_text(44, 400, "A/B: return", sPlayMuted);
            return;
        default:
            rl_ui_text(44, 40, "Battle in progress", sPlayGold);
            return;
    }
    rl_ui_text(44, 416, "D-pad move   A select   B back", sPlayMuted);
    rl_play_draw_status();
}

#endif
