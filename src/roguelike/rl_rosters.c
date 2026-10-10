#ifdef ROGUELIKE
#include "src/roguelike/rl_rosters.h"
#include "src/roguelike/rl_battle.h"
#include "global.h"
#define RL_ROSTERS_DEFINE
#include "build/rl_gen/rl_rosters.h"

static int rl_roster_mon(const RlGenMon* source, RlMon* output) {
    int i;
    int base;

    bzero(output, sizeof(*output));
    output->species = source->species;
    output->level = source->level;
    output->dvs = source->dvs;
    for (i = 0; i < 4; i++) {
        output->moves[i] = source->moves[i];
        if (source->moves[i] != 0) {
            base = D_80072B00[source->moves[i] - 1].unk_05;
            output->max_pp[i] = base + source->pp_ups[i] * ((base >= 40) ? 7 : base / 5);
        }
    }
    for (i = 0; i < 5; i++) {
        output->stat_exp[i] = source->stat_exp[i];
    }
    return rl_prepare_mon(output, 1);
}

int rl_load_rental_pool(unsigned int set, RlMon* output, int capacity) {
    unsigned int i;
    const RlGenRentalSet* pool;

    if ((set >= RL_NUM_RENTAL_SETS) || (output == NULL) ||
        (capacity < gRlRentalSets[set].count)) {
        osSyncPrintf("rl_load_rental_pool: invalid set/output capacity\n");
        return -1;
    }
    pool = &gRlRentalSets[set];
    for (i = 0; i < pool->count; i++) {
        if (rl_roster_mon(&pool->mons[i], &output[i]) != 0) {
            return -1;
        }
    }
    return pool->count;
}

int rl_load_trainer(unsigned int set, unsigned int trainer, RlMon* output,
                    int capacity, unsigned char* ai) {
    unsigned int i;
    const RlGenTrainer* team;

    if ((set >= RL_NUM_TRAINER_SETS) || (trainer >= gRlTrainerSets[set].count) ||
        (output == NULL) || (ai == NULL)) {
        osSyncPrintf("rl_load_trainer: invalid trainer request\n");
        return -1;
    }
    team = &gRlTrainerSets[set].trainers[trainer];
    if (capacity < team->count) {
        osSyncPrintf("rl_load_trainer: insufficient output capacity\n");
        return -1;
    }
    for (i = 0; i < team->count; i++) {
        if (rl_roster_mon(&team->mons[i], &output[i]) != 0) {
            return -1;
        }
    }
    *ai = team->ai;
    return team->count;
}
#endif
