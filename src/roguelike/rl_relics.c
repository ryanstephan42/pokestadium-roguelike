/* PURE */
#include "rl_core.h"

const RlRelicDef rl_relic_defs[RL_RELIC_COUNT] = {
    {"Boulder Badge", "Defense +10%"},
    {"Cascade Badge", "Water damage +15%"},
    {"Thunder Badge", "Speed +10%"},
    {"Rainbow Badge", "Heal party 10% after battle"},
    {"Soul Badge", "Block status for first two turns"},
    {"Marsh Badge", "Special +10%"},
    {"Volcano Badge", "Fire damage +15%"},
    {"Earth Badge", "+1 reward choice"},
    {"Amulet Coin", "+50% battle money"},
    {"Exp. Share", "Bench earns half battle XP"},
    {"Lucky Egg", "+50% battle XP"},
    {"Leftovers Charm", "Recover 1/16 max HP each turn"},
    {"Focus Band Charm", "Once per battle survive at 1 HP"},
    {"Scope Lens Charm", "Increased critical chance"},
    {"Silph Scope", "Reveal enemy party before battle"},
    {"PP Up Pouch", "Move maximum PP +20%"},
    {"Old Amber", "One-time additional recruit"},
    {"Master Ball", "One-time boss recruit"},
    {"Ghost Relic", "Ghost-type damage +15%"},
    {"Cursed Power", "All damage +10%; lose 1 HP each turn"}
};

static const RlAscension ascensions[11] = {
    {0,0,0,0,100,0,50},
    {1,0,0,0,95,1,50},
    {2,0,0,0,90,2,50},
    {3,1,1,0,85,3,50},
    {4,1,1,0,80,4,50},
    {5,1,1,0,75,5,50},
    {6,1,1,1,70,6,50},
    {7,1,1,1,65,7,50},
    {8,1,1,1,60,8,50},
    {9,1,1,1,55,9,50},
    {10,1,1,1,50,10,50}
};

RlAscension rl_ascension(unsigned level) {
    if (level > 10) level = 10;
    return ascensions[level];
}

unsigned rl_relic_modify_stat(RlU32 relics, unsigned stat, unsigned base) {
    unsigned percent;
    percent = 100;
    if (stat == RL_DEFENSE && (relics & (1U << RL_BOULDER))) percent += 10;
    if (stat == RL_SPEED && (relics & (1U << RL_THUNDER))) percent += 10;
    if (stat == RL_SPECIAL && (relics & (1U << RL_MARSH))) percent += 10;
    return (base * percent + 50) / 100;
}

unsigned rl_relic_modify_damage(RlU32 relics, unsigned type, unsigned base) {
    unsigned percent;
    percent = 100;
    if (type == RL_WATER && (relics & (1U << RL_CASCADE))) percent += 15;
    if (type == RL_FIRE && (relics & (1U << RL_VOLCANO))) percent += 15;
    if (type == RL_GHOST_TYPE && (relics & (1U << RL_GHOST))) percent += 15;
    if (relics & (1U << RL_CURSED_POWER)) percent += 10;
    return (base * percent + 50) / 100;
}

void rl_relic_end_of_turn(RlU32 relics, RlMon *mon) {
    unsigned hp, loss;
    if (!mon || !mon->cur_hp || mon->fainted) return;
    if (relics & (1U << RL_LEFTOVERS)) {
        hp = mon->max_hp / 16;
        if (!hp) hp = 1;
        if (mon->cur_hp + hp > mon->max_hp) mon->cur_hp = mon->max_hp;
        else mon->cur_hp = (RlU16)(mon->cur_hp + hp);
    }
    if (relics & (1U << RL_CURSED_POWER)) {
        loss = 1;
        if (mon->cur_hp <= loss) {
            mon->cur_hp = 0;
            mon->fainted = 1;
        } else mon->cur_hp--;
    }
}

void rl_relic_post_battle(RunState *run) {
    unsigned i;
    if (!run) return;
    if (run->relics & (1U << RL_RAINBOW))
        for (i = 0; i < run->party_count; i++)
            if (!run->party[i].fainted) rl_mon_heal(&run->party[i], 10);
    if (run->relics & (1U << RL_AMULET)) run->money += 150;
}

unsigned rl_relic_reward_count(RlU32 relics) {
    return (relics & (1U << RL_EARTH)) ? 4 : 3;
}

int rl_relic_status_blocked(RlU32 relics, unsigned turn) {
    return (relics & (1U << RL_SOUL)) && turn < 2;
}

int rl_relic_survive(RlU32 relics, RlBattleHooks *hooks, RlMon *mon) {
    if (!hooks || !mon || !(relics & (1U << RL_FOCUS)) ||
        hooks->focus_used || !mon->max_hp) return 0;
    hooks->focus_used = 1;
    mon->cur_hp = 1;
    mon->fainted = 0;
    return 1;
}

unsigned rl_relic_crit_bonus(RlU32 relics) {
    return (relics & (1U << RL_SCOPE_LENS)) ? 1 : 0;
}

unsigned rl_relic_pp_max(RlU32 relics, unsigned base) {
    if (relics & (1U << RL_PP_POUCH)) return (base * 120 + 50) / 100;
    return base;
}

int rl_relic_consume_recruit(RunState *run, unsigned relic, const RlMon *mon) {
    int result;
    if (!run || !mon || (relic != RL_OLD_AMBER && relic != RL_MASTER_BALL))
        return RL_INVALID;
    if (!(run->relics & (1U << relic)) ||
        (run->consumed_relics & (1U << relic))) return RL_INVALID;
    result = rl_party_recruit(run, mon);
    if (result == RL_OK) run->consumed_relics |= 1U << relic;
    return result;
}
