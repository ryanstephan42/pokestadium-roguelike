/* PURE */
#include "rl_core.h"

static RlU32 rl_cube(unsigned level) {
    return (RlU32)level * level * level;
}

static void rl_zero_mon(RlMon *mon) {
    volatile unsigned char *bytes;
    unsigned i;
    bytes = (volatile unsigned char *)mon;
    for (i = 0; i < sizeof(*mon); i++) bytes[i] = 0;
}

RlU32 rl_xp_for_level(unsigned level, unsigned growth) {
    RlS32 n, value;
    if (level > 100) level = 100;
    n = (RlS32)level;
    if (growth == RL_FAST) return (4U * rl_cube(level)) / 5U;
    if (growth == RL_SLOW) return (5U * rl_cube(level)) / 4U;
    if (growth == RL_MEDIUM_SLOW) {
        value = (6 * n * n * n) / 5 - 15 * n * n + 100 * n - 140;
        return value < 0 ? 0U : (RlU32)value;
    }
    return rl_cube(level);
}

static unsigned rl_isqrt(unsigned value) {
    unsigned bit, root;
    bit = 1U << 30;
    while (bit > value) bit >>= 2;
    root = 0;
    while (bit) {
        if (value >= root + bit) {
            value -= root + bit;
            root = (root >> 1) + bit;
        } else root >>= 1;
        bit >>= 2;
    }
    return root;
}

unsigned rl_mon_stat_value(const RlMon *mon, unsigned stat, unsigned level) {
    unsigned dv, effort, value, attack, defense, speed, special, root;
    if (!mon || stat >= 5 || level > 100) return 0;
    if (stat == RL_HP) {
        attack = mon->dvs & 15;
        defense = (mon->dvs >> 4) & 15;
        speed = (mon->dvs >> 8) & 15;
        special = (mon->dvs >> 12) & 15;
        dv = ((special & 1) << 3) | ((speed & 1) << 2) |
             ((defense & 1) << 1) | (attack & 1);
    } else dv = (mon->dvs >> ((stat - 1) * 4)) & 15;
    root = rl_isqrt(mon->stat_exp[stat]);
    if (root * root < mon->stat_exp[stat]) root++;
    if (root > 254) root = 255;
    effort = root / 4;
    value = (effort + 2 * (mon->base_stats[stat] + dv)) * level / 100;
    if (stat == RL_HP) return value + level + 10;
    return value + 5;
}

void rl_mon_recalculate_stats(RlMon *mon, int preserve_hp) {
    unsigned i, old_hp, new_hp;
    if (!mon || mon->level > 100) return;
    old_hp = mon->max_hp;
    for (i = 0; i < 5; i++) {
        unsigned value;
        value = rl_mon_stat_value(mon, i, mon->level);
        if (value > 999) value = 999;
        mon->current_stats[i] = (RlU16)value;
    }
    new_hp = mon->current_stats[RL_HP];
    if (preserve_hp && mon->cur_hp) {
        if (new_hp >= old_hp)
            mon->cur_hp = (RlU16)(mon->cur_hp + new_hp - old_hp);
        else if (mon->cur_hp > old_hp - new_hp)
            mon->cur_hp = (RlU16)(mon->cur_hp - (old_hp - new_hp));
        else mon->cur_hp = 0;
    }
    mon->max_hp = (RlU16)new_hp;
    if (mon->cur_hp > mon->max_hp) mon->cur_hp = mon->max_hp;
    mon->fainted = (RlU8)(mon->cur_hp == 0);
}

int rl_mon_add_xp(RlMon *mon, RlU32 xp) {
    RlU32 total, next_xp;
    unsigned level;
    if (!mon || mon->growth > RL_MEDIUM_SLOW || mon->level > 100)
        return RL_INVALID;
    total = mon->exp;
    if (total > 1250000U) total = 1250000U;
    if (xp > 1250000U - total) total = 1250000U;
    else total += xp;
    mon->exp = total;
    level = mon->level;
    while (level < 100) {
        next_xp = rl_xp_for_level(level + 1, mon->growth);
        if (next_xp > total) break;
        level++;
    }
    if (level == mon->level) return RL_OK;
    mon->level = (RlU8)level;
    rl_mon_recalculate_stats(mon, 1);
    return RL_OK;
}

void rl_mon_heal(RlMon *mon, unsigned percent) {
    unsigned hp;
    if (!mon || !mon->max_hp) return;
    if (percent > 100) percent = 100;
    if (mon->fainted || !mon->cur_hp) return;
    hp = (unsigned)mon->max_hp * percent / 100;
    if (!hp && percent) hp = 1;
    if (mon->cur_hp + hp > mon->max_hp) mon->cur_hp = mon->max_hp;
    else mon->cur_hp = (RlU16)(mon->cur_hp + hp);
    if (mon->cur_hp) mon->fainted = 0;
}

