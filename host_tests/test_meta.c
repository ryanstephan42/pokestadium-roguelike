#include <assert.h>
#include <limits.h>

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
    assert(meta.hall_count == 0);
    assert(rl_meta_starter_unlocked(&meta, RL_META_POOL_STARTERS));
    assert(!rl_meta_starter_unlocked(&meta, RL_META_POOL_PRIME_CUP));
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
    assert(length == 361);
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
    assert(rl_meta_record_run(&meta, &run) == RL_OK);
    assert(meta.runs == UINT_MAX && meta.wins == UINT_MAX);
    assert(rl_meta_starter_unlocked(&meta, RL_META_POOL_LITTLE_CUP));
    assert(rl_meta_relic_unlocked(&meta, RL_RELIC_COUNT - 1));
    assert(rl_meta_event_unlocked(&meta, RL_EVENT_COUNT - 1));
}

int main(void) {
    test_initial_profile();
    test_progression_and_roundtrip();
    test_loss_rejection_and_saturation();
    return 0;
}
