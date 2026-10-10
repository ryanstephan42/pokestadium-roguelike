#include <assert.h>
#include <limits.h>
#include <stddef.h>

#include "rl_meta.h"

static void prepare_terminal_run(RunState *run, unsigned ascension,
                                unsigned phase) {
    rl_run_new(run, 0x12345678U, ascension);
    run->party_count = 1;
    run->party[0] = rl_placeholder_pool.mons[2];
    if (phase == RL_WON) {
        run->act = 3;
        rl_map_generate(&run->map, &run->rng, 3, ascension);
        run->row = 0;
        run->lane = 0;
    }
    run->phase = (RlU8)phase;
}

static void test_initial_profile(void) {
    RlMeta meta;
    RlU8 selected, event;
    RlU32 rng, rng_copy;
    rl_meta_init(&meta);
    assert(meta.unlocked_pools == 1U);
    assert(meta.unlocked_relics == 0xffU);
    assert(meta.unlocked_events == 0xffU);
    assert(rl_meta_permitted_pools(&meta) == 1U);
    assert(rl_meta_permitted_relics(&meta) == 0xffU);
    assert(rl_meta_permitted_events(&meta) == 0xffU);
    assert(meta.runs == 0 && meta.wins == 0);
    assert(meta.best_floor == 0 && meta.ascension == 0);
    assert(meta.currency == 0 && meta.options == 0);
    assert(meta.hall_count == 0);
    assert(rl_meta_starter_unlocked(&meta, RL_META_POOL_STARTERS));
    assert(!rl_meta_starter_unlocked(&meta, RL_META_POOL_PRIME_CUP));
    assert(rl_meta_species_unlocked(&meta, 1));
    assert(rl_meta_species_unlocked(&meta, 25));
    assert(!rl_meta_species_unlocked(&meta, 8));
    assert(!rl_meta_species_unlocked(&meta, 151));
    assert(!rl_meta_option_unlocked(&meta, RL_META_MOVESET_1));
    assert(rl_meta_option_available(RL_META_MOVESET_1));
    assert(rl_meta_option_available(RL_META_THEME_4));
    assert(rl_meta_option_available(RL_META_KIT_1));
    assert(!rl_meta_option_available(RL_META_KIT_2));
    assert(!rl_meta_option_available(RL_META_EVENT_PACK_1));
    assert(!rl_meta_option_available(RL_META_CONTRACT_1));
    assert(!rl_meta_option_available(RL_META_COSMETIC_1));
    assert(rl_meta_option_cost(RL_META_MOVESET_1) == 30);
    assert(rl_meta_option_cost(RL_META_KIT_2) == 0);
    assert(!rl_meta_relic_unlocked(&meta, RL_RELIC_COUNT));
    assert(!rl_meta_event_unlocked(&meta, RL_EVENT_COUNT));
    selected = 77;
    assert(rl_meta_select_ascension(&meta, 0, &selected) == RL_OK);
    assert(selected == 0);
    assert(rl_meta_select_ascension(&meta, 1, &selected) == RL_INVALID);
    assert(selected == 0);

    rng = 0x98765432U;
    rng_copy = rng;
    event = RL_EVENT_COUNT;
    assert(rl_meta_choose_event(&meta, &rng, &event) == RL_OK);
    assert(rl_meta_event_unlocked(&meta, event));
    assert(rl_meta_choose_event(&meta, &rng_copy, &selected) == RL_OK);
    assert(selected == event && rng_copy == rng);

    meta.unlocked_events = 0;
    rng = 7;
    event = 99;
    assert(rl_meta_choose_event(&meta, &rng, &event) == RL_EMPTY);
    assert(rng == 7 && event == 99);
}

