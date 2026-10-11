#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "rl_core.h"
#include "rl_save_format.h"
#include "rl_meta.h"

static RlMon mons[4];
static RlPool pool;
static const RlU8 moves[] = {85, 87};
static RlMovePool move_pool = {moves, 2};

static void init_run(RunState *run) {
    rl_run_new(run, 8, 0);
    assert(rl_party_recruit(run, &mons[0]) == RL_OK);
    assert(rl_party_recruit(run, &mons[1]) == RL_OK);
    run->phase = RL_MAP;
    run->money = 100000;
}

static void test_catalog(void) {
    RunState run;
    RlShopEntry entries[12];
    unsigned i, seen;
    init_run(&run);
    assert(rl_shop_generate(&run, &move_pool, entries, 12) == 12);
    seen = 0;
    for (i = 0; i < 12; i++) {
        seen |= 1U << entries[i].item;
        assert(rl_shop_buy(&run, &entries[i]) == RL_OK);
    }
    assert(seen == (1U << RL_MONEY) - 1);
    assert(run.tm_count == 1);
    assert(run.tm_moves[0] == 85 || run.tm_moves[0] == 87);
    assert(run.bag[RL_ELIXIR] == 1 && run.bag[RL_FULL_HEAL] == 1);
}

static void test_events(void) {
    RunState run;
    unsigned event, choice;
    for (event = 0; event < RL_EVENT_COUNT; event++) {
        for (choice = 0; choice < (event == 6 ? 3U : 2U); choice++) {
            init_run(&run);
            assert(rl_event_resolve(&run, event, choice, 0, &pool,
                                    &move_pool) == RL_OK);
            assert(run.party_count >= 1 && run.party_count <= RL_PARTY);
            if (event == 0)
                assert(run.party[2].species == (choice ? 138 : 140));
            if (event == 2 && choice == 0) {
                assert(run.daycare_active == 1 && run.party_count == 1);
                assert(rl_run_advance(&run, 0) == RL_OK);
                assert(rl_run_finish_node(&run) == RL_OK);
                assert(rl_run_advance(&run, 0) == RL_OK);
                assert(run.daycare_active == 0 && run.party_count == 2);
                assert(run.party[1].level == mons[0].level + 5);
            }
            if (event == 10 && choice == 0)
                assert(run.tm_count == 1 && run.tm_moves[0] != 0);
        }
    }
    init_run(&run);
    run.party[0].cur_hp = 0;
    run.party[0].fainted = 1;
    assert(rl_event_resolve(&run, 7, 1, 0, &pool, &move_pool) == RL_OK);
    assert(run.party[0].cur_hp == 0 && run.party[0].fainted);
    assert(rl_event_resolve(&run, 9, 0, 0, &pool, &move_pool) == RL_OK);
    assert(run.party[0].cur_hp == 0 && run.party[0].fainted);
    assert(rl_party_revive(&run, 0) == RL_OK);
    assert(run.party[0].cur_hp > 0 && !run.party[0].fainted);
}

static void test_node_suspend(void) {
    RunState run, restored;
    RlMeta meta, loaded;
    RlU8 buffer[RL_SAVE_MAX_SIZE], has_run;
    unsigned type, row, lane, size;
    memset(&meta, 0, sizeof(meta));
    for (type = RL_BATTLE; type <= RL_BOSS; type++) {
        init_run(&run);
        row = type == RL_BOSS ? RL_ROWS - 1 : 2;
        lane = 0;
        run.row = (RlU8)row;
        run.lane = (RlU8)lane;
        run.phase = RL_NODE;
        /* Keep the generated Center: replacing a sole Center would invalidate
         * the canonical save map. Use a different lane for the fixture. */
        if (type != RL_BOSS && run.map.nodes[row][lane].type == RL_CENTER)
            lane++;
        run.lane = (RlU8)lane;
        run.map.nodes[row][lane].type = (RlU8)type;
        assert(rl_map_valid(&run.map));
        assert(rl_save_encode(&meta, &run, buffer, sizeof(buffer), &size) == RL_OK);
        assert(size > 0);
        assert(rl_save_decode(&loaded, &restored, &has_run, buffer, size) == RL_OK);
        assert(has_run && restored.phase == RL_NODE);
        assert(restored.row == row && restored.lane == lane);
        assert(restored.map.nodes[row][lane].type == type);
        assert(restored.rng == run.rng);
        assert(rl_map_valid(&restored.map));
    }

}

static void test_complete_run_unlocks(void) {
        RunState run;
        RlMeta meta;
        unsigned lane, next, type;
        init_run(&run);
        rl_meta_init(&meta);
        while (!rl_run_is_over(&run)) {
            next = run.row == RL_NO_SLOT ? 0 : (unsigned)run.row + 1;
            lane = 0;
            if (run.row != RL_NO_SLOT) {
                while (!(run.map.nodes[run.row][run.lane].edges & (1U << lane))) lane++;
            }
            assert(next < rl_map_rows(run.act));
            assert(rl_run_advance(&run, lane) == RL_OK);
            type = run.map.nodes[run.row][run.lane].type;
            if (type == RL_BATTLE || type == RL_ELITE || type == RL_BOSS)
                assert(rl_run_on_battle_result(&run, 1, 3) == RL_OK);
            assert(rl_run_finish_node(&run) == RL_OK);
        }
        assert(run.phase == RL_WON && run.act == 3 && run.row == 0);
        assert(rl_meta_record_run(&meta, &run) == RL_OK);
        assert(meta.runs == 1 && meta.wins == 1 && meta.best_floor == 22);
        assert(meta.ascension == 1 && meta.hall_count == run.party_count);
        assert(rl_meta_starter_unlocked(&meta, RL_META_POOL_PRIME_CUP));
        assert(rl_meta_relic_unlocked(&meta, RL_AMULET));
        assert(rl_meta_event_unlocked(&meta, 8));
        assert(!rl_meta_event_unlocked(&meta, 9));
}

int main(void) {
    unsigned i;
    for (i = 0; i < 4; i++) mons[i] = rl_placeholder_pool.mons[i];
    mons[2].species = 140;
    mons[3].species = 138;
    pool.mons = mons;
    pool.count = 4;
    pool.fixed_order = 0;
    test_catalog();
    test_events();
    test_node_suspend();
    test_complete_run_unlocks();
    puts("Node catalog, twelve events and suspend tests passed");
    return 0;
}
