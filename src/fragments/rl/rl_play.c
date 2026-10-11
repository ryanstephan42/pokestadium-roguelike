#ifdef ROGUELIKE
#include "global.h"
#include "src/math_util.h"
#include "src/roguelike/rl_battle.h"
#include "src/roguelike/rl_main.h"
#include "src/roguelike/rl_rosters.h"
#include "src/roguelike/rl_save.h"
#include "src/roguelike/rl_meta.h"
#include "src/roguelike/rl_content.h"
#include "src/roguelike/rl_text.h"
#include "rl_play.h"
#include "rl_ui.h"
#include "rl_scene_nodes.h"

#include "rl_gen/rl_rosters.h"

#define RL_PLAY_DRAFT_SIZE 6
#define RL_PLAY_REWARD_SIZE 4
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
    RL_PLAY_BATTLE,
    RL_PLAY_NODE,
    RL_PLAY_PROFILE,
    RL_PLAY_SETTINGS,
    RL_PLAY_META,
    RL_PLAY_REGISTER
};

static const Color_RGBA8 sPlayWhite = { 255, 255, 255, 255 };
static const Color_RGBA8 sPlayGold = { 255, 224, 96, 255 };
static const Color_RGBA8 sPlayMuted = { 160, 176, 192, 255 };
static RlUiList sPlayList;
static RlUiItem sPlayItems[153];
static char sPlayLabels[153][36];
static unsigned char sPlayMetaIds[153];
static RlMetaRentalLoadout sPlayCatalog[RL_PLAY_POOL_SIZE];
static RlMetaMoveVariant sPlayVariants[RL_PLAY_POOL_SIZE];
static RlU8 sPlaySpecies[RL_PLAY_POOL_SIZE];
static RlU16 sPlayBst[RL_PLAY_POOL_SIZE];
static RlU8 sPlayThemed[RL_PLAY_POOL_SIZE];
static unsigned char sPlayVariantIndex[RL_PLAY_DRAFT_SIZE];
static int sPlayNewAfterRegistration;
static RlMon sPlayPool[RL_PLAY_POOL_SIZE];
static RlPool sPlayRewardPool;
static RlPool sPlayNodePool;
RlMon gRlPlayDraft[RL_PLAY_DRAFT_SIZE];
#define sPlayDraft gRlPlayDraft
static unsigned char sPlayDraftSelected[RL_PLAY_DRAFT_SIZE];
static unsigned char sPlayDraftOldSpecies[RL_PLAY_DRAFT_SIZE];
static RlReward sPlayRewards[RL_PLAY_REWARD_SIZE];
static RlUiOrder sPlayActiveOrder;
#define sPlayActiveSlots sPlayActiveOrder.slots
#define sPlayActiveCount sPlayActiveOrder.count
static RunState sPlayPreviewRun;
RlMon gRlPlayPreviewTeam[RL_BATTLE_MAX_MONS];
#define sPlayPreviewTeam gRlPlayPreviewTeam
static RlEncounter sPlayPreviewEncounter;
int gRlPlayPreviewCount;
#define sPlayPreviewCount gRlPlayPreviewCount
static int sPlayPreviewLane;
static const char* sPlayPreviewError;
static unsigned char sPlayChosenLane;
static unsigned char sPlayRerollUsed;
static int sPlayPoolCount;
static int sPlayRewardCount;
static const char* sPlayMessage;
#define sPlayMeta gRlPlayProfile.meta
#define sPlayNextMeta gRlPlayProfile.next_meta
static RlMeta sPlayLoadedMeta;
static RunState sPlayLoadedRun;
#define sPlaySummaryPrepared gRlPlayProfile.summary_prepared
#define sPlaySummarySaved gRlPlayProfile.summary_saved
#define sPlayAscension gRlPlayProfile.ascension
#define sPlayStarterPool gRlPlayProfile.starter_pool
#define sPlayUnsaved gRlPlayProfile.unsaved
static RlSaveStatus sPlayProfileStatus;
static RlU8 sPlayMetaBytes[2][RL_META_MAX_SIZE];
static RlU8 sPlayRecordBytes[2][RL_SAVE_MAX_SIZE];

static unsigned rl_play_active_selected(unsigned slot);
static void rl_play_make_summary(void);
static RlSaveStatus rl_play_store(int include_run);
static void rl_play_settings(void);
static int rl_play_begin_new(void);
static void rl_play_preview_lane(void);
static void rl_play_meta_menu(void);
static void rl_play_registration(void);

static int rl_play_meta_equal(const RlMeta* a, const RlMeta* b) {
    unsigned first;
    unsigned second;
    if (rl_save_encode(a, NULL, sPlayMetaBytes[0], RL_META_MAX_SIZE, &first) != RL_OK ||
        rl_save_encode(b, NULL, sPlayMetaBytes[1], RL_META_MAX_SIZE, &second) != RL_OK)
        return 0;
    return first == second && bcmp(sPlayMetaBytes[0], sPlayMetaBytes[1], first) == 0;
}

static void rl_play_begin_node(unsigned mode) {
    sPlayNodePool.mons = sPlayPool;
    sPlayNodePool.count = (RlU16)sPlayPoolCount;
    sPlayNodePool.fixed_order = 0;
    rl_scene_nodes_begin(&gRlRun, &sPlayNodePool, mode, &sPlayMeta);
    gRlPlayScene = RL_PLAY_NODE;
    sPlayMessage = NULL;
}

static unsigned rl_play_route_rows(void) {
    return gRlPlayTestMode ? RL_PLAY_ROUTE_SIZE : rl_map_rows(gRlRun.act);
}

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
    rl_play_number(output + 9, mon->level, 3);
    output[12] = ' ';
    output[13] = 'H';
    output[14] = 'P';
    rl_play_number(output + 15, mon->cur_hp, 3);
    output[18] = '/';
    rl_play_number(output + 19, mon->max_hp, 3);
    output[22] = '\0';
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
    sPlayItems[0].text = "New run";
    sPlayItems[0].disabled = 0;
    sPlayItems[1].text = "Resume from Controller Pak";
    sPlayItems[1].disabled = 0;
    sPlayItems[2].text = "Return to Menu Select";
    sPlayItems[2].disabled = 0;
    sPlayItems[3].text = "Starter pool / Ascension";
    sPlayItems[3].disabled = 0;
    sPlayItems[4].text = "Permanent shop / registration";
    sPlayItems[4].disabled = 0;
    if (sPlaySummaryPrepared && !sPlaySummarySaved && rl_run_is_over(&gRlRun)) {
        sPlayItems[5].text = "Retry unsaved run results";
        sPlayItems[5].disabled = 0;
        rl_play_init_list("ROGUELIKE RUN", 6, 6);
    } else {
        rl_play_init_list("ROGUELIKE RUN", 5, 5);
    }
}