static void test_progression_and_roundtrip(void) {
    RlMeta meta, loaded;
    RunState run;
    RlU8 buffer[RL_SAVE_MAX_SIZE], has_run, selected;
    unsigned length, i;

    rl_meta_init(&meta);
    for (i = 0; i < 13; i++) {
        prepare_terminal_run(&run, meta.ascension, RL_WON);
        assert(rl_meta_record_run(&meta, &run) == RL_OK);
        assert(meta.runs == i + 1);
        assert(meta.wins == i + 1);
        assert(meta.best_floor == 22);
    }
    assert(meta.ascension == 10);
    assert(meta.currency == 325);
    assert(rl_meta_starter_unlocked(&meta, RL_META_POOL_PRIME_CUP));
    assert(rl_meta_starter_unlocked(&meta, RL_META_POOL_LITTLE_CUP));
    assert(rl_meta_permitted_pools(&meta) == 7U);
    for (i = 0; i < RL_RELIC_COUNT; i++)
        assert(rl_meta_relic_unlocked(&meta, i));
    for (i = 0; i < RL_EVENT_COUNT; i++)
        assert(rl_meta_event_unlocked(&meta, i));
    assert(rl_meta_select_ascension(&meta, 10, &selected) == RL_OK);
    assert(selected == 10);
    assert(rl_meta_select_ascension(&meta, 11, &selected) == RL_INVALID);

    assert(meta.hall_count == 1);
    assert(meta.hall[0].species == run.party[0].species);
    assert(rl_save_encode(&meta, 0, buffer, sizeof(buffer), &length) == RL_OK);
    assert(length == 416);
    assert(rl_save_decode(&loaded, 0, &has_run, buffer, length) == RL_OK);
    assert(!has_run);
    assert(loaded.unlocked_pools == meta.unlocked_pools);
    assert(loaded.unlocked_relics == meta.unlocked_relics);
    assert(loaded.unlocked_events == meta.unlocked_events);
    assert(loaded.runs == meta.runs && loaded.wins == meta.wins);
    assert(loaded.best_floor == meta.best_floor);
    assert(loaded.ascension == meta.ascension);
    assert(loaded.hall_count == meta.hall_count);
    assert(loaded.hall[0].species == meta.hall[0].species);
    assert(loaded.currency == meta.currency);
    assert(loaded.pending_registration_count == 0);
}

static void test_options_species_and_currency(void) {
    RlMeta meta, loaded;
    RunState run;
    RlU8 species[3], selected, buffer[RL_SAVE_MAX_SIZE], has_run;
    RlU16 bst[3];
    RlU32 rng, before_rng;
    unsigned length, seed, low_bst_count, high_bst_count;

    rl_meta_init(&meta);
    meta.currency = 60;
    assert(rl_meta_purchase_option(&meta, RL_META_MOVESET_1) == RL_OK);
    assert(meta.currency == 30);
    assert(rl_meta_purchase_option(&meta, RL_META_MOVESET_1) == RL_OK);
    assert(meta.currency == 30);
    assert(rl_meta_purchase_option(&meta, RL_META_THEME_1) == RL_FUNDS);
    assert(rl_meta_purchase_option(&meta, RL_META_OPTION_COUNT) == RL_INVALID);
    assert(rl_meta_purchase_option(&meta, RL_META_KIT_2) == RL_INVALID);
    assert(rl_meta_purchase_option(&meta, RL_META_EVENT_PACK_1) == RL_INVALID);
    assert(rl_meta_option_unlocked(&meta, RL_META_MOVESET_1));

    prepare_terminal_run(&run, 0, RL_LOST);
    run.act = 2;
    run.row = 6;
    run.lane = 0;
    run.party[0].species = 8;
    run.stats.battles = 4;
    run.stats.wins = 3;
    rl_map_generate(&run.map, &run.rng, 2, 0);
    assert(rl_meta_record_run(&meta, &run) == RL_OK);
    assert(meta.currency == 65);
    assert(meta.pending_registration_count == 1);
    assert(rl_meta_register_species(&meta, 9) == RL_INVALID);
    assert(rl_meta_register_species(&meta, 8) == RL_OK);
    assert(rl_meta_species_unlocked(&meta, 8));
    assert(meta.last_registered_species == 8);
    assert(meta.pending_registration_count == 0);
    assert(rl_meta_register_species(&meta, 8) == RL_OK);
    assert(rl_meta_register_species(&meta, 9) == RL_INVALID);

    assert(rl_save_encode(&meta, 0, buffer, sizeof(buffer), &length) == RL_OK);
    assert(rl_save_decode(&loaded, 0, &has_run, buffer, length) == RL_OK);
    assert(loaded.currency == meta.currency);
    assert(loaded.options == meta.options);
    assert(loaded.last_registration_run == meta.last_registration_run);
    assert(loaded.last_registered_species == 8);
    assert(rl_meta_species_unlocked(&loaded, 8));

    species[0] = 17;
    species[1] = 18;
    species[2] = 150;
    bst[0] = 250;
    bst[1] = 500;
    bst[2] = 600;
    low_bst_count = 0;
    high_bst_count = 0;
    for (seed = 1; seed <= 3000; seed++) {
        rng = seed;
        before_rng = rng;
        selected = 0;
        assert(rl_meta_choose_species(&meta, &rng, species, bst, 3,
                                      &selected) == RL_OK);
        assert(rng != before_rng);
        if (selected == 17) low_bst_count++;
        else if (selected == 18 || selected == 150) high_bst_count++;
        else assert(0);
    }
    assert(low_bst_count > high_bst_count);
    species[0] = 1;
    rng = 99;
    before_rng = rng;
    selected = 77;
    assert(rl_meta_choose_species(&meta, &rng, species, bst, 1,
                                  &selected) == RL_EMPTY);
    assert(rng == before_rng && selected == 77);

    prepare_terminal_run(&run, 0, RL_WON);
    run.party[0].species = 9;
    run.stats.battles = 2;
    run.stats.wins = 2;
    assert(rl_meta_record_run(&loaded, &run) == RL_OK);
    assert(loaded.pending_registration_count == 1);
    assert(rl_save_encode(&loaded, 0, buffer, sizeof(buffer), &length) == RL_OK);
    assert(rl_save_decode(&meta, 0, &has_run, buffer, length) == RL_OK);
    assert(meta.pending_species[(9 - 1) >> 3] & (1U << ((9 - 1) & 7)));
    assert(rl_meta_skip_species_registration(&meta) == RL_OK);
    assert(meta.pending_registration_count == 0);
    assert(rl_meta_skip_species_registration(&meta) == RL_INVALID);
}

