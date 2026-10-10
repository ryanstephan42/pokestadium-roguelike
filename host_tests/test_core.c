#include "rl_core.h"
#include "rl_save_format.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_rng_and_map(void) {
    RlU32 a, b, seed;
    RlMap map_a, map_b;
    unsigned i;
    a = 0x12345678U;
    b = a;
    for (i = 0; i < 1000; i++) assert(rl_random(&a) == rl_random(&b));
    for (i = 0; i < 1000; i++)
        assert(rl_random_bounded(&a, 7) < 7);
    for (seed = 1; seed <= 10000; seed++) {
        RlU32 ra, rb;
        ra = seed;
        rb = seed;
        rl_map_generate(&map_a, &ra, seed % 4, seed % 11);
        rl_map_generate(&map_b, &rb, seed % 4, seed % 11);
        assert(rl_map_valid(&map_a));
        assert(memcmp(&map_a, &map_b, sizeof(map_a)) == 0);
        assert(ra == rb);
    }
}

static void test_enemy_scaling(void) {
    RunState run;
    RlMon team[3];
    assert(rl_enemy_level(0, 0, RL_BATTLE, 0) == 30);
    assert(rl_enemy_level(0, 0, RL_ELITE, 0) == 33);
    assert(rl_enemy_level(0, 6, RL_BOSS, 0) == 47);
    assert(rl_enemy_level(3, 6, RL_BOSS, 10) == 100);
    rl_run_new(&run, 1234, 0);
    run.row = 4;
    assert(rl_enemy_generate(&run, &rl_placeholder_pool, RL_ELITE, team, 3) == RL_OK);
    assert(team[0].level == 41);
    assert(team[0].dvs == 0xffff);
    assert(team[0].stat_exp[0] == 10000);
    assert(team[0].cur_hp == team[0].max_hp);
}

static void test_xp_and_party(void) {
    RlMon mon;
    RlMon high_dv;
    RlU32 next;
    RunState run;
    assert(rl_xp_for_level(1, RL_MEDIUM_FAST) == 1);
    assert(rl_xp_for_level(2, RL_MEDIUM_FAST) == 8);
    assert(rl_xp_for_level(100, RL_MEDIUM_FAST) == 1000000U);
    assert(rl_xp_for_level(100, RL_FAST) == 800000U);
    assert(rl_xp_for_level(100, RL_SLOW) == 1250000U);
    assert(rl_xp_for_level(2, RL_MEDIUM_SLOW) == 9);
    assert(rl_xp_for_level(100, RL_MEDIUM_SLOW) == 1059860U);
    mon = rl_placeholder_pool.mons[0];
    assert(rl_mon_add_xp(&mon, rl_xp_for_level(31, mon.growth) - mon.exp) == RL_OK);
    assert(mon.level == 31);
    assert(mon.max_hp >= 80);
    mon.level = 49;
    mon.growth = RL_MEDIUM_FAST;
    mon.exp = rl_xp_for_level(49, mon.growth);
    mon.max_hp = 1;
    mon.cur_hp = 1;
    mon.base_stats[RL_HP] = 100;
    mon.dvs = 0;
    next = rl_xp_for_level(50, mon.growth);
    assert(rl_mon_add_xp(&mon, next - mon.exp) == RL_OK);
    high_dv = mon;
    high_dv.level = 49;
    high_dv.exp = rl_xp_for_level(49, high_dv.growth);
    high_dv.max_hp = 1;
    high_dv.cur_hp = 1;
    high_dv.dvs = 0xffff;
    assert(rl_mon_add_xp(&high_dv, next - high_dv.exp) == RL_OK);
    assert(high_dv.max_hp > mon.max_hp);
    mon = rl_placeholder_pool.mons[0];
    mon.growth = RL_SLOW;
    mon.level = 99;
    mon.exp = rl_xp_for_level(99, RL_SLOW);
    mon.max_hp = 200;
    mon.cur_hp = 200;
    assert(rl_mon_add_xp(&mon,
        rl_xp_for_level(100, RL_SLOW) - mon.exp) == RL_OK);
    assert(mon.level == 100);
    rl_run_new(&run, 3, 0);
    assert(rl_party_recruit(&run, &mon) == RL_OK);
    assert(rl_party_recruit(&run, &rl_placeholder_pool.mons[1]) == RL_OK);
    run.party[0].cur_hp = 0;
    run.party[0].fainted = 1;
    assert(rl_party_revive(&run, 0) == RL_OK);
    assert(run.party[0].cur_hp > 0 && !run.party[0].fainted);
    assert(rl_party_swap(&run, 0, 1) == RL_OK);
    assert(rl_party_release(&run, 1) == RL_OK);
    assert(run.party_count == 1);
}