static void rl_play_settings(void) {
    static const char* pools[] = {"Poke Cup starters", "Prime Cup starters", "Little Cup starters"};
    unsigned i;
    for (i = 0; i < RL_META_POOL_COUNT; i++) {
        sPlayItems[i].text = pools[i];
        sPlayItems[i].disabled = !rl_meta_starter_unlocked(&sPlayMeta, i);
    }
    rl_play_copy_text(sPlayLabels[3], "Ascension 00");
    rl_play_number(sPlayLabels[3] + 10, sPlayAscension, 2);
    sPlayItems[3].text = sPlayLabels[3];
    sPlayItems[3].disabled = 0;
    sPlayItems[4].text = gRlPlayProfile.theme == RL_NO_SLOT ? "Theme: All registered" :
        rl_meta_theme_name(gRlPlayProfile.theme);
    sPlayItems[4].disabled = 0;
    sPlayItems[5].text = gRlPlayProfile.kit == RL_META_KIT_DEFAULT ?
        "Kit: 300 coins" : "Kit: 100 coins + 1 Potion";
    sPlayItems[5].disabled = 0;
    sPlayItems[6].text = "Back";
    sPlayItems[6].disabled = 0;
    rl_play_init_list("A: select / cycle settings", 7, 7);
    rl_play_set_scene(RL_PLAY_SETTINGS);
}

static int rl_play_record_equal(const RlMeta* a, const RunState* ar,
                                 const RlMeta* b, const RunState* br) {
    unsigned first, second;
    if (rl_save_encode(a, ar, sPlayRecordBytes[0], RL_SAVE_MAX_SIZE, &first) != RL_OK ||
        rl_save_encode(b, br, sPlayRecordBytes[1], RL_SAVE_MAX_SIZE, &second) != RL_OK)
        return 0;
    return first == second && bcmp(sPlayRecordBytes[0], sPlayRecordBytes[1], first) == 0;
}

/* Keep the exact candidate after an uncertain write, especially for skip. */
static RlSaveStatus rl_play_commit_edit(void) {
    RlPlayProfile* profile;
    RlU8 has_run;
    RlSaveStatus status;
    const RunState* saved_run;
    profile = &gRlPlayProfile;
    if (!profile->edit_pending) return RL_SAVE_INVALID;
    saved_run = profile->edit_has_run ? &profile->edit_run : NULL;
    status = rl_save_load(0, &sPlayLoadedMeta, &sPlayLoadedRun, &has_run);
    if (status != RL_SAVE_OK) return status;
    if (rl_play_record_equal(&sPlayLoadedMeta, has_run ? &sPlayLoadedRun : NULL,
                             &profile->edit_candidate, saved_run)) {
        sPlayMeta = profile->edit_candidate;
        profile->edit_pending = 0;
        return RL_SAVE_OK;
    }
    if (!rl_play_record_equal(&sPlayLoadedMeta, has_run ? &sPlayLoadedRun : NULL,
                              &profile->edit_base, saved_run))
        return RL_SAVE_DEVICE_CHANGED;
    status = rl_save_store(0, &profile->edit_candidate, saved_run, 0);
    if (status == RL_SAVE_OK) {
        sPlayMeta = profile->edit_candidate;
        profile->edit_pending = 0;
    }
    return status;
}

static RlSaveStatus rl_play_edit(unsigned action, unsigned id) {
    RlPlayProfile* profile;
    RlSaveStatus status;
    int result;
    profile = &gRlPlayProfile;
    if (profile->edit_pending) return rl_play_commit_edit();
    status = rl_save_load(0, &profile->edit_base, &profile->edit_run,
                           &profile->edit_has_run);
    if (status != RL_SAVE_OK) return status;
    if (!rl_play_meta_equal(&profile->edit_base, &sPlayMeta))
        return RL_SAVE_DEVICE_CHANGED;
    profile->edit_candidate = profile->edit_base;
    if (action == 0) result = rl_meta_purchase_option(&profile->edit_candidate, id);
    else if (action == 1) result = rl_meta_register_species(&profile->edit_candidate, id);
    else result = rl_meta_skip_species_registration(&profile->edit_candidate);
    if (result != RL_OK) return RL_SAVE_INVALID;
    profile->edit_pending = 1;
    return rl_play_commit_edit();
}

static void rl_play_meta_menu(void) {
    unsigned i, count;
    count = 0;
    if (gRlPlayProfile.edit_pending) {
        sPlayItems[0].text = "Retry pending profile save";
        sPlayItems[0].disabled = 0;
        sPlayMetaIds[0] = RL_NO_SLOT;
        count = 1;
    } else {
        sPlayItems[count].text = "Register earned species / skip";
        sPlayItems[count].disabled = !sPlayMeta.pending_registration_count;
        sPlayMetaIds[count++] = RL_NO_SLOT;
        for (i = 0; i < RL_META_OPTION_COUNT; i++) {
            unsigned length;
            if (!rl_meta_option_available(i)) continue;
            rl_play_copy_text(sPlayLabels[count], rl_meta_option_name(i));
            for (length = 0; sPlayLabels[count][length]; length++) {}
            sPlayLabels[count][length++] = ' ';
            if (rl_meta_option_unlocked(&sPlayMeta, i))
                rl_play_copy_text(sPlayLabels[count] + length, "(owned)");
            else {
                rl_play_number(sPlayLabels[count] + length, rl_meta_option_cost(i), 2);
                sPlayLabels[count][length + 2] = '\0';
            }
            sPlayItems[count].text = sPlayLabels[count];
            sPlayItems[count].disabled = rl_meta_option_unlocked(&sPlayMeta, i) ||
                sPlayMeta.currency < rl_meta_option_cost(i);
            sPlayMetaIds[count++] = (unsigned char)i;
        }
    }
    sPlayItems[count].text = "Back";
    sPlayItems[count].disabled = 0;
    rl_play_init_list("Permanent options - not run coins", count + 1, 7);
    rl_play_set_scene(RL_PLAY_META);
}

static void rl_play_registration(void) {
    unsigned species, count;
    count = 0;
    if (gRlPlayProfile.edit_pending) {
        rl_play_meta_menu();
        return;
    }
    for (species = 1; species <= 151; species++) {
        if (!(sPlayMeta.pending_species[(species - 1) >> 3] &
              (1U << ((species - 1) & 7)))) continue;
        sPlayMetaIds[count] = (unsigned char)species;
        rl_play_copy_text(sPlayLabels[count], rl_ui_species_name(species));
        sPlayItems[count].text = sPlayLabels[count];
        sPlayItems[count++].disabled = 0;
    }
    sPlayMetaIds[count] = 0;
    sPlayItems[count].text = "Skip one earned choice";
    sPlayItems[count++].disabled = !sPlayMeta.pending_registration_count;
    sPlayItems[count].text = "Back";
    sPlayItems[count].disabled = 0;
    rl_play_init_list("Register one species per earned receipt", count + 1, 7);
    rl_play_set_scene(RL_PLAY_REGISTER);
}

static int rl_play_load_species(void) {
    static const unsigned sets[] = {
        RL_RENTAL_SET_POKE, RL_RENTAL_SET_PRIME, RL_RENTAL_SET_PETIT
    };
    unsigned i, j, bst;
    int themed_count;
    sPlayPoolCount = rl_load_rental_pool(sets[sPlayStarterPool], sPlayPool,
                                         RL_PLAY_POOL_SIZE);
    if (sPlayPoolCount < 1) return 0;
    for (i = 0; i < (unsigned)sPlayPoolCount; i++) {
        sPlaySpecies[i] = sPlayPool[i].species;
        bst = 0;
        for (j = 0; j < 5; j++) bst += sPlayPool[i].base_stats[j];
        sPlayBst[i] = (RlU16)bst;
    }
    if (gRlPlayProfile.theme != RL_NO_SLOT) {
        themed_count = rl_meta_theme_species(&sPlayMeta, gRlPlayProfile.theme,
            sPlaySpecies, sPlayPoolCount, sPlayThemed, RL_PLAY_POOL_SIZE);
        if (themed_count < 1) return 0;
        for (i = 0; i < (unsigned)sPlayPoolCount; i++) {
            for (j = 0; j < (unsigned)themed_count; j++)
                if (sPlaySpecies[i] == sPlayThemed[j]) break;
            if (j == (unsigned)themed_count) sPlaySpecies[i] = 0;
        }
    }
    return 1;
}