static void test_loss_rejection_and_saturation(void) {
    RlMeta meta, before;
    RunState run;

    rl_meta_init(&meta);
    prepare_terminal_run(&run, 0, RL_LOST);
    run.act = 2;
    run.row = 6;
    run.lane = 0;
    rl_map_generate(&run.map, &run.rng, 2, 0);
    assert(rl_meta_record_run(&meta, &run) == RL_OK);
    assert(meta.runs == 1 && meta.wins == 0);
    assert(meta.best_floor == 21);
    assert(meta.currency == 0);
    assert(meta.hall_count == 0);
    assert(!rl_meta_starter_unlocked(&meta, RL_META_POOL_PRIME_CUP));
    assert(!rl_meta_relic_unlocked(&meta, RL_META_INITIAL_RELICS));

    before = meta;
    run.phase = RL_MAP;
    assert(rl_meta_record_run(&meta, &run) == RL_INVALID);
    assert(meta.runs == before.runs && meta.wins == before.wins);
    assert(meta.best_floor == before.best_floor);
    assert(meta.unlocked_pools == before.unlocked_pools);
    assert(meta.unlocked_relics == before.unlocked_relics);
    assert(meta.unlocked_events == before.unlocked_events);
    assert(meta.ascension == before.ascension);
    assert(meta.hall_count == before.hall_count);
    assert(meta.hall[0].species == before.hall[0].species);

    meta.runs = UINT_MAX;
    meta.wins = UINT_MAX;
    prepare_terminal_run(&run, 0, RL_WON);
    run.party[0].species = 12;
    run.stats.battles = 2;
    run.stats.wins = 1;
    assert(rl_meta_record_run(&meta, &run) == RL_OK);
    assert(meta.runs == UINT_MAX && meta.wins == UINT_MAX);
    assert(meta.currency == 40);
    assert(meta.pending_registration_count == 1);
    assert(meta.pending_species[(12 - 1) >> 3] &
           (1U << ((12 - 1) & 7)));
    assert(rl_meta_starter_unlocked(&meta, RL_META_POOL_LITTLE_CUP));
    assert(rl_meta_relic_unlocked(&meta, RL_RELIC_COUNT - 1));
    assert(rl_meta_event_unlocked(&meta, RL_EVENT_COUNT - 1));
}

