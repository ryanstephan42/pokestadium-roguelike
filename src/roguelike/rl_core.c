/* PURE */
#include "rl_core.h"
#include <string.h>

RlU32 rl_random(RlU32 *state) {
    RlU32 x;
    if (!state) return 0;
    x = *state;
    if (!x) x = 0x6d2b79f5U;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

RlU32 rl_random_bounded(RlU32 *state, RlU32 limit) {
    RlU32 x, threshold;
    if (!state || !limit) return 0;
    threshold = (0U - limit) % limit;
    do { x = rl_random(state); } while (x < threshold);
    return x % limit;
}

void rl_run_new(RunState *run, RlU32 seed, unsigned ascension) {
    if (!run) return;
    memset(run, 0, sizeof(*run));
    run->seed = seed;
    run->rng = seed ? seed : 0x6d2b79f5U;
    run->ascension = (RlU8)(ascension > 10 ? 10 : ascension);
    run->row = RL_NO_SLOT;
    run->lane = RL_NO_SLOT;
    run->phase = RL_DRAFT;
    run->money = 300;
    rl_map_generate(&run->map, &run->rng, 0, run->ascension);
}

int rl_run_is_over(const RunState *run) {
    return run && (run->phase == RL_WON || run->phase == RL_LOST);
}

int rl_run_advance(RunState *run, unsigned lane) {
    unsigned next;
    unsigned left;
    if (!run || run->phase != RL_MAP || !run->party_count) return RL_INVALID;
    next = run->row == RL_NO_SLOT ? 0 : (unsigned)run->row + 1;
    if (next >= RL_ROWS || lane >= run->map.lanes[next]) return RL_INVALID;
    if (run->row != RL_NO_SLOT &&
        !(run->map.nodes[run->row][run->lane].edges & (1U << lane)))
        return RL_INVALID;
    run->row = (RlU8)next;
    run->lane = (RlU8)lane;
    run->phase = RL_NODE;
    run->stats.nodes++;
    if (run->daycare_active == 1) {
        run->daycare_nodes++;
        if (run->daycare_nodes >= 2) {
            left = rl_xp_for_level((unsigned)run->daycare.level + 5,
                                   run->daycare.growth);
            if (left > run->daycare.exp)
                rl_mon_add_xp(&run->daycare, left - run->daycare.exp);
            run->daycare_active = 2;
        }
    }
    if (run->daycare_active == 2 && run->party_count < RL_PARTY) {
        run->party[run->party_count++] = run->daycare;
        run->daycare_active = 0;
    }
    return RL_OK;
}

int rl_run_finish_node(RunState *run) {
    if (!run || (run->phase != RL_NODE && run->phase != RL_REWARD))
        return RL_INVALID;
    if (run->row == RL_ROWS - 1) {
        if (run->phase != RL_REWARD) return RL_INVALID;
        if (++run->act == 4) { run->phase = RL_WON; return RL_OK; }
        rl_map_generate(&run->map, &run->rng, run->act, run->ascension);
        run->row = RL_NO_SLOT;
        run->lane = RL_NO_SLOT;
    }
    run->phase = RL_MAP;
    return RL_OK;
}

int rl_run_on_battle_result(RunState *run, int won, RlU8 active_mask) {
    unsigned i, type, live;
    RlU32 xp;
    RlAscension asc;
    if (!run || run->phase != RL_NODE || run->row >= RL_ROWS ||
        run->lane >= run->map.lanes[run->row]) return RL_INVALID;
    type = run->map.nodes[run->row][run->lane].type;
    if (type != RL_BATTLE && type != RL_ELITE && type != RL_BOSS) return RL_INVALID;
    asc = rl_ascension(run->ascension);
    run->stats.battles++;
    for (i = 0; i < run->party_count; i++)
        run->party[i].fainted = (RlU8)(run->party[i].cur_hp == 0);
    if (asc.permadeath) {
        i = run->party_count;
        while (i) {
            i--;
            if (run->party[i].fainted) {
                rl_party_release(run, i);
                active_mask = (RlU8)((active_mask & ((1U << i) - 1)) |
                    ((active_mask >> (i + 1)) << i));
            }
        }
    }
    live = 0;
    for (i = 0; i < run->party_count; i++)
        if (!run->party[i].fainted) live++;
    if (!live || (!won && type == RL_BOSS)) {
        run->phase = RL_LOST;
        return RL_OK;
    }
    if (!won) { run->phase = RL_MAP; return RL_OK; }
    run->stats.wins++;
    xp = (RlU32)(run->act + 1) * 1200U + (RlU32)run->row * 200U;
    if (run->relics & (1U << RL_LUCKY_EGG)) xp = xp * 3 / 2;
    for (i = 0; i < run->party_count; i++) {
        if (!run->party[i].fainted) {
            if (active_mask & (1U << i)) rl_mon_add_xp(&run->party[i], xp);
            else if (run->relics & (1U << RL_EXP_SHARE))
                rl_mon_add_xp(&run->party[i], xp / 2);
        }
    }
    rl_relic_post_battle(run);
    run->phase = RL_REWARD;
    return RL_OK;
}