static int rl_play_clean_starter(RlMon* mon) {
    unsigned i;
    mon->level = 30;
    mon->dvs = 0x9999;
    mon->status = 0;
    mon->fainted = 0;
    mon->held_relic = RL_NO_SLOT;
    mon->exp = 0;
    for (i = 0; i < 5; i++) mon->stat_exp[i] = 0;
    for (i = 0; i < 4; i++) {
        mon->max_pp[i] = mon->moves[i] ?
            D_80072B00[mon->moves[i] - 1].unk_05 : 0;
        mon->pp[i] = mon->max_pp[i];
    }
    return rl_prepare_mon(mon, 1) == 0;
}

static int rl_play_cycle_variant(unsigned slot) {
    unsigned option, i, count;
    int loaded, found;
    RlMon candidate;
    count = 0;
    /* Index zero always restores the canonical starter loadout. */
    for (i = 0; i < (unsigned)sPlayPoolCount; i++)
        if (sPlayPool[i].species == sPlayDraft[slot].species) break;
    if (i == (unsigned)sPlayPoolCount) return 0;
    candidate = sPlayPool[i];
    for (option = RL_META_MOVESET_1; option <= RL_META_MOVESET_4; option++) {
        if (!rl_meta_option_unlocked(&sPlayMeta, option)) continue;
        loaded = rl_load_meta_rental_catalog(option, sPlayCatalog, RL_PLAY_POOL_SIZE);
        if (loaded < 1) return 0;
        found = rl_meta_list_move_variants(&sPlayMeta, option, candidate.species,
            candidate.moves, sPlayCatalog, loaded, sPlayVariants + count,
            RL_PLAY_POOL_SIZE - count);
        if (found == RL_EMPTY) continue;
        if (found < 0) return 0;
        count += found;
    }
    sPlayVariantIndex[slot] = (unsigned char)((sPlayVariantIndex[slot] + 1) % (count + 1));
    if (sPlayVariantIndex[slot])
        for (i = 0; i < 4; i++)
            candidate.moves[i] = sPlayVariants[sPlayVariantIndex[slot] - 1].moves[i];
    if (!rl_play_clean_starter(&candidate)) return 0;
    sPlayDraft[slot] = candidate;
    return 1;
}

