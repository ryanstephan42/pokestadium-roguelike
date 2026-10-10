/* PURE */
#include "rl_meta_content.h"

static const RlU8 theme_species[RL_META_THEME_COUNT][24] = {
    { 1, 10, 11, 12, 13, 14, 15, 43, 44, 45, 46, 47,
      48, 49, 69, 70, 71, 102, 103, 123, 127, 0, 0, 0 },
    { 7, 8, 9, 54, 55, 60, 61, 62, 72, 73, 79, 80,
      86, 87, 90, 91, 98, 99, 116, 117, 118, 119, 120, 129 },
    { 27, 28, 29, 30, 31, 32, 33, 34, 41, 42, 50, 51,
      56, 57, 66, 67, 68, 74, 75, 76, 95, 104, 105, 111 },
    { 16, 17, 18, 21, 22, 25, 26, 35, 36, 39, 40, 63,
      64, 65, 77, 78, 83, 84, 85, 96, 97, 100, 101, 122 }
};

static const char *theme_names[RL_META_THEME_COUNT] = {
    "Forest", "Coast", "Cavern", "Sky"
};

static const char *option_names[RL_META_KIT_1 + 1] = {
    "Poke Cup loadouts", "Prime Cup loadouts",
    "Petit Cup loadouts", "Prime Cup Round 2 loadouts",
    "Forest starters", "Coast starters", "Cavern starters", "Sky starters",
    "Potion Cache"
};

static const unsigned theme_options[RL_META_THEME_COUNT] = {
    RL_META_THEME_1, RL_META_THEME_2, RL_META_THEME_3, RL_META_THEME_4
};

static const RlMetaKitDef kits[] = {
    { "Coin Purse", RL_META_KIT_DEFAULT, RL_META_STARTING_BUDGET,
      RL_META_STARTING_BUDGET, { 0 } },
    { "Potion Cache", RL_META_KIT_POTION, 100,
      RL_META_STARTING_BUDGET, { 1, 0 } }
};

static unsigned move_option_source(unsigned option) {
    switch (option) {
    case RL_META_MOVESET_1: return RL_META_RENTAL_POKE_CUP;
    case RL_META_MOVESET_2: return RL_META_RENTAL_PRIME_CUP;
    case RL_META_MOVESET_3: return RL_META_RENTAL_PETIT_CUP;
    case RL_META_MOVESET_4: return RL_META_RENTAL_PRIME_CUP_R2;
    default: return RL_META_RENTAL_SOURCE_COUNT;
    }
}

static int valid_moves(const RlU8 moves[4]) {
    unsigned i;
    int empty;
    empty = 0;
    for (i = 0; i < 4; i++) {
        if (!moves[i]) {
            empty = 1;
        } else if (empty || moves[i] > 165) {
            return 0;
        }
    }
    return moves[0] != 0;
}

static int same_moves(const RlU8 a[4], const RlU8 b[4]) {
    unsigned i;
    for (i = 0; i < 4; i++)
        if (a[i] != b[i]) return 0;
    return 1;
}

static int valid_catalog(const RlMetaRentalLoadout *catalog,
                         unsigned count) {
    unsigned i;
    if (count > RL_META_MAX_CATALOG_RECORDS || (count && !catalog))
        return 0;
    for (i = 0; i < count; i++) {
        if (!catalog[i].species || catalog[i].species > 151 ||
            catalog[i].source >= RL_META_RENTAL_SOURCE_COUNT ||
            !valid_moves(catalog[i].moves)) return 0;
    }
    return 1;
}

static int collect_variants(unsigned source, unsigned species,
                            const RlU8 current_moves[4],
                            const RlMetaRentalLoadout *catalog,
                            unsigned catalog_count,
                            RlMetaMoveVariant *output,
                            unsigned *result_count) {
    unsigned i, j, count;
    int duplicate;

    count = 0;
    for (i = 0; i < catalog_count; i++) {
        if (catalog[i].source != source ||
            catalog[i].species != species ||
            same_moves(catalog[i].moves, current_moves)) continue;
        duplicate = 0;
        for (j = 0; j < i; j++) {
            if (catalog[j].source == source &&
                catalog[j].species == species &&
                same_moves(catalog[j].moves, catalog[i].moves)) {
                duplicate = 1;
                break;
            }
        }
        if (duplicate) continue;
        if (output) {
            unsigned move;
            for (move = 0; move < 4; move++)
                output[count].moves[move] = catalog[i].moves[move];
        }
        count++;
    }
    *result_count = count;
    return count ? RL_OK : RL_EMPTY;
}

