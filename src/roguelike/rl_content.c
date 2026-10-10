/* PURE */
#include "rl_content.h"

int rl_encounter_select(const RunState *run, RlEncounter *encounter) {
    RlU32 rng;
    unsigned type, roster, trainer;
    if (!run || !encounter || run->act > 3 || run->ascension > 10 ||
        run->row >= rl_map_rows(run->act) ||
        run->lane >= run->map.lanes[run->row]) return RL_INVALID;
    type = run->map.nodes[run->row][run->lane].type;
    if (type != RL_BATTLE && type != RL_ELITE && type != RL_BOSS)
        return RL_INVALID;
    rng = run->seed ^ (0x9e3779b9U * (1U + run->act)) ^
        (0x85ebca6bU * (1U + run->row)) ^
        (0xc2b2ae35U * (1U + run->lane)) ^ run->ascension;
    if (run->act == 3) {
        if (type != RL_BOSS) return RL_INVALID;
        roster = RL_ROSTER_MEWTWO;
        trainer = 0;
    } else if (type == RL_BOSS && run->act == 2) {
        roster = RL_ROSTER_RIVAL;
        trainer = rl_random_bounded(&rng, 7);
    } else if ((type == RL_ELITE && run->act == 2) ||
               (type == RL_BOSS && run->act == 1 && (run->lane & 1))) {
        roster = RL_ROSTER_ELITE_FOUR;
        trainer = rl_random_bounded(&rng, 4);
    } else if (type == RL_BOSS) {
        roster = RL_ROSTER_PEWTER + rl_random_bounded(&rng, 8);
        trainer = 3;
    } else {
        roster = run->act ? RL_ROSTER_PRIME_POKE : RL_ROSTER_POKE_POKE;
        roster += type == RL_ELITE ? 3U : run->row / 2;
        trainer = rl_random_bounded(&rng, 8);
    }
    encounter->roster = (RlU8)roster;
    encounter->trainer = (RlU8)trainer;
    encounter->round = (RlU8)(run->ascension >= 8 ? 2 : 1);
    encounter->type = (RlU8)type;
    return RL_OK;
}

unsigned rl_battle_money(unsigned act, unsigned row, unsigned type,
                         unsigned ascension, RlU32 relics) {
    unsigned amount;
    RlAscension asc;
    if (act > 3 || row >= rl_map_rows(act) ||
        (type != RL_BATTLE && type != RL_ELITE && type != RL_BOSS)) return 0;
    asc = rl_ascension(ascension);
    amount = 100 + act * 75 + row * 15;
    if (type == RL_ELITE) amount *= 2;
    if (type == RL_BOSS) amount *= 3;
    amount = amount * asc.money_percent / 100;
    return rl_relic_modify_money(relics, amount);
}