static void rl_play_request_new(void) {
    RlU8 has_run;
    if (gRlPlayProfile.edit_pending) {
        rl_play_meta_menu();
        sPlayMessage = "Resolve the pending profile save first";
        return;
    }
    if (sPlaySummaryPrepared && !sPlaySummarySaved) {
        sPlayMessage = "Retry unsaved results before starting another run";
        return;
    }
    sPlayProfileStatus = rl_save_load(0, &sPlayMeta, &sPlayLoadedRun, &has_run);
    if (sPlayProfileStatus == RL_SAVE_OK) {
        sPlayUnsaved = 0;
        if (sPlayMeta.pending_registration_count) {
            sPlayNewAfterRegistration = 1;
            rl_play_registration();
            return;
        }
        rl_play_begin_new();
        return;
    }
    if (sPlayProfileStatus != RL_SAVE_NOT_FOUND &&
        sPlayProfileStatus != RL_SAVE_NO_DEVICE) {
        sPlayMessage = rl_save_status_text(sPlayProfileStatus);
        gRlPlayScene = RL_PLAY_ERROR;
        return;
    }
    sPlayItems[0].text = "Create new progression on Pak";
    sPlayItems[0].disabled = sPlayProfileStatus != RL_SAVE_NOT_FOUND;
    sPlayItems[1].text = "Play without saving progression";
    sPlayItems[1].disabled = 0;
    sPlayItems[2].text = "Cancel";
    sPlayItems[2].disabled = 0;
    rl_play_init_list("No profile: choose explicitly", 3, 3);
    rl_play_set_scene(RL_PLAY_PROFILE);
    sPlayMessage = rl_save_status_text(sPlayProfileStatus);
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
        if (gRlPlayTestMode && avoid_previous && available > RL_PLAY_DRAFT_SIZE) {
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
        if (!gRlPlayTestMode) {
            RlU8 species[RL_PLAY_POOL_SIZE], chosen;
            RlU16 bst[RL_PLAY_POOL_SIZE];
            unsigned count;
            int result;
            count = 0;
            for (j = 0; j < available; j++) {
                unsigned index;
                index = indices[j];
                if (!sPlaySpecies[index] ||
                    !rl_meta_species_unlocked(&sPlayMeta, sPlaySpecies[index])) continue;
                species[count] = sPlaySpecies[index];
                bst[count++] = sPlayBst[index];
            }
            result = rl_meta_choose_species(&sPlayMeta, &gRlRun.rng, species, bst,
                                             count, &chosen);
            if (result != RL_OK) {
                sPlayMessage = "Registered theme needs six distinct species";
                gRlPlayScene = RL_PLAY_ERROR;
                return;
            }
            for (pick = 0; pick < available; pick++)
                if (sPlayPool[indices[pick]].species == chosen) break;
        } else {
            pick = rl_random_bounded(&gRlRun.rng, available);
        }
        sPlayDraft[i] = sPlayPool[indices[pick]];
        if (!gRlPlayTestMode && !rl_play_clean_starter(&sPlayDraft[i])) {
            sPlayMessage = "Could not construct canonical starter";
            gRlPlayScene = RL_PLAY_ERROR;
            return;
        }
        sPlayVariantIndex[i] = 0;
        gRlPlayDraftSpecies[i] = sPlayDraft[i].species;
        indices[pick] = indices[available - 1];
        available--;
        if (!gRlPlayTestMode) {
            for (j = 0; j < available; ) {
                if (sPlayPool[indices[j]].species == sPlayDraft[i].species)
                    indices[j] = indices[--available];
                else j++;
            }
        }
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
    static const unsigned sets[] = {
        RL_RENTAL_SET_POKE, RL_RENTAL_SET_PRIME, RL_RENTAL_SET_PETIT
    };
    unsigned i;
    unsigned seed;
    RlU8 ascension;

    seed = gRlPlayTestSeed;
    if (seed == 0) {
        seed = ((unsigned)MathUtil_Random16() << 16) | MathUtil_Random16();
    }
    if (rl_meta_select_ascension(&sPlayMeta, sPlayAscension, &ascension) != RL_OK ||
        !rl_meta_starter_unlocked(&sPlayMeta, sPlayStarterPool)) {
        sPlayMessage = "Selected run settings are locked";
        return 0;
    }
    rl_run_new(&gRlRun, seed, ascension);
    if (!gRlPlayTestMode &&
        rl_meta_apply_kit(&sPlayMeta, &gRlRun, gRlPlayProfile.kit) != RL_OK) {
        sPlayMessage = "Selected kit is unavailable";
        gRlPlayScene = RL_PLAY_ERROR;
        return 0;
    }
    sPlaySummaryPrepared = 0;
    sPlaySummarySaved = 0;
    sPlayRerollUsed = 0;
    sPlayChosenLane = 0;
    sPlayPoolCount = rl_load_rental_pool(sets[sPlayStarterPool], sPlayPool,
                                         RL_PLAY_POOL_SIZE);
    if (!gRlPlayTestMode && !rl_play_load_species()) {
        sPlayMessage = "Selected theme has no legal registered starters";
        gRlPlayScene = RL_PLAY_ERROR;
        return 0;
    }
    if (sPlayPoolCount < RL_PLAY_DRAFT_SIZE) {
        sPlayMessage = "Could not load the Poke Cup rental pool";
        gRlPlayScene = RL_PLAY_ERROR;
        return 0;
    }
    if (gRlPlayTestMode &&
        rl_map_prepare_battle_route(&gRlRun.map, RL_PLAY_ROUTE_SIZE) != RL_OK) {
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
    static const char* names[] = {
        "Trainer battle", "Elite trainer", "Pokemon Center",
        "Poke Mart", "Event", "Recruit", "Boss"
    };
    unsigned row;
    unsigned lanes;
    unsigned lane;

    row = gRlRun.row == RL_NO_SLOT ? 0 : (unsigned)gRlRun.row + 1;
    if (gRlRun.phase == RL_WON || gRlRun.phase == RL_LOST) {
        rl_play_make_summary();
        return;
    }
    if (row >= rl_play_route_rows() || gRlRun.phase != RL_MAP) {
        sPlayMessage = "Invalid map progression";
        gRlPlayScene = RL_PLAY_ERROR;
        return;
    }
    lanes = gRlRun.map.lanes[row];
    for (lane = 0; lane < lanes; lane++) {
        int reachable;
        unsigned type;
        reachable = gRlRun.row == RL_NO_SLOT ||
            (gRlRun.map.nodes[gRlRun.row][gRlRun.lane].edges & (1U << lane));
        type = gRlRun.map.nodes[row][lane].type;
        rl_play_copy_text(sPlayLabels[lane],
                          type <= RL_BOSS ? names[type] : "Invalid node");
        sPlayItems[lane].text = sPlayLabels[lane];
        sPlayItems[lane].disabled = !reachable;
    }
    sPlayItems[lanes].text = "Save run and return";
    sPlayItems[lanes].disabled = 0;
    sPlayItems[lanes + 1].text = "Bag - use items / teach TMs";
    sPlayItems[lanes + 1].disabled = 0;
    sPlayItems[lanes + 2].text = "Manage party";
    sPlayItems[lanes + 2].disabled = 0;
    rl_play_init_list("Choose the next node", (int)lanes + 3, 6);
    rl_play_set_scene(RL_PLAY_MAP);
    sPlayPreviewLane = -1;
    rl_play_preview_lane();
}

static void rl_play_preview_lane(void) {
    unsigned row;
    unsigned type;
    unsigned char ai;
    if (sPlayPreviewLane == sPlayList.cursor) return;
    sPlayPreviewLane = sPlayList.cursor;
    sPlayPreviewCount = 0;
    sPlayPreviewError = NULL;
    row = gRlRun.row == RL_NO_SLOT ? 0 : (unsigned)gRlRun.row + 1;
    if (sPlayList.cursor >= gRlRun.map.lanes[row]) return;
    type = gRlRun.map.nodes[row][sPlayList.cursor].type;
    if (type != RL_BATTLE && type != RL_ELITE && type != RL_BOSS) return;
    if (gRlPlayTestMode) {
        sPlayPreviewError = "Regression: random trainer at launch";
        return;
    }
    sPlayPreviewRun = gRlRun;
    sPlayPreviewRun.row = (RlU8)row;
    sPlayPreviewRun.lane = (RlU8)sPlayList.cursor;
    sPlayPreviewRun.phase = RL_NODE;
    if (rl_encounter_select(&sPlayPreviewRun, &sPlayPreviewEncounter) != RL_OK) {
        sPlayPreviewError = "Encounter descriptor unavailable";
        return;
    }
    sPlayPreviewCount = rl_load_encounter(&sPlayPreviewRun, sPlayPreviewTeam,
                                         RL_BATTLE_MAX_MONS, &ai);
    if (sPlayPreviewCount < 1) {
        sPlayPreviewError = "Encounter roster unavailable";
        sPlayPreviewCount = 0;
    }
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
        if (rl_play_active_selected(i))
            sPlayLabels[i][0] = (char)('0' + rl_play_active_selected(i));
        sPlayItems[i].text = sPlayLabels[i];
        sPlayItems[i].disabled = gRlRun.party[i].fainted ||
            gRlRun.party[i].cur_hp == 0;
    }
    sPlayItems[gRlRun.party_count].text = "Confirm active team";
    sPlayItems[gRlRun.party_count].disabled = sPlayActiveCount != required ||
        sPlayActiveCount == 0;
    result = rl_play_init_list("Order team: 1 is the lead",
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
    int index;
    index = rl_ui_order_index(&sPlayActiveOrder, slot);
    return index >= 0 ? (unsigned)index + 1 : 0;
}

static int rl_play_active_toggle(unsigned slot) {
    return rl_ui_order_toggle(&sPlayActiveOrder, slot,
                              rl_play_active_required()) == RL_UI_NONE;
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
    sPlayRewardCount = (int)rl_rewards_generate_unlocked(&gRlRun, &sPlayRewardPool, NULL,
                                                rl_scene_available_relics(
                                                    rl_meta_permitted_relics(&sPlayMeta)),
                                                sPlayRewards,
                                                RL_PLAY_REWARD_SIZE);
    if (sPlayRewardCount != (int)rl_relic_reward_count(gRlRun.relics)) {
        sPlayMessage = "Reward generation failed";
        gRlPlayScene = RL_PLAY_ERROR;
        return;
    }
    for (i = 0; i < (unsigned)sPlayRewardCount; i++) {
        if (sPlayRewards[i].kind == RL_REWARD_RECRUIT) {
            char* label;
            label = sPlayLabels[i];
            rl_play_copy_text(label, "Trainer No.");
            rl_play_number(label + 11, sPlayRewards[i].mon.species, 3);
            label[14] = ' ';
            label[15] = 'L';
            rl_play_number(label + 16, sPlayRewards[i].mon.level, 3);
            label[19] = '\0';
            sPlayItems[i].text = label;
        } else if (sPlayRewards[i].kind == RL_REWARD_RELIC &&
                   sPlayRewards[i].detail < RL_RELIC_COUNT) {
            sPlayItems[i].text = rl_relic_defs[sPlayRewards[i].detail].name;
        } else {
            sPlayItems[i].text = rl_play_reward_label(&sPlayRewards[i]);
        }
        sPlayItems[i].disabled = rl_play_reward_disabled(i);
    }
    rl_play_init_list("Choose one reward", sPlayRewardCount, 3);
    rl_play_set_scene(RL_PLAY_REWARD);
}

static void rl_play_make_summary(void) {
    RlSaveStatus status;
    sPlayItems[0].text = sPlaySummarySaved ? "Results saved" : "Save results (replaces Pak run)";
    sPlayItems[0].disabled = 0;
    sPlayItems[1].text = "Return (unsaved results retained)";
    sPlayItems[1].disabled = 0;
    sPlayItems[2].text = "Permanent shop / registration";
    sPlayItems[2].disabled = !sPlaySummarySaved;
    rl_play_init_list("Run summary", 3, 3);
    rl_play_set_scene(RL_PLAY_SUMMARY);
    if (!sPlaySummarySaved && !sPlayUnsaved &&
        gRlPlayTestMode != 2 &&
        !(gRlPlayTestMode && gRlRun.phase == RL_WON)) {
        status = rl_play_store(0);
        if (status == RL_SAVE_OK) {
            sPlayItems[0].text = "Results saved";
            sPlayItems[2].disabled = 0;
            sPlayMessage = "Progression saved to Controller Pak";
        } else {
            sPlayMessage = rl_save_status_text(status);
        }
    }
}

static void rl_play_after_reward(void) {
    unsigned row;
    row = gRlRun.row;
    if (rl_run_finish_node(&gRlRun) != RL_OK) {
        sPlayMessage = "Could not finish the selected route";
        gRlPlayScene = RL_PLAY_ERROR;
        return;
    }
    if (gRlPlayTestMode && row + 1 >= RL_PLAY_ROUTE_SIZE) {
        gRlRun.phase = RL_WON;
        rl_play_make_summary();
    } else if (rl_run_is_over(&gRlRun)) {
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
    if (gRlPlayTestMode) {
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
    } else {
        enemy_count = rl_load_encounter(&gRlRun, enemy,
                                        RL_BATTLE_MAX_MONS, &ai);
    }
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
    RlU8 has_run;
    RlSaveStatus status;
    if (sPlayUnsaved) return RL_SAVE_NO_DEVICE;
    if (!include_run && sPlaySummarySaved) return RL_SAVE_OK;
    if (!include_run && !sPlaySummaryPrepared) {
        if (gRlPlayTestMode == 2 || (gRlPlayTestMode && gRlRun.phase == RL_WON))
            return RL_SAVE_INVALID;
        sPlayNextMeta = sPlayMeta;
        if (rl_meta_record_run(&sPlayNextMeta, &gRlRun) != RL_OK)
            return RL_SAVE_INVALID;
        sPlaySummaryPrepared = 1;
    }
    status = rl_save_load(0, &sPlayLoadedMeta, &sPlayLoadedRun, &has_run);
    if (status != RL_SAVE_OK) return status;
    if (!include_run && !has_run &&
        rl_play_meta_equal(&sPlayLoadedMeta, &sPlayNextMeta)) {
        sPlaySummarySaved = 1;
        sPlayMeta = sPlayNextMeta;
        return RL_SAVE_OK;
    }
    if (!rl_play_meta_equal(&sPlayLoadedMeta, &sPlayMeta))
        return RL_SAVE_DEVICE_CHANGED;
    status = rl_save_store(0, include_run ? &sPlayMeta : &sPlayNextMeta,
                            include_run ? &gRlRun : NULL, 0);
    if (status == RL_SAVE_OK && !include_run) {
        sPlayMeta = sPlayNextMeta;
        sPlaySummarySaved = 1;
    }
    return status;
}

static void rl_play_resume(void) {
    RlMeta meta;
    RunState run;
    RlU8 has_run;
    RlSaveStatus status;
    RlU8 ascension;

    status = rl_save_load(0, &meta, &run, &has_run);
    if (status != RL_SAVE_OK && status != RL_SAVE_RECOVERED) {
        sPlayMessage = rl_save_status_text(status);
        gRlPlayScene = RL_PLAY_ERROR;
        return;
    }
    if (!has_run || (run.phase != RL_MAP && run.phase != RL_NODE) ||
        run.party_count == 0) {
        sPlayMessage = "Controller Pak has no resumable run";
        gRlPlayScene = RL_PLAY_ERROR;
        return;
    }
    if (rl_meta_select_ascension(&meta, run.ascension, &ascension) != RL_OK) {
        sPlayMessage = "Saved run ascension is not unlocked";
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
    sPlayMeta = meta;
    sPlayUnsaved = 0;
    sPlayAscension = run.ascension;
    sPlaySummaryPrepared = 0;
    sPlaySummarySaved = 0;
    gRlPlayBattleCount = run.stats.battles;
    if (status == RL_SAVE_RECOVERED) {
        sPlayMessage = rl_save_status_text(status);
    }
    if (run.phase == RL_NODE) {
        unsigned type;
        type = run.map.nodes[run.row][run.lane].type;
        if (type != RL_CENTER && type != RL_MART &&
            type != RL_EVENT && type != RL_RECRUIT) {
            sPlayMessage = "This battle cannot be resumed between commands";
            gRlPlayScene = RL_PLAY_ERROR;
            return;
        }
        rl_play_begin_node(type);
    } else {
        rl_play_make_map();
    }
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
    sPlayPoolCount = rl_load_rental_pool(RL_RENTAL_SET_POKE, sPlayPool,
                                         RL_PLAY_POOL_SIZE);
    if (sPlayPoolCount < RL_PLAY_DRAFT_SIZE) {
        sPlayMessage = "Could not load node rental content";
        gRlPlayScene = RL_PLAY_ERROR;
        return;
    }
    rl_play_make_map();
}

static void rl_play_update_root(u16 pressed) {
    int result;
    result = rl_ui_list(&sPlayList, pressed);
    if (result == 0) {
        rl_play_request_new();
    } else if (result == 1) {
        if (gRlPlayProfile.edit_pending) {
            rl_play_meta_menu();
            sPlayMessage = "Resolve the pending profile save before resuming";
        } else rl_play_resume();
    } else if (result == 2 || result == RL_UI_CANCEL) {
        gRlMain.returnState = STATE_MENU_SELECT;
        gRlPlayScene = RL_PLAY_MENU;
        sPlayMessage = NULL;
        gRlPlayExit = 1;
    } else if (result == 3 || result == 4) {
        RlU8 has_run;
        RlSaveStatus status;
        if (sPlaySummaryPrepared && !sPlaySummarySaved) {
            sPlayMessage = "Retry unsaved results before editing progression";
            return;
        }
        if (gRlPlayProfile.edit_pending) {
            rl_play_meta_menu();
            return;
        }
        status = rl_save_load(0, &sPlayMeta, &sPlayLoadedRun, &has_run);
        if (status == RL_SAVE_OK) {
            if (result == 3) rl_play_settings();
            else {
                sPlayNewAfterRegistration = 0;
                rl_play_meta_menu();
            }
        } else {
            sPlayMessage = rl_save_status_text(status);
        }
    } else if (result == 5 && sPlaySummaryPrepared && !sPlaySummarySaved) {
        rl_play_make_summary();
    }
}

static void rl_play_update_draft(u16 pressed) {
    int result;
    if (!gRlPlayTestMode && (pressed & (RL_UI_LEFT | RL_UI_RIGHT))) {
        if (sPlayList.cursor < RL_PLAY_DRAFT_SIZE) {
            if (!rl_play_cycle_variant(sPlayList.cursor))
                sPlayMessage = "Compatible loadouts unavailable";
            else sPlayMessage = "Canonical loadout selected; no stat or PP-Up bonuses";
        }
        pressed &= ~(RL_UI_LEFT | RL_UI_RIGHT);
    }
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
    rl_play_preview_lane();
    row = gRlRun.row == RL_NO_SLOT ? 0 : (unsigned)gRlRun.row + 1;
    lane_count = gRlRun.map.lanes[row];
    if (result >= 0 && (unsigned)result < lane_count) {
        unsigned type;
        sPlayChosenLane = (unsigned char)result;
        type = gRlRun.map.nodes[row][result].type;
        if (type == RL_BATTLE || type == RL_ELITE || type == RL_BOSS) {
            rl_play_make_party();
        } else if (type == RL_CENTER || type == RL_MART ||
                   type == RL_EVENT || type == RL_RECRUIT) {
            if (rl_run_advance(&gRlRun, sPlayChosenLane) != RL_OK) {
                sPlayMessage = "That node is no longer reachable";
                return;
            }
            rl_play_begin_node(type);
        } else {
            sPlayMessage = "Unsupported map node";
        }
    } else if (result == (int)lane_count) {
        RlSaveStatus status;
        status = rl_play_store(1);
        if (status == RL_SAVE_OK) {
            gRlMain.returnState = STATE_MENU_SELECT;
            gRlPlayExit = 1;
        } else {
            sPlayMessage = rl_save_status_text(status);
        }
    } else if (result == (int)lane_count + 1) {
        rl_play_begin_node(RL_SCENE_BAG);
    } else if (result == (int)lane_count + 2) {
        rl_play_begin_node(RL_SCENE_PARTY);
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
    if (pressed & (RL_UI_LEFT | RL_UI_RIGHT)) {
        if (sPlayList.cursor < gRlRun.party_count &&
            rl_ui_order_move(&sPlayActiveOrder, (unsigned)sPlayList.cursor,
                             pressed & RL_UI_LEFT ? -1 : 1) == RL_UI_NONE) {
            sPlayMessage = "Battle order updated; 1 is the lead";
        } else {
            sPlayMessage = "Select an entrant; LEFT earlier, RIGHT later";
        }
        pressed &= ~(RL_UI_LEFT | RL_UI_RIGHT);
    }
    for (i = 0; i < gRlRun.party_count; i++) {
        rl_play_mon_label(sPlayLabels[i], &gRlRun.party[i],
                          rl_play_active_selected(i));
        if (rl_play_active_selected(i))
            sPlayLabels[i][0] = (char)('0' + rl_play_active_selected(i));
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
        } else {
            sPlayMessage = "A removes/adds; additions go last";
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
    } else if (result == 2 && sPlaySummarySaved) {
        sPlayNewAfterRegistration = 0;
        rl_play_meta_menu();
    }
}

static void rl_play_update_meta(u16 pressed) {
    int result;
    RlSaveStatus status;
    unsigned scene;
    scene = gRlPlayScene;
    result = rl_ui_list(&sPlayList, pressed);
    if (result == RL_UI_CANCEL || result == sPlayList.count - 1) {
        sPlayNewAfterRegistration = 0;
        rl_play_build_root();
        rl_play_set_scene(RL_PLAY_MENU);
        return;
    }
    if (result < 0) return;
    if (scene == RL_PLAY_META && !gRlPlayProfile.edit_pending && result == 0) {
        rl_play_registration();
        return;
    }
    if (gRlPlayProfile.edit_pending) status = rl_play_commit_edit();
    else if (scene == RL_PLAY_REGISTER)
        status = rl_play_edit(sPlayMetaIds[result] ? 1 : 2, sPlayMetaIds[result]);
    else status = rl_play_edit(0, sPlayMetaIds[result]);
    if (status != RL_SAVE_OK) {
        rl_play_meta_menu();
        sPlayMessage = rl_save_status_text(status);
        return;
    }
    if (sPlayNewAfterRegistration && !sPlayMeta.pending_registration_count) {
        sPlayNewAfterRegistration = 0;
        rl_play_begin_new();
    } else if (scene == RL_PLAY_REGISTER || sPlayNewAfterRegistration) {
        rl_play_registration();
        sPlayMessage = "Choice saved; remaining receipts are shown above";
    } else {
        rl_play_meta_menu();
        sPlayMessage = "Profile saved; suspension preserved";
    }
}

s32 rl_play_init(void) {
    s32 result;
    gRlPlayExit = 0;
    if (!gRlPlayProfile.options_initialized) {
        gRlPlayProfile.theme = RL_NO_SLOT;
        gRlPlayProfile.kit = RL_META_KIT_DEFAULT;
        gRlPlayProfile.options_initialized = 1;
    }
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
    if (!gRlPlayProfile.edit_pending &&
        !(sPlaySummaryPrepared && !sPlaySummarySaved)) {
        RlU8 has_run;
        RlSaveStatus status;
        status = rl_save_load(0, &sPlayLoadedMeta, &sPlayLoadedRun, &has_run);
        if (status == RL_SAVE_OK) sPlayMeta = sPlayLoadedMeta;
        else sPlayMessage = rl_save_status_text(status);
    }
    rl_play_build_root();
    return RL_UI_NONE;
}

s32 rl_play_update(u16 pressed) {
    if (gRlPlayExit) {
        return RL_UI_CANCEL;
    }
    if (gRlPlayScene == RL_PLAY_NODE && (pressed & CONT_START)) {
        RlSaveStatus status;
        status = rl_play_store(1);
        if (status == RL_SAVE_OK) {
            gRlMain.returnState = STATE_MENU_SELECT;
            gRlPlayExit = 1;
            return RL_UI_CANCEL;
        }
        sPlayMessage = rl_save_status_text(status);
        return RL_UI_NONE;
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
        case RL_PLAY_META:
        case RL_PLAY_REGISTER:
            rl_play_update_meta(pressed);
            break;
        case RL_PLAY_NODE:
            {
                int result;
                result = rl_scene_nodes_update(pressed);
                if (result == RL_SCENE_COMPLETED) {
                    rl_play_after_reward();
                } else if (result == RL_SCENE_CANCEL) {
                    rl_play_make_map();
                }
            }
            break;
        case RL_PLAY_PROFILE:
            {
                int result;
                RlSaveStatus status;
                result = rl_ui_list(&sPlayList, pressed);
                if (result == 0 || result == 1) {
                    rl_meta_init(&sPlayMeta);
                    sPlayAscension = 0;
                    sPlayStarterPool = 0;
                    gRlPlayProfile.theme = RL_NO_SLOT;
                    gRlPlayProfile.kit = RL_META_KIT_DEFAULT;
                    sPlayUnsaved = result == 1;
                    if (!sPlayUnsaved) {
                        status = rl_save_store(0, &sPlayMeta, NULL, 1);
                        if (status != RL_SAVE_OK) {
                            sPlayMessage = rl_save_status_text(status);
                            break;
                        }
                    }
                    rl_play_begin_new();
                } else if (result == 2 || result == RL_UI_CANCEL) {
                    rl_play_build_root();
                    rl_play_set_scene(RL_PLAY_MENU);
                }
            }
            break;
        case RL_PLAY_SETTINGS:
            {
                int result;
                RlU8 selected;
                result = rl_ui_list(&sPlayList, pressed);
                if (result >= 0 && result < RL_META_POOL_COUNT) {
                    sPlayStarterPool = (unsigned char)result;
                    sPlayMessage = "Starter pool selected";
                } else if (result == 3) {
                    unsigned requested;
                    requested = sPlayAscension < sPlayMeta.ascension ? sPlayAscension + 1 : 0;
                    if (rl_meta_select_ascension(&sPlayMeta, requested, &selected) != RL_OK) {
                        sPlayMessage = "Ascension is locked";
                    } else {
                        sPlayAscension = selected;
                        rl_play_number(sPlayLabels[3] + 10, selected, 2);
                    }
                } else if (result == 4) {
                    unsigned attempt, theme, count, i;
                    theme = gRlPlayProfile.theme;
                    for (attempt = 0; attempt <= RL_META_THEME_COUNT; attempt++) {
                        theme = theme == RL_NO_SLOT ? 0 :
                            theme + 1 == RL_META_THEME_COUNT ? RL_NO_SLOT : theme + 1;
                        if (theme == RL_NO_SLOT) break;
                        if (!rl_meta_option_unlocked(&sPlayMeta, rl_meta_theme_option(theme)))
                            continue;
                        gRlPlayProfile.theme = (RlU8)theme;
                        if (!rl_play_load_species()) continue;
                        count = 0;
                        for (i = 0; i < (unsigned)sPlayPoolCount; i++)
                            if (sPlaySpecies[i]) count++;
                        if (count >= RL_PLAY_DRAFT_SIZE) break;
                    }
                    gRlPlayProfile.theme = (RlU8)theme;
                    rl_play_settings();
                } else if (result == 5) {
                    gRlPlayProfile.kit = gRlPlayProfile.kit == RL_META_KIT_DEFAULT &&
                        rl_meta_option_unlocked(&sPlayMeta, RL_META_KIT_POTION) ?
                        RL_META_KIT_POTION : RL_META_KIT_DEFAULT;
                    rl_play_settings();
                } else if (result == 6 || result == RL_UI_CANCEL) {
                    rl_play_build_root();
                    rl_play_set_scene(RL_PLAY_MENU);
                }
            }
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
    unsigned i;
    char label[24];
    if (mon == NULL || mon->species == 0) {
        return;
    }
    rl_ui_mon_icon(mon->species, x + 150, y);
    rl_ui_text(x, y, rl_ui_species_name(mon->species), sPlayGold);
    rl_play_copy_text(label, "Level 000");
    rl_play_number(label + 6, mon->level, 3);
    rl_ui_text(x, y + 24, label, sPlayWhite);
    rl_play_copy_text(label, "HP 000/000");
    rl_play_number(label + 3, mon->cur_hp, 3);
    rl_play_number(label + 7, mon->max_hp, 3);
    rl_ui_text(x, y + 44, label, sPlayWhite);
    rl_ui_hp_bar(x, y + 68, 190, mon->cur_hp, mon->max_hp);
    for (i = 0; i < 4; i++) {
        rl_ui_text(x, y + 92 + i * 34, rl_ui_move_name(mon->moves[i]), sPlayWhite);
        rl_play_copy_text(label, "PP 00/00");
        rl_play_number(label + 3, mon->pp[i], 2);
        rl_play_number(label + 6, mon->max_pp[i], 2);
        rl_ui_text(x + 92, y + 108 + i * 34, label, sPlayMuted);
    }
}

static void rl_play_draw_preview(void) {
    static const char* roster_names[RL_ROSTER_COUNT] = {
        "Poke / Poke", "Poke / Great", "Poke / Ultra", "Poke / Master",
        "Prime / Poke", "Prime / Great", "Prime / Ultra", "Prime / Master",
        "Brock", "Misty", "Lt. Surge", "Erika", "Koga", "Sabrina",
        "Blaine", "Giovanni", "Elite Four", "Rival", "Mewtwo"
    };
    static const char* node_help[] = {
        "", "", "Heal living mons by 50% or revive one fainted mon.",
        "Buy healing, Candy, vitamins, a TM or an unlocked relic.",
        "A deterministic unlocked event. Choices shown before use.",
        "Choose one of three rental candidates or leave.", ""
    };
    unsigned i;
    unsigned row;
    unsigned type;
    char label[36];
    row = gRlRun.row == RL_NO_SLOT ? 0 : (unsigned)gRlRun.row + 1;
    if (sPlayList.cursor >= gRlRun.map.lanes[row]) {
        rl_ui_text_wrap(414, 96, 190, 9,
            "Bag: use items and TMs.\nParty: reorder or release.\nSave: suspend on Pak.", sPlayWhite);
        return;
    }
    type = gRlRun.map.nodes[row][sPlayList.cursor].type;
    if (sPlayPreviewError) {
        rl_ui_text_wrap(414, 96, 190, 5, sPlayPreviewError, sPlayGold);
        return;
    }
    if (!sPlayPreviewCount) {
        if (type <= RL_BOSS)
            rl_ui_text_wrap(414, 96, 190, 7, node_help[type], sPlayWhite);
        return;
    }
    rl_play_copy_text(label, roster_names[sPlayPreviewEncounter.roster]);
    rl_ui_text(414, 78, label, sPlayGold);
    rl_play_copy_text(label, "Trainer 0 / Round 0");
    rl_play_number(label + 8, sPlayPreviewEncounter.trainer + 1, 1);
    rl_play_number(label + 18, sPlayPreviewEncounter.round, 1);
    rl_ui_text(414, 54, label, sPlayMuted);
    for (i = 0; i < (unsigned)sPlayPreviewCount; i++) {
        rl_play_copy_text(label, rl_ui_species_name(sPlayPreviewTeam[i].species));
        rl_ui_text(414, 108 + i * 24, label, sPlayWhite);
        label[0] = 'L';
        rl_play_number(label + 1, sPlayPreviewTeam[i].level, 3);
        label[4] = '\0';
        rl_ui_text(556, 108 + i * 24, label, sPlayMuted);
    }
    rl_play_copy_text(label, "Win coins: 000000");
    rl_play_number(label + 11, rl_battle_money(gRlRun.act, row, type,
                                              gRlRun.ascension, gRlRun.relics), 6);
    rl_ui_text(414, 262, label, sPlayGold);
    rl_ui_text_wrap(414, 292, 190, 5,
        "Win: XP + coins.\nPossible rewards:\nRoster recruit,\nCandy / vitamin,\nrelic / money.", sPlayWhite);
    rl_ui_text_wrap(414, 390, 190, 1, "Not guaranteed.", sPlayMuted);
    rl_ui_text_wrap(44, 376, 360, 1,
        "Enemy: first 3 fight; roster can recruit.", sPlayMuted);
}

static void rl_play_draw_status(void) {
    if (sPlayMessage != NULL) {
        rl_ui_text_wrap(42, 394, 556, 1, sPlayMessage, sPlayGold);
    }
}

static void rl_play_draw_currency(s16 y) {
    char label[36];
    const RlMeta* meta;
    meta = gRlPlayScene == RL_PLAY_SUMMARY && sPlaySummaryPrepared &&
        !sPlaySummarySaved ? &sPlayNextMeta : &sPlayMeta;
    rl_play_copy_text(label, "Permanent coins: 0000000000");
    rl_play_number(label + 17, meta->currency, 10);
    rl_ui_text(44, y, label, sPlayGold);
    rl_play_copy_text(label, "Earned choices remaining: 000");
    rl_play_number(label + 25, meta->pending_registration_count, 3);
    rl_ui_text(44, y + 24, label, sPlayWhite);
}

void rl_play_draw(void) {
    unsigned row;
    unsigned i;

    rl_ui_window(24, 24, 592, 432);
    switch (gRlPlayScene) {
        case RL_PLAY_MENU:
            rl_ui_text(44, 40, "Pokemon Stadium Roguelike", sPlayGold);
            rl_ui_list_draw(&sPlayList, 44, 84, 540);
            rl_play_draw_currency(342);
            break;
        case RL_PLAY_PROFILE:
        case RL_PLAY_SETTINGS:
            rl_ui_text(44, 40, "Persistent progression settings", sPlayGold);
            rl_ui_list_draw(&sPlayList, 44, 84, 540);
            rl_ui_text_wrap(44, 350, 540, 2,
                "Themes filter registered species. Prices and weights provisional.\n"
                "Potion Cache exchanges 200 starting coins for 1 Potion.", sPlayMuted);
            break;
        case RL_PLAY_META:
        case RL_PLAY_REGISTER:
            rl_ui_text(44, 40, gRlPlayScene == RL_PLAY_META ?
                "Permanent shop" : "Earned species registration", sPlayGold);
            rl_ui_list_draw(&sPlayList, 44, 84, 360);
            rl_play_draw_currency(324);
            if (gRlPlayScene == RL_PLAY_REGISTER) {
                rl_ui_text_wrap(414, 98, 190, 9,
                    "At most one receipt per eligible finished run.\n"
                    "Register one species or skip one receipt.\n"
                    "Resolve all before New Run.", sPlayWhite);
            } else if (!gRlPlayProfile.edit_pending && sPlayList.cursor > 0 &&
                       sPlayList.cursor < sPlayList.count - 1) {
                unsigned option;
                option = sPlayMetaIds[sPlayList.cursor];
                rl_ui_text_wrap(414, 98, 190, 9,
                    option <= RL_META_MOVESET_4 ?
                    "Matching species only.\nCanonical rental moves.\n"
                    "Draft LEFT/RIGHT cycles compatible loadouts.\nNo stat or PP-Up bonuses." :
                    option <= RL_META_THEME_4 ?
                    "Filter registered starter species by theme.\n"
                    "Choose in settings.\nNo permanent stats." :
                    "Starting kit:\n100 coins + 1 Potion instead of 300 coins.\n"
                    "Same 300-coin budget.\nChoose in settings.", sPlayWhite);
            }
            rl_ui_text_wrap(414, 304, 190, 3, "Prices / weights provisional.", sPlayMuted);
            break;
        case RL_PLAY_DRAFT:
            rl_ui_text(44, 40, "Starter draft - select 3 of 6", sPlayGold);
            rl_ui_list_draw(&sPlayList, 44, 80, 360);
            if (sPlayList.cursor < RL_PLAY_DRAFT_SIZE) {
                rl_play_draw_mon_detail(&sPlayDraft[sPlayList.cursor], 410, 94);
            }
            rl_ui_text(44, 374, "Selected: ", sPlayWhite);
            rl_play_number(sPlayLabels[8], rl_play_count_selected(), 1);
            sPlayLabels[8][1] = '\0';
            rl_ui_text(142, 374, sPlayLabels[8], sPlayWhite);
            rl_ui_text(44, 350, "LEFT/RIGHT: cycle owned legal loadouts", sPlayMuted);
            break;
        case RL_PLAY_MAP:
            rl_ui_text(44, 40, "Act map - choose a connected node", sPlayGold);
            rl_ui_list_draw(&sPlayList, 44, 84, 360);
            row = gRlRun.row == RL_NO_SLOT ? 0 : (unsigned)gRlRun.row + 1;
            rl_ui_text(44, 316, "Act", sPlayWhite);
            rl_play_number(sPlayLabels[8], gRlRun.act + 1, 1);
            sPlayLabels[8][1] = '\0';
            rl_ui_text(100, 316, sPlayLabels[8], sPlayWhite);
            rl_ui_text(144, 316, "Node", sPlayWhite);
            rl_play_number(sPlayLabels[8], row + 1, 1);
            sPlayLabels[8][1] = '\0';
            rl_ui_text(204, 316, sPlayLabels[8], sPlayWhite);
            rl_ui_text(44, 350, "Seed", sPlayWhite);
            rl_play_hex(sPlayLabels[8], gRlRun.seed);
            rl_ui_text(104, 350, sPlayLabels[8], sPlayWhite);
            rl_play_draw_preview();
            break;
        case RL_PLAY_PARTY:
            rl_ui_text(44, 40, "Select the active team", sPlayGold);
            rl_ui_list_draw(&sPlayList, 44, 80, 360);
            if (sPlayList.cursor < gRlRun.party_count) {
                rl_play_draw_mon_detail(&gRlRun.party[sPlayList.cursor], 410, 80);
            }
            for (i = 0; i < sPlayActiveCount; i++) {
                rl_play_number(sPlayLabels[8], i + 1, 1);
                sPlayLabels[8][1] = ' ';
                rl_play_copy_text(sPlayLabels[8] + 2,
                    rl_ui_species_name(gRlRun.party[sPlayActiveSlots[i]].species));
                rl_ui_text(414, 328 + i * 22, sPlayLabels[8], i ? sPlayWhite : sPlayGold);
            }
            break;
        case RL_PLAY_REWARD:
            rl_ui_text(44, 40, "Victory - choose one reward", sPlayGold);
            rl_ui_list_draw(&sPlayList, 44, 84, 360);
            if (sPlayList.cursor < (unsigned)sPlayRewardCount &&
                sPlayRewards[sPlayList.cursor].kind == RL_REWARD_RECRUIT) {
                rl_play_draw_mon_detail(&sPlayRewards[sPlayList.cursor].mon, 410, 90);
            } else if (sPlayList.cursor < (unsigned)sPlayRewardCount &&
                       sPlayRewards[sPlayList.cursor].kind == RL_REWARD_RELIC) {
                unsigned relic;
                relic = sPlayRewards[sPlayList.cursor].detail;
                if (relic < RL_RELIC_COUNT) {
                    rl_ui_text_wrap(414, 100, 190, 9, rl_relic_defs[relic].effect, sPlayWhite);
                    rl_ui_text_wrap(414, 300, 190, 4,
                        rl_scene_relic_status(&gRlRun, relic), sPlayGold);
                }
            }
            rl_ui_text(44, 326, "Party slots", sPlayWhite);
            rl_play_number(sPlayLabels[8], gRlRun.party_count, 1);
            sPlayLabels[8][1] = '\0';
            rl_ui_text(200, 326, sPlayLabels[8], sPlayWhite);
            rl_ui_text_wrap(44, 356, 360, 2,
                "Amber / Master Ball unavailable:\nrecruit actions not implemented.", sPlayMuted);
            break;
        case RL_PLAY_SUMMARY:
            rl_ui_text(44, 40, gRlRun.phase == RL_WON ? "RUN COMPLETE" : "RUN ENDED",
                       sPlayGold);
            rl_ui_text(44, 78, gRlPlayTestMode ? "Three-encounter regression route" :
                       "Three Acts and the Mewtwo finale", sPlayWhite);
            rl_ui_list_draw(&sPlayList, 44, 116, 540);
            rl_play_draw_currency(266);
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
        case RL_PLAY_NODE:
            rl_scene_nodes_draw();
            rl_ui_text(44, 416, "START: suspend to Controller Pak", sPlayMuted);
            rl_play_draw_status();
            return;
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
    rl_ui_text_wrap(44, 416, 552, 1, gRlPlayScene == RL_PLAY_PARTY ?
        "UP/DOWN: move  A: toggle/confirm  LEFT/RIGHT: order  B: back" :
        "D-pad move   A select   B back", sPlayMuted);
    rl_play_draw_status();
}

#endif
