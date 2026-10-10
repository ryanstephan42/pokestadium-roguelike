/* Host-only estimator; native moves, typing and AI require emulator playtests. */
#include "rl_content.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "balance_data.h"

typedef struct Curve {
    unsigned reached, battles, wins;
    double player_level, enemy_level;
} Curve;

static double power(const RlMon *mon) {
    return mon->cur_hp * (double)(mon->current_stats[RL_ATTACK] +
        mon->current_stats[RL_SPECIAL] + mon->current_stats[RL_SPEED]);
}

static unsigned choose_lane(const RunState *run) {
    unsigned edges, lane, best, value, best_value, row, type;
    row = run->row == RL_NO_SLOT ? 0 : run->row + 1U;
    edges = run->row == RL_NO_SLOT ? 15U :
        run->map.nodes[run->row][run->lane].edges;
    best = 0;
    best_value = 0;
    for (lane = 0; lane < run->map.lanes[row]; lane++) {
        if (!(edges & (1U << lane))) continue;
        type = run->map.nodes[row][lane].type;
        value = 1;
        if (type == RL_BATTLE) value = 4;
        if (type == RL_CENTER) {
            unsigned i;
            for (i = 0; i < run->party_count; i++)
                if (run->party[i].cur_hp < run->party[i].max_hp / 2)
                    value = 5;
        }
        if (type == RL_RECRUIT && run->party_count < RL_PARTY) value = 6;
        if (type == RL_EVENT) value = 3;
        if (type == RL_MART) value = 2;
        if (value > best_value) { best = lane; best_value = value; }
    }
    return best;
}

static void draft(RunState *run) {
    RlMon choices[6], swap;
    unsigned i, j, level;
    for (i = 0; i < 6; i++) {
        choices[i] = balance_starters.mons[
            rl_random_bounded(&run->rng, balance_starters.count)];
        level = choices[i].level;
        choices[i].level = (RlU8)level;
        choices[i].exp = rl_xp_for_level(level, choices[i].growth);
        rl_mon_recalculate_stats(&choices[i], 0);
        choices[i].cur_hp = choices[i].max_hp;
        choices[i].fainted = 0;
    }
    for (i = 0; i < 6; i++)
        for (j = i + 1; j < 6; j++)
            if (power(&choices[j]) > power(&choices[i])) {
                swap = choices[i]; choices[i] = choices[j]; choices[j] = swap;
            }
    for (i = 0; i < 3; i++) assert(rl_party_recruit(run, &choices[i]) == RL_OK);
    run->phase = RL_MAP;
}

static void rewards(RunState *run, const RlPool *enemy) {
    RlReward offers[4];
    unsigned count, i, best, value, best_value;
    count = rl_rewards_generate(run, enemy, 0, offers, 4);
    best = 0;
    best_value = 0;
    for (i = 0; i < count; i++) {
        value = 1;
        if (offers[i].kind == RL_REWARD_RECRUIT && run->party_count == RL_PARTY)
            value = 0;
        if (offers[i].kind == RL_REWARD_RECRUIT && run->party_count < RL_PARTY)
            value = 6;
        if (offers[i].kind == RL_REWARD_CANDY) value = 5;
        if (offers[i].kind == RL_REWARD_RELIC) value = 4;
        if (value > best_value) { best = i; best_value = value; }
    }
    if (best_value) assert(rl_reward_apply(run, &offers[best]) == RL_OK);
    for (i = 0; i < run->party_count && run->bag[RL_CANDY]; i++)
        if (run->party[i].cur_hp)
            assert(rl_item_use(run, RL_CANDY, i, 0) == RL_OK);
}

static void battle(RunState *run, RlU32 *rng, Curve *curve) {
    RlEncounter encounter;
    RlMon enemy[6];
    RlPool source, defeated;
    unsigned i, j, slots[6], selected, count, won, damage;
    RlU8 mask;
    double player_power, enemy_power, ratio, odds, levels;
    assert(rl_encounter_select(run, &encounter) == RL_OK);
    source = balance_pools[encounter.round - 1][encounter.roster][encounter.trainer];
    count = source.count;
    assert(rl_enemy_generate(run, &source, encounter.type, enemy, count) == RL_OK);
    selected = 0;
    for (i = 0; i < run->party_count; i++)
        if (run->party[i].cur_hp) slots[selected++] = i;
    for (i = 0; i < selected; i++)
        for (j = i + 1; j < selected; j++)
            if (power(&run->party[slots[j]]) > power(&run->party[slots[i]])) {
                unsigned swap;
                swap = slots[i]; slots[i] = slots[j]; slots[j] = swap;
            }
    if (!rl_ascension(run->ascension).full_team) {
        if (selected > 3) selected = 3;
        if (count > 3) count = 3;
    }
    player_power = enemy_power = levels = 0;
    mask = 0;
    for (i = 0; i < selected; i++) {
        player_power += power(&run->party[slots[i]]);
        levels += run->party[slots[i]].level;
        mask |= (RlU8)(1U << slots[i]);
    }
    for (i = 0; i < count; i++) enemy_power += power(&enemy[i]);
    ratio = player_power / enemy_power;
    odds = ratio * ratio;
    odds *= odds;
    odds *= odds;
    odds = odds / (1 + odds);
    won = rl_random_bounded(rng, 1000000) < (RlU32)(odds * 1000000);
    curve->battles++;
    curve->wins += won;
    curve->player_level += selected ? levels / selected : 0;
    curve->enemy_level += enemy[0].level;
    for (i = 0; i < selected; i++) {
        RlMon *mon;
        mon = &run->party[slots[i]];
        damage = (unsigned)(mon->max_hp * (won ? 0.12 : 0.40) /
                            (ratio > 0.25 ? ratio : 0.25));
        mon->cur_hp = (RlU16)(damage < mon->cur_hp ? mon->cur_hp - damage : 0);
    }
    assert(rl_run_on_battle_result(run, won, mask) == RL_OK);
    if (run->phase == RL_REWARD) {
        defeated.mons = enemy;
        defeated.count = source.count;
        defeated.fixed_order = 0;
        rewards(run, &defeated);
    }
}

