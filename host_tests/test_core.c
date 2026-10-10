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
    RlMon mewtwo;
    RlPool boss_pool;
    RlU32 rng;
    unsigned i;
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
    assert(team[0].current_stats[RL_HP] == 102);
    assert(team[0].cur_hp == team[0].max_hp);
    rl_map_generate(&run.map, &run.rng, 3, 0);
    for (i = 0; i < run.map.lanes[RL_ROWS - 1]; i++)
        assert(run.map.nodes[RL_ROWS - 1][i].target_species == 150);
    mewtwo = rl_placeholder_pool.mons[0];
    mewtwo.species = 150;
    boss_pool.mons = &mewtwo;
    boss_pool.count = 1;
    boss_pool.fixed_order = 1;
    run.act = 3;
    run.row = RL_ROWS - 1;
    assert(rl_enemy_generate(&run, &boss_pool, RL_BOSS, team, 1) == RL_OK);
    boss_pool.mons = &rl_placeholder_pool.mons[0];
    rng = run.rng;
    rl_map_generate(&run.map, &rng, 3, 0);
    assert(rl_map_valid(&run.map));
}

static void test_xp_and_party(void) {
    RlMon mon;
    RlMon high_dv;
    RlU32 next;
    unsigned damage;
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
    assert(mon.max_hp == mon.current_stats[RL_HP]);
    mon.level = 49;
    mon.growth = RL_MEDIUM_FAST;
    mon.exp = rl_xp_for_level(49, mon.growth);
    mon.base_stats[RL_HP] = 100;
    mon.dvs = 0;
    rl_mon_recalculate_stats(&mon, 0);
    mon.cur_hp = 80;
    next = rl_xp_for_level(50, mon.growth);
    damage = mon.max_hp - mon.cur_hp;
    assert(rl_mon_add_xp(&mon, next - mon.exp) == RL_OK);
    assert((unsigned)(mon.max_hp - mon.cur_hp) == damage);
    high_dv = mon;
    high_dv.level = 49;
    high_dv.exp = rl_xp_for_level(49, high_dv.growth);
    high_dv.max_hp = 1;
    high_dv.cur_hp = 1;
    high_dv.dvs = 0xffff;
    assert(rl_mon_add_xp(&high_dv, next - high_dv.exp) == RL_OK);
    assert(mon.max_hp == 160);
    assert(high_dv.max_hp == 175);
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
    RunState a, b, safari;
    RlReward rewards_a[4], rewards_b[4];
    RlReward money_reward;
    RlShopEntry shop[10];
    RlMon wild[3];
    RlMon fossil_mons[2], single_mon;
    RlMovePool move_pool;
    RlPool fossil_pool, single_pool;
    RlU8 moves[2];
    unsigned i, count, shop_count;
    RlU32 event_rng, expected_rng;
    RlU32 offered;
    moves[0] = 85;
    moves[1] = 87;
    move_pool.moves = moves;
    move_pool.count = 2;
    fossil_mons[0] = rl_placeholder_pool.mons[0];
    fossil_mons[0].species = 138;
    fossil_mons[0].base_stats[RL_HP] = 35;
    fossil_mons[0].nickname[0] = 'O';
    fossil_mons[0].nickname[1] = '\0';
    fossil_mons[1] = rl_placeholder_pool.mons[1];
    fossil_mons[1].species = 140;
    fossil_mons[1].base_stats[RL_HP] = 30;
    fossil_mons[1].nickname[0] = 'K';
    fossil_mons[1].nickname[1] = '\0';
    fossil_pool.mons = fossil_mons;
    fossil_pool.count = 2;
    fossil_pool.fixed_order = 1;
    rl_run_new(&a, 0x98765U, 1);
    b = a;
    a.relics |= 1U << RL_EARTH;
    b.relics |= 1U << RL_EARTH;
    count = rl_rewards_generate(&a, &rl_placeholder_pool, &move_pool, rewards_a, 4);
    assert(count == 4);
    assert(rl_rewards_generate(&b, &rl_placeholder_pool, &move_pool,
                               rewards_b, 4) == count);
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
    shop_count = rl_shop_generate(&a, &move_pool, shop, 10);
    assert(shop_count == 10);
    for (i = 0; i < shop_count; i++) {
        assert(shop[i].item < RL_ITEM_COUNT);
        assert(shop[i].price >= 200);
        if (shop[i].item == RL_TM)
            assert(shop[i].detail == 85 || shop[i].detail == 87);
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
    assert(rl_event_resolve(&a, 0, 0, 0, &rl_placeholder_pool, 0) == RL_INVALID);
    assert(a.party_count == 0);
    assert(rl_event_resolve(&a, 0, 0, 0, &fossil_pool, 0) == RL_OK);
    assert(a.party_count == 1 && a.party[0].species == 140);
    assert(a.party[0].base_stats[RL_HP] == 30);
    event_rng = a.rng;
    count = rl_event_wild_options(&a, &rl_placeholder_pool, wild, 3);
    assert(count == 3);
    assert(a.rng == event_rng);
    expected_rng = event_rng;
    for (i = 0; i < count; i++)
        rl_random_bounded(&expected_rng, rl_placeholder_pool.count);
    assert(rl_event_resolve(&a, 6, 2, 0, &rl_placeholder_pool, 0) == RL_OK);
    assert(a.rng == expected_rng);
    assert(a.party_count == 2 && a.party[1].species == wild[2].species);
    assert(rl_event_resolve(&a, 7, 1, 0, &rl_placeholder_pool, 0) == RL_OK);
    assert(rl_event_resolve(&a, 10, 0, 0, &rl_placeholder_pool,
                            &move_pool) == RL_OK);
    assert(a.tm_count == 1);
    assert(a.tm_moves[0] == 85 || a.tm_moves[0] == 87);
    single_mon = rl_placeholder_pool.mons[0];
    single_pool.mons = &single_mon;
    single_pool.count = 1;
    single_pool.fixed_order = 0;
    rl_run_new(&safari, 77, 0);
    assert(rl_event_wild_options(&safari, &single_pool, wild, 3) == 1);
    assert(rl_event_resolve(&safari, 6, 1, 0, &single_pool, 0) == RL_INVALID);
    for (i = 0; i < RL_PARTY; i++)
        assert(rl_party_recruit(&safari, &single_mon) == RL_OK);
    event_rng = safari.rng;
    assert(rl_event_resolve(&safari, 6, 0, 0, &single_pool, 0) == RL_FULL);
    assert(safari.rng == event_rng);
}

static int can_learn_test(const RlMon *mon, RlU8 move, RlU8 *max_pp,
                          void *context) {
    (void)mon;
    if (move == *(const RlU8 *)context) *max_pp = 15;
    return move == *(const RlU8 *)context;
}

static void test_tutor_and_healing(void) {
    RunState run;
    RlMon mon;
    RlReward reward;
    RlU8 legal_move, rejected_move;
    rl_run_new(&run, 29, 0);
    mon = rl_placeholder_pool.mons[0];
    assert(rl_party_recruit(&run, &mon) == RL_OK);
    run.party[0].cur_hp = 0;
    run.party[0].fainted = 1;
    run.bag[RL_POTION] = 1;
    run.bag[RL_HYPER_POTION] = 1;
    run.bag[RL_FULL_RESTORE] = 1;
    assert(rl_item_use(&run, RL_POTION, 0, 0) == RL_INVALID);
    assert(run.party[0].cur_hp == 0 && run.party[0].fainted);
    assert(rl_item_use(&run, RL_HYPER_POTION, 0, 0) == RL_INVALID);
    assert(run.party[0].cur_hp == 0 && run.party[0].fainted);
    assert(rl_item_use(&run, RL_FULL_RESTORE, 0, 0) == RL_INVALID);
    assert(run.party[0].cur_hp == 0 && run.party[0].fainted);
    assert(rl_party_revive(&run, 0) == RL_OK);
    run.party[0].cur_hp = 10;
    assert(rl_item_use(&run, RL_POTION, 0, 0) == RL_OK);
    assert(run.party[0].cur_hp == 30);
    assert(rl_item_use(&run, RL_HYPER_POTION, 0, 0) == RL_OK);
    assert(run.party[0].cur_hp == run.party[0].max_hp);
    rl_mon_heal(&run.party[0], 100);
    assert(run.party[0].cur_hp == run.party[0].max_hp);
    run.party[0].cur_hp = 0;
    run.party[0].fainted = 1;
    rl_mon_heal(&run.party[0], 100);
    assert(run.party[0].cur_hp == 0 && run.party[0].fainted);
    run.party[0].cur_hp = 1;
    run.party[0].fainted = 0;
    memset(&reward, 0, sizeof(reward));
    reward.kind = RL_REWARD_TM;
    reward.detail = 85;
    assert(rl_reward_apply(&run, &reward) == RL_OK);
    assert(run.tm_count == 1 && run.tm_moves[0] == 85);
    legal_move = 87;
    rejected_move = 85;
    assert(rl_tm_teach(&run, 0, 0, 0, can_learn_test, &legal_move) == RL_INVALID);
    assert(run.tm_count == 1 && run.party[0].moves[0] != 85);
    assert(rl_tm_teach(&run, 0, 0, 0, can_learn_test, &rejected_move) == RL_OK);
    assert(run.tm_count == 0 && run.party[0].moves[0] == 85);
    assert(run.party[0].pp[0] == 15 && run.party[0].max_pp[0] == 15);
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
    rl_run_new(&run, 78, 0);
    assert(rl_party_recruit(&run, &active) == RL_OK);
    run.act = 3;
    rl_map_generate(&run.map, &run.rng, 3, 0);
    run.row = RL_ROWS - 1;
    run.lane = 0;
    run.phase = RL_NODE;
    assert(run.map.nodes[run.row][run.lane].target_species == 150);
    assert(rl_run_on_battle_result(&run, 1, 1) == RL_OK);
    assert(rl_run_finish_node(&run) == RL_OK);
    assert(rl_run_is_over(&run) && run.phase == RL_WON);
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
    run.tm_count = 1;
    run.tm_moves[0] = 85;
    assert(rl_save_encode(&meta, 0, buffer, sizeof(buffer), &length) == RL_OK);
    assert(length == 361);
    assert(length <= RL_META_MAX_SIZE);
    assert(rl_save_decode(&loaded, 0, &has_run, buffer, length) == RL_OK);
    assert(!has_run && loaded.runs == meta.runs);
    assert(loaded.hall[0].species == meta.hall[0].species);
    assert(rl_save_encode(&meta, &run, buffer, sizeof(buffer), &length) == RL_OK);
    assert(length == 917);
    assert(rl_save_decode(&loaded, &restored, &has_run, buffer, length) == RL_OK);
    assert(has_run && restored.seed == run.seed);
    assert(restored.party_count == 1 && restored.party[0].species == run.party[0].species);
    assert(restored.money == 777 && restored.bag[RL_CANDY] == 2);
    assert(restored.tm_count == 1 && restored.tm_moves[0] == 85);
    assert(restored.party[0].current_stats[RL_HP] == restored.party[0].max_hp);
    run.party[0].pp[0] = (RlU8)(run.party[0].max_pp[0] + 1);
    assert(rl_save_encode(&meta, &run, buffer, sizeof(buffer), &length) ==
           RL_INVALID);
    run.party[0] = rl_placeholder_pool.mons[2];
    run.party[0].species = 152;
    assert(rl_save_encode(&meta, &run, buffer, sizeof(buffer), &length) ==
           RL_INVALID);
    buffer[length / 2] ^= 0x80;
    assert(rl_save_decode(&loaded, &restored, &has_run, buffer, length) == RL_CORRUPT);
}

int main(void) {
    test_rng_and_map();
    test_enemy_scaling();
    test_xp_and_party();
    test_rewards_shop_and_events();
    test_tutor_and_healing();
    test_relics();
    test_run_lifecycle();
    test_save();
    puts("roguelike core tests passed");
    return 0;
}
