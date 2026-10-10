#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "rl_meta_content.h"

static void buy(RlMeta *meta, unsigned option) {
    meta->currency += rl_meta_option_cost(option);
    assert(rl_meta_purchase_option(meta, option) == RL_OK);
}

static void test_move_variants(void) {
    static const RlMetaRentalLoadout catalog[] = {
        { 1, RL_META_RENTAL_POKE_CUP, { 1, 2, 3, 4 } },
        { 1, RL_META_RENTAL_POKE_CUP, { 5, 6, 7, 8 } },
        { 1, RL_META_RENTAL_POKE_CUP, { 5, 6, 7, 8 } },
        { 1, RL_META_RENTAL_POKE_CUP, { 1, 2, 3, 4 } },
        { 4, RL_META_RENTAL_POKE_CUP, { 9, 10, 11, 12 } },
        { 1, RL_META_RENTAL_PRIME_CUP, { 13, 14, 15, 16 } },
        { 1, RL_META_RENTAL_PETIT_CUP, { 17, 18, 19, 20 } },
        { 1, RL_META_RENTAL_PRIME_CUP_R2, { 21, 22, 23, 24 } }
    };
    static const unsigned options[RL_META_RENTAL_SOURCE_COUNT] = {
        RL_META_MOVESET_1, RL_META_MOVESET_2,
        RL_META_MOVESET_3, RL_META_MOVESET_4
    };
    static const RlU8 expected[RL_META_RENTAL_SOURCE_COUNT][4] = {
        { 5, 6, 7, 8 }, { 13, 14, 15, 16 },
        { 17, 18, 19, 20 }, { 21, 22, 23, 24 }
    };
    RlMeta meta;
    RlMetaMoveVariant variants[2], before[2];
    RlMetaRentalLoadout invalid[1];
    RlU8 current[4];
    unsigned i;

    rl_meta_init(&meta);
    meta.currency = 200;
    for (i = 0; i < RL_META_RENTAL_SOURCE_COUNT; i++) buy(&meta, options[i]);
    current[0] = 1;
    current[1] = 2;
    current[2] = 3;
    current[3] = 4;

    for (i = 0; i < RL_META_RENTAL_SOURCE_COUNT; i++) {
        memset(variants, 0, sizeof(variants));
        assert(rl_meta_list_move_variants(&meta, options[i], 1, current,
            catalog, sizeof(catalog) / sizeof(catalog[0]), variants, 2) == 1);
        assert(!memcmp(variants[0].moves, expected[i], 4));
    }
    memset(variants, 0xA5, sizeof(variants));
    memcpy(before, variants, sizeof(before));
    assert(rl_meta_list_move_variants(&meta, RL_META_MOVESET_1, 1, current,
        catalog, sizeof(catalog) / sizeof(catalog[0]), variants, 0) == RL_FULL);
    assert(!memcmp(variants, before, sizeof(variants)));
    assert(rl_meta_list_move_variants(&meta, RL_META_MOVESET_1, 2, current,
        catalog, sizeof(catalog) / sizeof(catalog[0]), variants, 2) == RL_EMPTY);
    assert(rl_meta_list_move_variants(&meta, RL_META_MOVESET_1, 1, current,
        catalog, sizeof(catalog) / sizeof(catalog[0]), 0, 2) == RL_FULL);

    invalid[0] = catalog[1];
    invalid[0].moves[3] = 166;
    assert(rl_meta_list_move_variants(&meta, RL_META_MOVESET_1, 1, current,
        invalid, 1, variants, 2) == RL_INVALID);
    assert(rl_meta_list_move_variants(&meta, RL_META_EVENT_PACK_1, 1,
        current, catalog, sizeof(catalog) / sizeof(catalog[0]),
        variants, 2) == RL_INVALID);
}

