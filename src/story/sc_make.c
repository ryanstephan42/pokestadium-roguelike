/* PURE. Team materialization runs only while the cold catalog is owned. */
#include "sc_core.h"

int sc_make_mon(const ScCatalog *catalog, const ScTemplate *input,
                 RlU16 random_dvs, RlMon *output) {
    const ScSpecies *species;
    RlMon mon;
    unsigned i, j, move, empty;
    unsigned char *bytes = (unsigned char *)&mon;
    if (!catalog || !catalog->species || !catalog->pp || !input || !output ||
        input->species < 1 || input->species > 151 ||
        input->level < 1 || input->level > 100 || input->fixed_dvs > 1)
        return SC_INVALID;
    species = &catalog->species[input->species];
    if (species->growth > RL_MEDIUM_SLOW || species->learn_count > SC_LEARN_CAPACITY)
        return SC_INVALID;
    for (i = 0; i < sizeof(mon); i++) bytes[i] = 0;
    mon.species = input->species;
    mon.level = input->level;
    mon.growth = species->growth;
    mon.dvs = input->fixed_dvs ? input->dvs : random_dvs;
    mon.held_relic = RL_NO_SLOT;
    mon.exp = sc_xp(mon.level, mon.growth);
    for (i = 0; i < 5; i++) mon.base_stats[i] = species->base[i];
#ifdef SC_STORY_PACKED_MOVES
    if (input->moves[0] || input->moves[1] || input->moves[2] || input->moves[3]) {
#else
    if (input->moves[0]) {
#endif
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
#ifdef SC_STORY_PACKED_MOVES
    sc_compact_moves(&mon);
#endif
    if (!mon.moves[0]) return SC_INVALID;
    sc_recalculate(&mon, 0);
    *output = mon;
    return SC_OK;
}