static void set_lost_team(RunState *run, unsigned species, unsigned battles,
                          unsigned wins) {
    prepare_terminal_run(run, 0, RL_LOST);
    run->act = 2;
    run->row = 6;
    run->lane = 0;
    run->party[0].species = (RlU8)species;
    run->stats.battles = battles;
    run->stats.wins = wins;
    rl_map_generate(&run->map, &run->rng, 2, 0);
}

static void test_accumulated_registration_receipts(void) {
    RlMeta meta, loaded, overflow;
    RunState run;
    RlU8 bytes[RL_SAVE_MAX_SIZE], has_run;
    unsigned length;

    rl_meta_init(&meta);
    set_lost_team(&run, 8, 4, 3);
    assert(rl_meta_record_run(&meta, &run) == RL_OK);
    assert(meta.runs == 1 && meta.currency == 35);
    assert(meta.pending_registration_count == 1);

    assert(rl_save_encode(&meta, NULL, bytes, sizeof(bytes), &length) == RL_OK);
    assert(rl_save_decode(&loaded, NULL, &has_run, bytes, length) == RL_OK);
    assert(!has_run && loaded.pending_registration_count == 1);

    set_lost_team(&run, 9, 2, 1);
    assert(rl_meta_record_run(&loaded, &run) == RL_OK);
    assert(loaded.runs == 2 && loaded.currency == 50);
    assert(loaded.pending_registration_count == 2);
    assert(loaded.pending_species[(8 - 1) >> 3] &
           (1U << ((8 - 1) & 7)));
    assert(loaded.pending_species[(9 - 1) >> 3] &
           (1U << ((9 - 1) & 7)));

    assert(rl_meta_register_species(&loaded, 8) == RL_OK);
    assert(loaded.pending_registration_count == 1);
    assert(! (loaded.pending_species[(8 - 1) >> 3] &
              (1U << ((8 - 1) & 7))));
    assert(loaded.pending_species[(9 - 1) >> 3] &
           (1U << ((9 - 1) & 7)));
    assert(rl_meta_register_species(&loaded, 8) == RL_OK);
    assert(loaded.pending_registration_count == 1);
    assert(rl_meta_register_species(&loaded, 7) == RL_INVALID);

    assert(rl_save_encode(&loaded, NULL, bytes, sizeof(bytes), &length) == RL_OK);
    assert(rl_save_decode(&meta, NULL, &has_run, bytes, length) == RL_OK);
    assert(meta.pending_registration_count == 1);
    assert(rl_meta_register_species(&meta, 9) == RL_OK);
    assert(meta.pending_registration_count == 0);
    assert(meta.pending_species[(9 - 1) >> 3] == 0);
    assert(meta.runs == 2 && meta.currency == 50);

    set_lost_team(&run, 12, 1, 0);
    assert(rl_meta_record_run(&meta, &run) == RL_OK);
    set_lost_team(&run, 14, 1, 0);
    assert(rl_meta_record_run(&meta, &run) == RL_OK);
    assert(meta.pending_registration_count == 2);
    assert(rl_meta_skip_species_registration(&meta) == RL_OK);
    assert(meta.pending_registration_count == 1);
    assert(meta.pending_species[(12 - 1) >> 3] &
           (1U << ((12 - 1) & 7)));
    assert(meta.pending_species[(14 - 1) >> 3] &
           (1U << ((14 - 1) & 7)));
    assert(rl_meta_skip_species_registration(&meta) == RL_OK);
    assert(meta.pending_registration_count == 0);
    assert(meta.pending_species[(12 - 1) >> 3] == 0);
    assert(meta.pending_species[(14 - 1) >> 3] == 0);
    assert(rl_meta_skip_species_registration(&meta) == RL_INVALID);

    set_lost_team(&run, 1, 1, 0);
    assert(rl_meta_record_run(&meta, &run) == RL_OK);
    assert(meta.runs == 5 && meta.currency == 65);
    assert(meta.pending_registration_count == 0);

    overflow = meta;
    overflow.pending_registration_count = 152;
    assert(rl_meta_record_run(&overflow, &run) == RL_INVALID);
    assert(overflow.runs == meta.runs && overflow.currency == meta.currency);
}

int main(void) {
    test_initial_profile();
    test_progression_and_roundtrip();
    test_options_species_and_currency();
    test_loss_rejection_and_saturation();
    test_accumulated_registration_receipts();
    return 0;
}