int rl_meta_list_move_variants(const RlMeta *meta, unsigned option,
                               unsigned species, const RlU8 current_moves[4],
                               const RlMetaRentalLoadout *catalog,
                               unsigned catalog_count,
                               RlMetaMoveVariant *output,
                               unsigned capacity) {
    unsigned source, count;
    int result;

    source = move_option_source(option);
    if (!meta || source >= RL_META_RENTAL_SOURCE_COUNT ||
        !rl_meta_option_available(option) ||
        !rl_meta_option_unlocked(meta, option) ||
        !species || species > 151 || !current_moves ||
        !valid_moves(current_moves) ||
        !valid_catalog(catalog, catalog_count)) return RL_INVALID;
    result = collect_variants(source, species, current_moves,
                              catalog, catalog_count, 0, &count);
    if (result != RL_OK) return result;
    if (!output || capacity < count) return RL_FULL;
    result = collect_variants(source, species, current_moves,
                               catalog, catalog_count, output, &count);
    return result == RL_OK ? (int)count : result;
}

static int is_theme_species(unsigned theme, unsigned species) {
    unsigned i;
    for (i = 0; i < 24 && theme_species[theme][i]; i++)
        if (theme_species[theme][i] == species) return 1;
    return 0;
}

static int collect_theme(const RlMeta *meta, unsigned theme,
                         const RlU8 *canonical_species, unsigned count,
                         RlU8 *output, unsigned *result_count) {
    unsigned i, j, found;
    unsigned result;

    result = 0;
    for (i = 0; i < count; i++) {
        found = 0;
        for (j = 0; j < i; j++)
            if (canonical_species[j] == canonical_species[i]) {
                found = 1;
                break;
            }
        if (found || !is_theme_species(theme, canonical_species[i]) ||
            !rl_meta_species_unlocked(meta, canonical_species[i])) continue;
        if (output) output[result] = canonical_species[i];
        result++;
    }
    *result_count = result;
    return result ? RL_OK : RL_EMPTY;
}

int rl_meta_theme_species(const RlMeta *meta, unsigned theme,
                          const RlU8 *canonical_species, unsigned count,
                          RlU8 *output, unsigned capacity) {
    unsigned i, result_count, needed;
    int result;

    if (!meta || theme >= RL_META_THEME_COUNT ||
        !rl_meta_option_unlocked(meta, theme_options[theme]) ||
        count > RL_META_MAX_CATALOG_RECORDS ||
        (count && !canonical_species)) return RL_INVALID;
    for (i = 0; i < count; i++)
        if (!canonical_species[i] || canonical_species[i] > 151)
            return RL_INVALID;
    result = collect_theme(meta, theme, canonical_species, count, 0,
                           &needed);
    if (result != RL_OK) return result;
    if (!output || capacity < needed) return RL_FULL;
    result = collect_theme(meta, theme, canonical_species, count, output,
                           &result_count);
    return result == RL_OK ? (int)result_count : result;
}

const char *rl_meta_theme_name(unsigned theme) {
    if (theme >= RL_META_THEME_COUNT) return 0;
    return theme_names[theme];
}

unsigned rl_meta_theme_option(unsigned theme) {
    if (theme >= RL_META_THEME_COUNT) return RL_META_OPTION_COUNT;
    return theme_options[theme];
}

const char *rl_meta_option_name(unsigned option) {
    if (!rl_meta_option_available(option)) return 0;
    return option_names[option];
}

const RlMetaKitDef *rl_meta_kit(unsigned option) {
    unsigned i;
    for (i = 0; i < sizeof(kits) / sizeof(kits[0]); i++)
        if (kits[i].option == option) return &kits[i];
    return 0;
}

int rl_meta_apply_kit(const RlMeta *meta, RunState *run, unsigned option) {
    const RlMetaKitDef *kit;
    unsigned i;

    if (!meta || !run || (option != RL_META_KIT_DEFAULT &&
        (!rl_meta_option_available(option) ||
         !rl_meta_option_unlocked(meta, option)))) return RL_INVALID;
    kit = rl_meta_kit(option);
    if (!kit || run->phase != RL_DRAFT || run->act != 0 ||
        run->row != RL_NO_SLOT || run->lane != RL_NO_SLOT ||
        run->party_count || run->money != kit->budget ||
        run->tm_count || run->relics || run->consumed_relics ||
        run->stats.battles || run->stats.wins || run->stats.nodes ||
        run->stats.recruits) return RL_INVALID;
    for (i = 0; i < RL_ITEM_COUNT; i++)
        if (run->bag[i]) return RL_INVALID;
    run->money = kit->starting_money;
    for (i = 0; i < RL_ITEM_COUNT; i++) run->bag[i] = kit->bag[i];
    return RL_OK;
}