static void test_rewards_shop_and_events(void) {
    RunState a, b;
    RlReward rewards_a[4], rewards_b[4];
    RlReward money_reward;
    RlShopEntry shop[10];
    RlMon wild[3];
    unsigned i, count, shop_count;
    RlU32 event_rng;
    RlU32 offered;
    rl_run_new(&a, 0x98765U, 1);
    b = a;
    a.relics |= 1U << RL_EARTH;
    b.relics |= 1U << RL_EARTH;
    count = rl_rewards_generate(&a, &rl_placeholder_pool, rewards_a, 4);
    assert(count == 4);
    assert(rl_rewards_generate(&b, &rl_placeholder_pool, rewards_b, 4) == count);
    offered = a.relics;
    for (i = 0; i < count; i++) {
        assert(rewards_a[i].kind <= RL_REWARD_TM);
        assert(rewards_a[i].kind == rewards_b[i].kind);
        assert(rewards_a[i].detail == rewards_b[i].detail);
        assert(rewards_a[i].amount == rewards_b[i].amount);
        if (rewards_a[i].kind == RL_REWARD_RELIC) {
            assert(!(offered & (1U << rewards_a[i].detail)));
            offered |= 1U << rewards_a[i].detail;
        }
    }
    shop_count = rl_shop_generate(&a, shop, 10);
    assert(shop_count == 10);
    for (i = 0; i < shop_count; i++) {
        assert(shop[i].item < RL_ITEM_COUNT);
        assert(shop[i].price >= 200);
    }
    a.money = 10000;
    shop[0].item = RL_RELIC_ITEM;
    shop[0].detail = RL_BOULDER;
    shop[0].price = 100;
    assert(rl_shop_buy(&a, &shop[0]) == RL_OK);
    assert(a.relics & (1U << RL_BOULDER));
    memset(&money_reward, 0, sizeof(money_reward));
    money_reward.kind = RL_REWARD_MONEY;
    money_reward.amount = 100;
    a.relics |= 1U << RL_AMULET;
    assert(rl_reward_apply(&a, &money_reward) == RL_OK);
    assert(a.money == 10000 - 100 + 142);
    assert(rl_event_resolve(&a, 0, 0, 0, &rl_placeholder_pool) == RL_OK);
    assert(a.party_count == 1);
    event_rng = a.rng;
    count = rl_event_wild_options(&a, &rl_placeholder_pool, wild, 3);
    assert(count == 3);
    assert(a.rng == event_rng);
    assert(rl_event_resolve(&a, 6, 2, 0, &rl_placeholder_pool) == RL_OK);
    assert(a.party_count == 2 && a.party[1].species == wild[2].species);
    assert(rl_event_resolve(&a, 7, 1, 0, &rl_placeholder_pool) == RL_OK);
}

static void test_relics(void) {
    RlU32 all;
    RlMon mon;
    RlBattleHooks hooks;
    all = (1U << RL_BOULDER) | (1U << RL_CASCADE) |
        (1U << RL_THUNDER) | (1U << RL_RAINBOW) |
        (1U << RL_SOUL) | (1U << RL_LEFTOVERS) |
        (1U << RL_FOCUS) | (1U << RL_PP_POUCH);
    assert(rl_relic_modify_stat(all, RL_DEFENSE, 100) == 110);
    assert(rl_relic_modify_stat(all, RL_SPEED, 100) == 110);
    assert(rl_relic_modify_damage(all, RL_WATER, 100) == 115);
    assert(rl_relic_reward_count(all | (1U << RL_EARTH)) == 4);
    assert(rl_relic_status_blocked(all, 0));
    assert(!rl_relic_status_blocked(all, 2));
    assert(rl_relic_pp_max(all, 10) == 12);
    mon = rl_placeholder_pool.mons[0];
    mon.cur_hp = 0;
    memset(&hooks, 0, sizeof(hooks));
    assert(rl_relic_survive(all, &hooks, &mon));
    assert(mon.cur_hp == 1);
    mon.cur_hp = 40;
    rl_relic_end_of_turn(all, &mon);
    assert(mon.cur_hp > 40);
}