void rl_mon_restore_hp(RlMon *mon, unsigned amount) {
    unsigned value;
    if (!mon || mon->fainted || !mon->cur_hp) return;
    value = mon->cur_hp + amount;
    mon->cur_hp = (RlU16)(value > mon->max_hp ? mon->max_hp : value);
}

int rl_party_recruit(RunState *run, const RlMon *mon) {
    RlMon copy;
    if (!run || !mon || !mon->species || mon->species > 151 ||
        !mon->level || mon->level > 100 || mon->growth > RL_MEDIUM_SLOW ||
        !mon->max_hp || mon->cur_hp > mon->max_hp) return RL_INVALID;
    if (run->party_count >= RL_PARTY) return RL_FULL;
    copy = *mon;
    if (!copy.max_hp) return RL_INVALID;
    copy.fainted = (RlU8)(copy.cur_hp == 0);
    run->party[run->party_count++] = copy;
    run->stats.recruits++;
    return RL_OK;
}

int rl_party_release(RunState *run, unsigned slot) {
    unsigned i;
    if (!run || slot >= run->party_count) return RL_INVALID;
    for (i = slot; i + 1 < run->party_count; i++)
        run->party[i] = run->party[i + 1];
    run->party_count--;
    rl_zero_mon(&run->party[run->party_count]);
    return RL_OK;
}

int rl_party_swap(RunState *run, unsigned a, unsigned b) {
    RlMon temp;
    if (!run || a >= run->party_count || b >= run->party_count)
        return RL_INVALID;
    temp = run->party[a];
    run->party[a] = run->party[b];
    run->party[b] = temp;
    return RL_OK;
}

int rl_party_revive(RunState *run, unsigned slot) {
    if (!run || slot >= run->party_count) return RL_INVALID;
    if (!run->party[slot].fainted && run->party[slot].cur_hp) return RL_INVALID;
    run->party[slot].fainted = 0;
    run->party[slot].status = 0;
    run->party[slot].cur_hp = (RlU16)(run->party[slot].max_hp / 2);
    if (!run->party[slot].cur_hp) run->party[slot].cur_hp = 1;
    return RL_OK;
}

static void rl_restore_pp(RlMon *mon, unsigned move, unsigned amount) {
    unsigned value;
    value = mon->pp[move] + amount;
    mon->pp[move] = (RlU8)(value > mon->max_pp[move] ?
                          mon->max_pp[move] : value);
}

int rl_item_use(RunState *run, unsigned item, unsigned slot, unsigned move) {
    RlMon *mon;
    unsigned i;
    RlU32 threshold;
    if (!run || item >= RL_ITEM_COUNT || !run->bag[item]) return RL_EMPTY;
    if (slot >= run->party_count) return RL_INVALID;
    mon = &run->party[slot];
    if ((item == RL_POTION || item == RL_SUPER_POTION ||
         item == RL_HYPER_POTION || item == RL_FULL_RESTORE) &&
        (mon->fainted || !mon->cur_hp)) return RL_INVALID;
    switch (item) {
    case RL_POTION: rl_mon_restore_hp(mon, 20); break;
    case RL_SUPER_POTION: rl_mon_restore_hp(mon, 50); break;
    case RL_HYPER_POTION: rl_mon_restore_hp(mon, 200); break;
    case RL_FULL_RESTORE:
        if (!mon->fainted && mon->cur_hp) {
            mon->cur_hp = mon->max_hp;
            mon->status = 0;
        }
        break;
    case RL_REVIVE:
        if (rl_party_revive(run, slot) != RL_OK) return RL_INVALID;
        break;
    case RL_ETHER:
        if (move >= 4 || !mon->max_pp[move]) return RL_INVALID;
        rl_restore_pp(mon, move, 10);
        break;
    case RL_ELIXIR:
        for (i = 0; i < 4; i++) rl_restore_pp(mon, i, 10);
        break;
    case RL_FULL_HEAL:
        if (mon->fainted) return RL_INVALID;
        mon->status = 0;
        break;
    case RL_CANDY:
        if (mon->level >= 100) return RL_INVALID;
        threshold = rl_xp_for_level((unsigned)mon->level + 1, mon->growth);
        if (threshold > mon->exp) threshold -= mon->exp;
        else threshold = 0;
        if (rl_mon_add_xp(mon, threshold) != RL_OK) return RL_INVALID;
        break;
    case RL_VITAMIN:
        for (i = 0; i < 5; i++)
            if (mon->stat_exp[i] < 60000) {
                unsigned amount;
                amount = mon->stat_exp[i] + 2560;
                mon->stat_exp[i] = (RlU16)(amount > 65535 ? 65535 : amount);
            }
        rl_mon_recalculate_stats(mon, 1);
        break;
    case RL_MONEY:
        run->money += 100;
        break;
    default:
        return RL_INVALID;
    }
    run->bag[item]--;
    return RL_OK;
}
