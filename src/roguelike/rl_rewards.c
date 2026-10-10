/* PURE */
#include "rl_core.h"

static void rl_zero_bytes(void *target, unsigned size) {
    unsigned char *bytes;
    unsigned i;
    bytes = (unsigned char *)target;
    for (i = 0; i < size; i++) bytes[i] = 0;
}
const RlEventDef rl_events[RL_EVENT_COUNT] = {
    {"Mt. Moon Fossil", {"Kabuto fossil", "Omanyte fossil"}},
    {"Game Corner", {"Bet 100 coins", "Walk away"}},
    {"Daycare", {"Deposit a Pokemon", "Leave"}},
    {"Silph Co.", {"Trade for a stronger Pokemon", "Leave"}},
    {"Bill's PC", {"Swap a party Pokemon", "Leave"}},
    {"Lavender Tower", {"Take the cursed blessing", "Leave"}},
    {"Safari Zone", {"Recruit a wild Pokemon", "Leave"}},
    {"Professor Oak", {"Rare Candy", "Heal the party"}},
    {"Hidden Grotto", {"Search for a relic", "Leave"}},
    {"Berry Forest", {"Restore the party", "Take 200 coins"}},
    {"Move Tutor", {"Train a move slot", "Leave"}},
    {"Mysterious Shrine", {"Gain power and a curse", "Leave"}}
};

static int rl_random_relic(RunState *run, RlU32 owned) {
    RlU32 available;
    unsigned i, count, selected;
    available = (~owned) & ((1U << RL_RELIC_COUNT) - 1);
    count = 0;
    for (i = 0; i < RL_RELIC_COUNT; i++)
        if (available & (1U << i)) count++;
    if (!count) return -1;
    selected = rl_random_bounded(&run->rng, count);
    for (i = 0; i < RL_RELIC_COUNT; i++)
        if (available & (1U << i)) {
            if (!selected) return (int)i;
            selected--;
        }

    return -1;
}

static int rl_move_pool_valid(const RlMovePool *pool) {
    unsigned i;
    if (!pool || !pool->moves || !pool->count) return 0;
    for (i = 0; i < pool->count; i++)
        if (!pool->moves[i] || pool->moves[i] > 165) return 0;
    return 1;
}

unsigned rl_rewards_generate(RunState *run, const RlPool *pool,
                            const RlMovePool *moves,
                            RlReward *out, unsigned capacity) {
    unsigned count, i, kind, index;
    int relic;
    RlU32 owned;
    if (!run || !out || !capacity ||
        (moves && moves->count && !rl_move_pool_valid(moves))) return 0;
    count = rl_relic_reward_count(run->relics);
    if (count > capacity) count = capacity;
    owned = run->relics;
    for (i = 0; i < count; i++) {
        rl_zero_bytes(&out[i], sizeof(out[i]));
        kind = rl_random_bounded(&run->rng,
                                 rl_move_pool_valid(moves) ? 6 : 5);
        if (kind == RL_REWARD_TM) {
            index = rl_random_bounded(&run->rng, moves->count);
            out[i].detail = moves->moves[index];
        }
        out[i].kind = (RlU8)kind;
        switch (kind) {
        case RL_REWARD_RECRUIT:
            if (pool && pool->mons && pool->count) {
                index = rl_random_bounded(&run->rng, pool->count);
                out[i].mon = pool->mons[index];
                out[i].mon.cur_hp = out[i].mon.max_hp;
                out[i].detail = (RlU8)index;
            } else out[i].kind = RL_REWARD_MONEY;
            break;
        case RL_REWARD_CANDY: out[i].amount = 1 + run->act; break;
        case RL_REWARD_VITAMIN: out[i].amount = 1; break;
        case RL_REWARD_RELIC:
            relic = rl_random_relic(run, owned);
            if (relic < 0) {
                out[i].kind = RL_REWARD_MONEY;
                out[i].amount = 200;
            } else {
                out[i].detail = (RlU8)relic;
                owned |= 1U << relic;
            }
            break;
        case RL_REWARD_MONEY:
            out[i].amount = (RlU16)(100 + run->act * 50 +
                rl_random_bounded(&run->rng, 101));
            break;
        case RL_REWARD_TM:
            break;
        }
    }
    return count;
}

int rl_reward_apply(RunState *run, const RlReward *reward) {
    unsigned slot, item;
    unsigned money;
    RlAscension asc;
    if (!run || !reward) return RL_INVALID;
    switch (reward->kind) {
    case RL_REWARD_RECRUIT:
        return rl_party_recruit(run, &reward->mon);
    case RL_REWARD_CANDY:
        item = RL_CANDY;
        break;
    case RL_REWARD_VITAMIN:
        item = RL_VITAMIN;
        break;
    case RL_REWARD_RELIC:
        if (reward->detail >= RL_RELIC_COUNT) return RL_INVALID;
        if (run->relics & (1U << reward->detail)) return RL_INVALID;
        run->relics |= 1U << reward->detail;
        return RL_OK;
    case RL_REWARD_MONEY:
        asc = rl_ascension(run->ascension);
        money = (unsigned)reward->amount * asc.money_percent / 100;
        run->money += rl_relic_modify_money(run->relics, money);
        return RL_OK;
    case RL_REWARD_TM:
        if (!reward->detail || reward->detail > 165) return RL_INVALID;
        if (run->tm_count >= RL_TM_CAPACITY) return RL_FULL;
        run->tm_moves[run->tm_count++] = reward->detail;
        return RL_OK;
    default:
        return RL_INVALID;
    }
    slot = reward->amount ? reward->amount : 1;
    if (run->bag[item] + slot > 999) return RL_FULL;
    run->bag[item] = (RlU16)(run->bag[item] + slot);
    return RL_OK;
}

