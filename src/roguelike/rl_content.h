#ifndef RL_CONTENT_H
#define RL_CONTENT_H

#include "rl_core.h"

enum RlRoster {
    RL_ROSTER_POKE_POKE, RL_ROSTER_POKE_GREAT, RL_ROSTER_POKE_ULTRA,
    RL_ROSTER_POKE_MASTER, RL_ROSTER_PRIME_POKE, RL_ROSTER_PRIME_GREAT,
    RL_ROSTER_PRIME_ULTRA, RL_ROSTER_PRIME_MASTER,
    RL_ROSTER_PEWTER, RL_ROSTER_CERULEAN, RL_ROSTER_VERMILION,
    RL_ROSTER_CELADON, RL_ROSTER_FUCHSIA, RL_ROSTER_SAFFRON,
    RL_ROSTER_CINNABAR, RL_ROSTER_VIRIDIAN, RL_ROSTER_ELITE_FOUR,
    RL_ROSTER_RIVAL, RL_ROSTER_MEWTWO, RL_ROSTER_COUNT
};

typedef struct RlEncounter {
    RlU8 roster, trainer, round, type;
} RlEncounter;

/* Selection is node-local: repeat queries and suspend/resume consume no RNG. */
int rl_encounter_select(const RunState *run, RlEncounter *encounter);
unsigned rl_battle_money(unsigned act, unsigned row, unsigned type,
                         unsigned ascension, RlU32 relics);
int rl_enemy_scale(const RunState *run, unsigned type, RlMon *team,
                   unsigned count);

#endif