static void test_themed_species(void) {
    static const RlU8 canonical[] = {
        1, 10, 13, 43, 46, 48, 69, 60, 79, 90, 104, 116, 118, 129,
        4, 8, 151, 10
    };
    static const RlU8 forest_expected[] = { 1, 10, 13, 43, 46, 48, 69 };
    static const RlU8 coast_expected[] = { 60, 79, 90, 116, 118, 129 };
    RlMeta meta;
    RlU8 result[16], before[16];
    unsigned theme, option;

    rl_meta_init(&meta);
    assert(rl_meta_theme_name(RL_META_THEME_FOREST));
    assert(!strcmp(rl_meta_theme_name(RL_META_THEME_COAST), "Coast"));
    assert(!strcmp(rl_meta_option_name(RL_META_MOVESET_1),
                   "Poke Cup loadouts"));
    assert(!rl_meta_option_name(RL_META_EVENT_PACK_1));
    assert(rl_meta_theme_species(&meta, RL_META_THEME_FOREST, canonical,
        sizeof(canonical), result, 16) == RL_INVALID);
    meta.currency = 200;
    for (theme = 0; theme < RL_META_THEME_COUNT; theme++) {
        option = rl_meta_theme_option(theme);
        assert(option >= RL_META_THEME_1 && option <= RL_META_THEME_4);
        buy(&meta, option);
    }
    assert(rl_meta_theme_species(&meta, RL_META_THEME_FOREST, canonical,
        sizeof(canonical), result, 16) ==
        (int)(sizeof(forest_expected) / sizeof(forest_expected[0])));
    assert(!memcmp(result, forest_expected, sizeof(forest_expected)));
    assert(rl_meta_theme_species(&meta, RL_META_THEME_COAST, canonical,
        sizeof(canonical), result, 16) ==
        (int)(sizeof(coast_expected) / sizeof(coast_expected[0])));
    assert(!memcmp(result, coast_expected, sizeof(coast_expected)));
    memset(result, 0x5A, sizeof(result));
    memcpy(before, result, sizeof(before));
    assert(rl_meta_theme_species(&meta, RL_META_THEME_FOREST, canonical,
        sizeof(canonical), result, 2) == RL_FULL);
    assert(!memcmp(result, before, sizeof(result)));
    assert(rl_meta_theme_species(&meta, RL_META_THEME_CAVERN, canonical,
        0, result, 16) == RL_EMPTY);
}

static void test_equal_budget_kits(void) {
    RlMeta meta;
    RunState run;
    const RlMetaKitDef *default_kit, *potion_kit;
    RlU32 original_rng;

    rl_meta_init(&meta);
    default_kit = rl_meta_kit(RL_META_KIT_DEFAULT);
    potion_kit = rl_meta_kit(RL_META_KIT_POTION);
    assert(default_kit && potion_kit);
    assert(default_kit->budget == RL_META_STARTING_BUDGET);
    assert(default_kit->starting_money == RL_META_STARTING_BUDGET);
    assert(potion_kit->budget == RL_META_STARTING_BUDGET);
    assert(potion_kit->starting_money == 100);
    assert(potion_kit->bag[RL_POTION] == 1);
    assert(potion_kit->starting_money + 200U * potion_kit->bag[RL_POTION] ==
           potion_kit->budget);
    assert(!rl_meta_kit(RL_META_KIT_2));

    rl_run_new(&run, 0x12345678U, 0);
    original_rng = run.rng;
    assert(rl_meta_apply_kit(&meta, &run, RL_META_KIT_DEFAULT) == RL_OK);
    assert(run.money == RL_META_STARTING_BUDGET && run.rng == original_rng);
    assert(rl_meta_apply_kit(&meta, &run, RL_META_KIT_POTION) == RL_INVALID);

    buy(&meta, RL_META_KIT_1);
    rl_run_new(&run, 0x87654321U, 0);
    original_rng = run.rng;
    assert(rl_meta_apply_kit(&meta, &run, RL_META_KIT_POTION) == RL_OK);
    assert(run.money == 100 && run.bag[RL_POTION] == 1);
    assert(run.rng == original_rng);
    assert(rl_meta_apply_kit(&meta, &run, RL_META_KIT_POTION) == RL_INVALID);
    assert(run.money == 100 && run.bag[RL_POTION] == 1);
}

int main(void) {
    test_move_variants();
    test_themed_species();
    test_equal_budget_kits();
    puts("Meta content catalogs: canonical loadout filtering, themes, equal-budget kits: OK");
    return 0;
}