static void test_run_lifecycle(void) {
    RunState run;
    RlMon active, bench;
    unsigned lane;
    active = rl_placeholder_pool.mons[0];
    bench = rl_placeholder_pool.mons[1];
    rl_run_new(&run, 77, 0);
    assert(rl_party_recruit(&run, &active) == RL_OK);
    assert(rl_party_recruit(&run, &bench) == RL_OK);
    run.relics |= 1U << RL_EXP_SHARE;
    run.phase = RL_MAP;
    run.map.nodes[0][0].type = RL_BATTLE;
    assert(rl_run_advance(&run, 0) == RL_OK);
    assert(run.row == 0 && run.phase == RL_NODE);
    assert(rl_run_on_battle_result(&run, 1, 1) == RL_OK);
    assert(run.phase == RL_REWARD && run.stats.wins == 1);
    assert(run.party[0].exp > active.exp);
    assert(run.party[1].exp > bench.exp);
    assert(rl_run_finish_node(&run) == RL_OK);
    assert(run.phase == RL_MAP && run.row == 0);
    for (lane = 0; lane < run.map.lanes[1]; lane++)
        if (run.map.nodes[0][0].edges & (1U << lane)) break;
    assert(lane < run.map.lanes[1]);
    assert(rl_run_advance(&run, lane) == RL_OK);
    assert(run.row == 1);
    run.row = RL_ROWS - 1;
    run.lane = 0;
    run.phase = RL_NODE;
    run.map.nodes[RL_ROWS - 1][0].type = RL_BOSS;
    assert(rl_run_on_battle_result(&run, 0, 1) == RL_OK);
    assert(rl_run_is_over(&run) && run.phase == RL_LOST);
}

static void test_save(void) {
    RlMeta meta, loaded;
    RunState run, restored;
    RlU8 buffer[RL_SAVE_MAX_SIZE], has_run;
    unsigned length;
    memset(&meta, 0, sizeof(meta));
    meta.unlocked_pools = 7;
    meta.unlocked_relics = 0x1234;
    meta.unlocked_events = 0xff;
    meta.runs = 99;
    meta.wins = 42;
    meta.best_floor = 21;
    meta.ascension = 4;
    meta.hall_count = 1;
    meta.hall[0] = rl_placeholder_pool.mons[0];
    rl_run_new(&run, 0xabcdef01U, 3);
    run.party_count = 1;
    run.party[0] = rl_placeholder_pool.mons[2];
    run.money = 777;
    run.bag[RL_CANDY] = 2;
    run.relics = 1U << RL_LEFTOVERS;
    run.stats.battles = 5;
    assert(rl_save_encode(&meta, 0, buffer, sizeof(buffer), &length) == RL_OK);
    assert(length == 361);
    assert(length <= RL_META_MAX_SIZE);
    assert(rl_save_decode(&loaded, 0, &has_run, buffer, length) == RL_OK);
    assert(!has_run && loaded.runs == meta.runs);
    assert(loaded.hall[0].species == meta.hall[0].species);
    assert(rl_save_encode(&meta, &run, buffer, sizeof(buffer), &length) == RL_OK);
    assert(length == 872);
    assert(rl_save_decode(&loaded, &restored, &has_run, buffer, length) == RL_OK);
    assert(has_run && restored.seed == run.seed);
    assert(restored.party_count == 1 && restored.party[0].species == run.party[0].species);
    assert(restored.money == 777 && restored.bag[RL_CANDY] == 2);
    buffer[length / 2] ^= 0x80;
    assert(rl_save_decode(&loaded, &restored, &has_run, buffer, length) == RL_CORRUPT);
}

int main(void) {
    test_rng_and_map();
    test_enemy_scaling();
    test_xp_and_party();
    test_rewards_shop_and_events();
    test_relics();
    test_run_lifecycle();
    test_save();
    puts("roguelike core tests passed");
    return 0;
}