static int simulate(RlU32 seed, unsigned asc, Curve curves[4]) {
    RunState run;
    RlU32 battle_rng;
    unsigned act, type, i;
    RlMon recruit;
    rl_run_new(&run, seed, asc);
    battle_rng = seed ^ 0x51ed270bU;
    draft(&run);
    curves[0].reached++;
    while (!rl_run_is_over(&run)) {
        assert(rl_run_advance(&run, choose_lane(&run)) == RL_OK);
        act = run.act;
        type = run.map.nodes[run.row][run.lane].type;
        if (type == RL_BATTLE || type == RL_ELITE || type == RL_BOSS) {
            battle(&run, &battle_rng, &curves[act]);
        } else if (type == RL_CENTER) {
            for (i = 0; i < run.party_count; i++)
                rl_mon_heal(&run.party[i], rl_ascension(asc).heal_percent);
        } else if (type == RL_RECRUIT && run.party_count < RL_PARTY) {
            recruit = balance_starters.mons[
                rl_random_bounded(&run.rng, balance_starters.count)];
            recruit.level = (RlU8)rl_enemy_level(act,run.row,RL_BATTLE,0);
            recruit.exp = rl_xp_for_level(recruit.level,recruit.growth);
            rl_mon_recalculate_stats(&recruit, 0);
            recruit.cur_hp = recruit.max_hp;
            recruit.fainted = 0;
            assert(rl_party_recruit(&run, &recruit) == RL_OK);
        } else if (type == RL_EVENT) {
            assert(rl_event_resolve(&run, 7, 1, 0, &balance_starters, 0) == RL_OK);
        } else if (type == RL_MART) {
            RlShopEntry potion;
            potion.item = RL_POTION; potion.detail = 0;
            potion.price = (RlU16)(200 + 100 * act);
            for (i = 0; i < run.party_count; i++)
                if (run.party[i].cur_hp && run.party[i].cur_hp < run.party[i].max_hp &&
                    rl_shop_buy(&run, &potion) == RL_OK)
                    assert(rl_item_use(&run, RL_POTION, i, 0) == RL_OK);
        }
        if (run.phase == RL_NODE || run.phase == RL_REWARD)
            assert(rl_run_finish_node(&run) == RL_OK);
        if (run.act > act) curves[run.act].reached++;
    }
    return run.phase == RL_WON;
}

int main(int argc, char **argv) {
    unsigned runs, seed, asc, i, act, wins;
    Curve curves[4];
    if (argc != 3) return 2;
    runs = (unsigned)strtoul(argv[1], 0, 10);
    seed = (unsigned)strtoul(argv[2], 0, 10);
    puts("# Host power-model ESTIMATE; not native move/AI or emulator win rates");
    puts("ascension,runs,run_wins,run_win_pct,act,reached,battles,battle_win_pct,player_level,enemy_level");
    for (asc = 0; asc <= 10; asc++) {
        for (act = 0; act < 4; act++) {
            curves[act].reached = curves[act].battles = curves[act].wins = 0;
            curves[act].player_level = curves[act].enemy_level = 0;
        }
        wins = 0;
        for (i = 0; i < runs; i++) wins += simulate(seed + i, asc, curves);
        for (act = 0; act < 4; act++) {
            Curve *c;
            c = &curves[act];
            printf("%u,%u,%u,%.2f,%u,%u,%u,%.2f,%.2f,%.2f\n", asc,runs,wins,
                100.0*wins/runs,act,c->reached,c->battles,
                c->battles ? 100.0*c->wins/c->battles : 0,
                c->battles ? c->player_level/c->battles : 0,
                c->battles ? c->enemy_level/c->battles : 0);
        }
    }
    return 0;
}