static RlU16 rl_price(unsigned price, unsigned act) {
    price += (price * act) / 2;
    return (RlU16)(price > 65535 ? 65535 : price);
}

unsigned rl_shop_generate(RunState *run, const RlMovePool *moves,
                          RlShopEntry *out, unsigned capacity) {
    static const RlU8 items[12] = {
        RL_POTION, RL_SUPER_POTION, RL_HYPER_POTION, RL_FULL_RESTORE,
        RL_REVIVE, RL_ETHER, RL_ELIXIR, RL_FULL_HEAL,
        RL_CANDY, RL_VITAMIN, RL_RELIC_ITEM, RL_TM
    };
    static const unsigned prices[12] = {
        200, 500, 1200, 2500, 1500, 600, 1200, 400,
        1800, 900, 5000, 3000
    };
    unsigned i, count, start, catalog_count;
    if (!run || !out || (moves && moves->count && !rl_move_pool_valid(moves)))
        return 0;
    catalog_count = rl_move_pool_valid(moves) ? 12U : 11U;
    count = capacity < catalog_count ? capacity : catalog_count;
    if (!count) return 0;
    start = rl_random_bounded(&run->rng, catalog_count);
    for (i = 0; i < count; i++) {
        unsigned index;
        index = (start + i) % catalog_count;
        out[i].item = items[index];
        if (out[i].item == RL_RELIC_ITEM) {
            int relic;
            relic = rl_random_relic(run, run->relics);
            out[i].detail = relic < 0 ? RL_NO_SLOT : (RlU8)relic;
        } else if (out[i].item == RL_TM && rl_move_pool_valid(moves))
            out[i].detail = moves->moves[rl_random_bounded(&run->rng, moves->count)];
        else if (out[i].item == RL_TM) {
            out[i].item = RL_MONEY;
            out[i].detail = 0;
        }
        else out[i].detail = 0;
        out[i].price = rl_price(prices[index], run->act);
    }
    return count;
}

int rl_shop_buy(RunState *run, const RlShopEntry *entry) {
    if (!run || !entry || entry->item >= RL_ITEM_COUNT) return RL_INVALID;
    if (run->money < entry->price) return RL_FUNDS;
    if (entry->item == RL_RELIC_ITEM) {
        if (entry->detail >= RL_RELIC_COUNT) return RL_INVALID;
        if (run->relics & (1U << entry->detail)) return RL_INVALID;
        run->money -= entry->price;
        run->relics |= 1U << entry->detail;
        return RL_OK;
    }
    if (run->bag[entry->item] == 999) return RL_FULL;
    if (entry->item == RL_TM) {
        if (!entry->detail || entry->detail > 165) return RL_INVALID;
        if (run->tm_count >= RL_TM_CAPACITY) return RL_FULL;
        run->money -= entry->price;
        run->tm_moves[run->tm_count++] = entry->detail;
        return RL_OK;
    }
    run->money -= entry->price;
    run->bag[entry->item]++;
    return RL_OK;
}

int rl_tm_teach(RunState *run, unsigned tm_slot, unsigned party_slot,
                unsigned move_slot, RlCanLearnMove can_learn, void *context) {
    unsigned i;
    RlU8 move, max_pp;
    RlMon *mon;
    if (!run || tm_slot >= run->tm_count || party_slot >= run->party_count ||
        move_slot >= 4 || !can_learn) return RL_INVALID;
    mon = &run->party[party_slot];
    if (mon->fainted || !mon->cur_hp) return RL_INVALID;
    move = run->tm_moves[tm_slot];
    max_pp = 0;
    if (!move || !can_learn(mon, move, &max_pp, context) || !max_pp)
        return RL_INVALID;
    mon->moves[move_slot] = move;
    mon->max_pp[move_slot] = max_pp;
    mon->pp[move_slot] = max_pp;
    for (i = tm_slot; i + 1 < run->tm_count; i++)
        run->tm_moves[i] = run->tm_moves[i + 1];
    run->tm_count--;
    run->tm_moves[run->tm_count] = 0;
    return RL_OK;
}

