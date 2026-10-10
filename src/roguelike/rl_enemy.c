/* PURE */
#include "rl_core.h"
#include "rl_content.h"

static const RlMon placeholder[] = {
    {1,30,RL_MEDIUM_SLOW,0,21760,0x8888,{0,0,0,0,0},{45,49,49,45,65},
     {0,0,0,0,0},
     {33,45,73,22},{35,40,10,25},{35,40,10,25},67,67,0,RL_NO_SLOT,"Bulbasaur"},
    {4,30,RL_MEDIUM_SLOW,0,21760,0x8888,{0,0,0,0,0},{39,52,43,65,50},
     {0,0,0,0,0},
     {10,45,52,43},{35,40,25,30},{35,40,25,30},63,63,0,RL_NO_SLOT,"Charmander"},
    {7,30,RL_MEDIUM_SLOW,0,21760,0x8888,{0,0,0,0,0},{44,48,65,43,50},
     {0,0,0,0,0},
     {33,39,55,44},{35,30,25,25},{35,30,25,25},66,66,0,RL_NO_SLOT,"Squirtle"},
    {25,30,RL_MEDIUM_FAST,0,27000,0x8888,{0,0,0,0,0},{35,55,30,90,50},
     {0,0,0,0,0},
     {84,45,98,86},{30,40,30,20},{30,40,30,20},61,61,0,RL_NO_SLOT,"Pikachu"}
};
const RlPool rl_placeholder_pool = { placeholder, 4, 0 };

unsigned rl_enemy_level(unsigned act, unsigned row, unsigned type, unsigned ascension) {
    static const unsigned bases[4] = {30,45,60,75};
    unsigned level;
    RlAscension asc;
    asc = rl_ascension(ascension);
    if (act > 3) act = 3;
    if (row > 6) row = 6;
    level = bases[act] + row * 2 + asc.level_bonus;
    if (type == RL_ELITE) level += 3;
    if (type == RL_BOSS) level += 5;
    return level > 100 ? 100 : level;
}

int rl_enemy_scale(const RunState *run, unsigned type, RlMon *team,
                   unsigned count) {
    unsigned i, j, dv, level;
    RlAscension asc;
    if (!run || !team || run->act > 3 ||
        !count || count > RL_PARTY || run->row >= rl_map_rows(run->act) ||
        (type != RL_BATTLE && type != RL_ELITE && type != RL_BOSS)) return RL_INVALID;
    for (i = 0; i < count; i++)
        if (!team[i].species || team[i].species > 151 ||
            team[i].growth > RL_MEDIUM_SLOW) return RL_INVALID;
    asc = rl_ascension(run->ascension);
    level = rl_enemy_level(run->act, run->row, type, run->ascension);
    dv = 7 + run->act * 2 + run->row / 2 + asc.enemy_dv_bonus;
    if (type == RL_ELITE || type == RL_BOSS || dv > 15) dv = 15;
    /* Even nibbles reset the derived HP DV; keep ascension HP monotonic. */
    dv |= 1U;
    for (i = 0; i < count; i++) {
        team[i].level = (RlU8)level;
        team[i].exp = rl_xp_for_level(level, team[i].growth);
        team[i].dvs = (RlU16)(dv * 0x1111U);
        for (j = 0; j < 5; j++)
            team[i].stat_exp[j] = (RlU16)((run->act * 7 + run->row + 1) * 2000);
        rl_mon_recalculate_stats(&team[i], 0);
        team[i].cur_hp = team[i].max_hp;
        team[i].fainted = 0;
        team[i].status = 0;
        for (j = 0; j < 4; j++) team[i].pp[j] = team[i].max_pp[j];
    }
    return RL_OK;
}

int rl_enemy_generate(RunState *run, const RlPool *pool, unsigned type,
                      RlMon *team, unsigned count) {
    unsigned i, index;
    if (!run || !pool || !pool->mons || !pool->count || !team ||
        !count || count > RL_PARTY || run->act > 3 ||
        run->row >= rl_map_rows(run->act) ||
        (type != RL_BATTLE && type != RL_ELITE && type != RL_BOSS)) return RL_INVALID;
    for (i = 0; i < pool->count; i++)
        if (!pool->mons[i].species || pool->mons[i].species > 151 ||
            pool->mons[i].growth > RL_MEDIUM_SLOW) return RL_INVALID;
    for (i = 0; i < count; i++) {
        index = pool->fixed_order ? i % pool->count :
            rl_random_bounded(&run->rng, pool->count);
        team[i] = pool->mons[index];
    }
    return rl_enemy_scale(run, type, team, count);
}
