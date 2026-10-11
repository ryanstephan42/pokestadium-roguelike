/* PURE */
#include "sc_core.h"

static void zero_mon(RlMon *mon) {
    unsigned i;
    unsigned char *bytes = (unsigned char *)mon;
    for (i = 0; i < sizeof(*mon); i++) bytes[i] = 0;
}

RlU32 sc_xp(unsigned level, unsigned growth) {
    RlS32 n, result;
    if (level < 1 || level > 100 || growth > RL_MEDIUM_SLOW) return 0;
    n = (RlS32)level;
    if (growth == RL_FAST) return (RlU32)(4 * n * n * n / 5);
    if (growth == RL_SLOW) return (RlU32)(5 * n * n * n / 4);
    if (growth == RL_MEDIUM_SLOW) {
        result = 6 * n * n * n / 5 - 15 * n * n + 100 * n - 140;
        /* Level-one underflow is not a supported glitch progression path. */
        return result < 0 ? 0U : (RlU32)result;
    }
    return (RlU32)(n * n * n);
}

unsigned sc_stat(const RlMon *mon, unsigned stat) {
    unsigned dv, root, value;
    if (!mon || stat >= 5 || mon->level < 1 || mon->level > 100) return 0;
    if (stat == RL_HP) {
        /* RlMon nibble order is not GB byte order; GB HP DV weights are 8/4/2/1. */
        dv = ((mon->dvs & 1) << 3) | (((mon->dvs >> 4) & 1) << 2) |
             (((mon->dvs >> 8) & 1) << 1) | ((mon->dvs >> 12) & 1);
    } else dv = (mon->dvs >> ((stat - 1) * 4)) & 15;
    root = 1;
    while (root < 255 && root * root < mon->stat_exp[stat]) root++;
    value = (2 * (mon->base_stats[stat] + dv) + root / 4) * mon->level / 100;
    value += stat == RL_HP ? mon->level + 10 : 5;
    return value > 999 ? 999 : value;
}

void sc_recalculate(RlMon *mon, int preserve_hp) {
    unsigned i, old, missing;
    int alive;
    if (!mon) return;
    old = mon->max_hp;
    alive = mon->cur_hp != 0;
    missing = old > mon->cur_hp ? old - mon->cur_hp : 0;
    for (i = 0; i < 5; i++) mon->current_stats[i] = (RlU16)sc_stat(mon, i);
    mon->max_hp = mon->current_stats[RL_HP];
    if (!preserve_hp) mon->cur_hp = mon->max_hp;
    else if (alive) mon->cur_hp = (RlU16)(mon->max_hp > missing ? mon->max_hp - missing : 0);
    else mon->cur_hp = 0;
    mon->fainted = (RlU8)(mon->cur_hp == 0);
}

int sc_make_mon(const ScCatalog *catalog, const ScTemplate *input,
                 RlU16 random_dvs, RlMon *output) {
    const ScSpecies *species;
    RlMon mon;
    unsigned i, j, move, empty;
    if (!catalog || !catalog->species || !catalog->pp || !input || !output ||
        input->species < 1 || input->species > 151 ||
        input->level < 1 || input->level > 100 || input->fixed_dvs > 1)
        return SC_INVALID;
    species = &catalog->species[input->species];
    if (species->growth > RL_MEDIUM_SLOW || species->learn_count > SC_LEARN_CAPACITY)
        return SC_INVALID;
    zero_mon(&mon);
    mon.species = input->species;
    mon.level = input->level;
    mon.growth = species->growth;
    mon.dvs = input->fixed_dvs ? input->dvs : random_dvs;
    mon.held_relic = RL_NO_SLOT;
    mon.exp = sc_xp(mon.level, mon.growth);
    for (i = 0; i < 5; i++) mon.base_stats[i] = species->base[i];
    if (input->moves[0]) {
        for (i = 0; i < 4; i++) mon.moves[i] = input->moves[i];
    } else {
        for (i = 1; i < 4; i++) if (input->moves[i]) return SC_INVALID;
        for (i = 0; i < 4; i++) mon.moves[i] = species->initial[i];
        for (i = 0; i < species->learn_count; i++) {
            if (species->learn[i].level > mon.level) break;
            move = species->learn[i].move;
            empty = 4;
            for (j = 0; j < 4; j++) {
                if (mon.moves[j] == move) break;
                if (!mon.moves[j] && empty == 4) empty = j;
            }
            if (j != 4) continue;
            if (empty == 4) {
                for (j = 0; j < 3; j++) mon.moves[j] = mon.moves[j + 1];
                empty = 3;
            }
            mon.moves[empty] = (RlU8)move;
        }
    }
    for (i = 0; i < 4; i++) {
        move = mon.moves[i];
        if (move > 165 || (move && !catalog->pp[move])) return SC_INVALID;
        mon.pp[i] = mon.max_pp[i] = catalog->pp[move];
    }
    if (!mon.moves[0]) return SC_INVALID;
    sc_recalculate(&mon, 0);
    *output = mon;
    return SC_OK;
}

int sc_add_xp(RlMon *mon, RlU32 award, RlU8 *learn_move,
              const ScCatalog *catalog) {
    const ScSpecies *species;
    unsigned old, i, j;
    RlU32 maximum;
    if (!mon || !catalog || !catalog->species || !learn_move ||
        mon->species < 1 || mon->species > 151 || mon->level < 1 ||
        mon->level > 100 || mon->growth > RL_MEDIUM_SLOW) return SC_INVALID;
    species = &catalog->species[mon->species];
    if (species->learn_count > SC_LEARN_CAPACITY || mon->growth != species->growth)
        return SC_INVALID;
    maximum = sc_xp(100, mon->growth);
    if (mon->exp < sc_xp(mon->level, mon->growth) || mon->exp > maximum ||
        (mon->level < 100 && mon->exp >= sc_xp(mon->level + 1, mon->growth)))
        return SC_INVALID;
    *learn_move = 0;
    if (!mon->cur_hp) return SC_OK;
    old = mon->level;
    if (award > maximum - mon->exp) mon->exp = maximum;
    else mon->exp += award;
    while (mon->level < 100 && sc_xp(mon->level + 1, mon->growth) <= mon->exp)
        mon->level++;
    if (old == mon->level) return SC_OK;
    sc_recalculate(mon, 1);
    /* Red/Blue asks only about a move at the final level of a level jump. */
    for (i = 0; i < species->learn_count; i++) {
        if (species->learn[i].level != mon->level) continue;
        for (j = 0; j < 4; j++)
            if (mon->moves[j] == species->learn[i].move) return SC_OK;
        *learn_move = species->learn[i].move;
        break;
    }
    return SC_OK;
}