unsigned rl_event_wild_options(const RunState *run, const RlPool *pool,
                               RlMon *out, unsigned capacity) {
    RlU32 rng;
    unsigned count, i, index;
    if (!run || !pool || !pool->mons || !pool->count || !out) return 0;
    count = capacity < 3 ? capacity : 3;
    if (count > pool->count) count = pool->count;
    rng = run->rng;
    for (i = 0; i < count; i++) {
        if (pool->fixed_order) index = i % pool->count;
        else index = rl_random_bounded(&rng, pool->count);
        out[i] = pool->mons[index];
        out[i].cur_hp = out[i].max_hp;
        out[i].fainted = 0;
        out[i].status = 0;
    }
    return count;
}

static void rl_event_heal(RunState *run, unsigned percent) {
    unsigned i;
    for (i = 0; i < run->party_count; i++) rl_mon_heal(&run->party[i], percent);
}

int rl_event_resolve(RunState *run, unsigned event, unsigned choice,
                     unsigned slot, const RlPool *pool,
                     const RlMovePool *moves) {
    unsigned index, level, i;
    int relic;
    int result;
    RlMon mon, options[3];
    RlU32 rng;
    if (!run || event >= RL_EVENT_COUNT ||
        (event == 6 ? choice > 2 : choice > 1)) return RL_INVALID;
    if (choice == 1 && event != 0 && event != 6 &&
        event != 7 && event != 9) return RL_OK;
    if (slot >= run->party_count && (event == 2 || event == 3 || event == 4))
        return RL_INVALID;
    switch (event) {
    case 0:
        if (!pool || !pool->mons || !pool->count) return RL_INVALID;
        index = choice ? pool->count - 1 : 0;
        for (index = 0; index < pool->count; index++)
            if (pool->mons[index].species == (choice ? 138 : 140)) break;
        if (index == pool->count) return RL_INVALID;
        mon = pool->mons[index];
        if (!mon.base_stats[RL_HP] || !mon.species) return RL_INVALID;
        mon.level = (RlU8)(20 + run->act * 10);
        mon.exp = rl_xp_for_level(mon.level, mon.growth);
        rl_mon_recalculate_stats(&mon, 0);
        mon.cur_hp = mon.max_hp;
        return rl_party_recruit(run, &mon);
    case 1:
        if (run->money < 100) return RL_FUNDS;
        run->money -= 100;
        if (rl_random_bounded(&run->rng, 2)) run->money += 250;
        return RL_OK;
    case 2:
        if (run->daycare_active || run->party_count <= 1 ||
            !run->party[slot].species) return RL_INVALID;
        run->daycare = run->party[slot];
        run->daycare_active = 1;
        run->daycare_nodes = 0;
        return rl_party_release(run, slot);
    case 3:
        if (!pool || !pool->mons || !pool->count) return RL_INVALID;
        mon = pool->mons[rl_random_bounded(&run->rng, pool->count)];
        level = (unsigned)run->party[slot].level + 5;
        if (level > 100) level = 100;
        mon.level = (RlU8)level;
        mon.exp = rl_xp_for_level(level, mon.growth);
        rl_mon_recalculate_stats(&mon, 0);
        mon.cur_hp = mon.max_hp;
        mon.fainted = 0;
        run->party[slot] = mon;
        return RL_OK;
    case 4:
        if (!pool || !pool->mons || !pool->count) return RL_INVALID;
        mon = pool->mons[rl_random_bounded(&run->rng, pool->count)];
        mon.cur_hp = mon.max_hp;
        run->party[slot] = mon;
        return RL_OK;
    case 5:
        for (index = 0; index < run->party_count; index++)
            if (run->party[index].cur_hp > 1) run->party[index].cur_hp--;
        run->relics |= 1U << RL_GHOST;
        return RL_OK;
    case 6:
        if (!pool || !pool->mons || !pool->count) return RL_INVALID;
        if (run->party_count >= RL_PARTY) return RL_FULL;
        if (choice >= (pool->count < 3 ? pool->count : 3)) return RL_INVALID;
        if (rl_event_wild_options(run, pool, options, 3) == 0) return RL_INVALID;
        rng = run->rng;
        if (!pool->fixed_order)
            for (i = 0; i < (pool->count < 3 ? pool->count : 3); i++)
                rl_random_bounded(&rng, pool->count);
        result = rl_party_recruit(run, &options[choice]);
        if (result == RL_OK) run->rng = rng;
        return result;
    case 7:
        if (choice == 0) {
            if (run->bag[RL_CANDY] < 999) run->bag[RL_CANDY]++;
        } else rl_event_heal(run, 50);
        return RL_OK;
    case 8:
        relic = rl_random_relic(run, run->relics);
        if (relic < 0) return RL_FULL;
        run->relics |= 1U << relic;
        return RL_OK;
    case 9:
        if (choice) run->money += 200;
        else rl_event_heal(run, 50);
        return RL_OK;
    case 10:
        if (!rl_move_pool_valid(moves)) return RL_INVALID;
        if (run->tm_count >= RL_TM_CAPACITY) return RL_FULL;
        run->tm_moves[run->tm_count++] =
            moves->moves[rl_random_bounded(&run->rng, moves->count)];
        return RL_OK;
    case 11:
        run->relics |= 1U << RL_CURSED_POWER;
        return RL_OK;
    default:
        return RL_INVALID;
    }
}
