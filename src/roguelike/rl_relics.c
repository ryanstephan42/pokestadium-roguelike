/* PURE */
#include "rl_core.h"

const RlRelicDef rl_relic_defs[RL_RELIC_COUNT] = {
    {"Boulder Badge", "Your Pokemon's Defense stat is 10% higher in battle."},
    {"Cascade Badge", "Water-type move damage is 15% higher in battle."},
    {"Thunder Badge", "Your Pokemon's Speed stat is 10% higher in battle."},
    {"Rainbow Badge", "After battle, each living party Pokemon heals 10% of max HP."},
    {"Soul Badge", "Blocks status effects during the first two turns of battle."},
    {"Marsh Badge", "Your Pokemon's Special stat is 10% higher in battle."},
    {"Volcano Badge", "Fire-type move damage is 15% higher in battle."},
    {"Earth Badge", "Offers one extra reward choice after battle."},
    {"Amulet Coin", "Increases run money rewards by 50%."},
    {"Exp. Share", "Bench Pokemon earn half of battle XP."},
    {"Lucky Egg", "Increases battle XP by 50%."},
    {"Leftovers Charm", "At each turn end, recover 1/16 of max HP."},
    {"Focus Band Charm", "Once per battle, survive a knockout at 1 HP."},
    {"Scope Lens Charm", "Adds 1 critical-hit bonus to each battle check."},
    {"Silph Scope", "Shows the opposing party before you choose your Pokemon."},
    {"PP Up Pouch", "Move maximum PP increases by 20%, rounded to a whole point."},
    {"Old Amber", "One use: recruit an extra Pokemon; consumed only if recruitment succeeds."},
    {"Master Ball", "One use: recruit a boss Pokemon; consumed only if recruitment succeeds."},
    {"Ghost Relic", "Ghost-type move damage is 15% higher in battle."},
    {"Cursed Power", "All damage is 10% higher; each Pokemon loses 1 HP at turn end."}
};

const char *rl_scene_relic_status(const RunState *run, unsigned relic) {
    if (!run || relic >= RL_RELIC_COUNT) return "Relic status unavailable.";
    switch (relic) {
    case RL_RAINBOW: return "Post-battle heal is active in run logic.";
    case RL_EARTH: return "One extra choice on each reward.";
    case RL_AMULET: return "Run money bonus is active.";
    case RL_EXP_SHARE:
    case RL_LUCKY_EGG: return "Battle XP bonus is active after wins.";
    case RL_SILPH_SCOPE:
        return "Roster preview is always available; no extra effect.";
    case RL_OLD_AMBER:
        if (run->consumed_relics & (1U << relic))
            return "Recruit use spent this run.";
        return "Recruit only; no passive effect; no UI yet.";
    case RL_MASTER_BALL:
        if (run->consumed_relics & (1U << relic))
            return "Recruit use spent this run.";
        return "Recruit UI missing; boss-only limit unenforced.";
    default: return "Native battle effect not connected yet.";
    }
}

RlU32 rl_scene_available_relics(RlU32 permitted) {
    return permitted & (((1U << RL_RELIC_COUNT) - 1U) &
                        ~((1U << RL_OLD_AMBER) | (1U << RL_MASTER_BALL)));
}

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

unsigned rl_relic_modify_money(RlU32 relics, unsigned base) {
    if (relics & (1U << RL_AMULET)) return base + base / 2;
    return base;
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
