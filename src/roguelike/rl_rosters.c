#ifdef ROGUELIKE
#include "src/roguelike/rl_rosters.h"
#include "src/roguelike/rl_battle.h"
#include "src/roguelike/rl_content.h"
#include "global.h"
#define RL_ROSTERS_DEFINE
#include "rl_gen/rl_rosters.h"

static const unsigned char rl_encounter_sets[2][RL_ROSTER_COUNT] = {
    { RL_TRAINER_SET_POKE_POKE, RL_TRAINER_SET_POKE_GREAT,
      RL_TRAINER_SET_POKE_ULTRA, RL_TRAINER_SET_POKE_MASTER,
      RL_TRAINER_SET_PRIME_POKE, RL_TRAINER_SET_PRIME_GREAT,
      RL_TRAINER_SET_PRIME_ULTRA, RL_TRAINER_SET_PRIME_MASTER,
      RL_TRAINER_SET_PEWTER, RL_TRAINER_SET_CERULEAN,
      RL_TRAINER_SET_VERMILION, RL_TRAINER_SET_CELADON,
      RL_TRAINER_SET_FUCHSIA, RL_TRAINER_SET_SAFFRON,
      RL_TRAINER_SET_CINNABAR, RL_TRAINER_SET_VIRIDIAN,
      RL_TRAINER_SET_ELITE_FOUR, RL_TRAINER_SET_RIVAL, RL_TRAINER_SET_MEWTWO },
    { RL_TRAINER_SET_POKE_POKE_R2, RL_TRAINER_SET_POKE_GREAT_R2,
      RL_TRAINER_SET_POKE_ULTRA_R2, RL_TRAINER_SET_POKE_MASTER_R2,
      RL_TRAINER_SET_PRIME_POKE_R2, RL_TRAINER_SET_PRIME_GREAT_R2,
      RL_TRAINER_SET_PRIME_ULTRA_R2, RL_TRAINER_SET_PRIME_MASTER_R2,
      RL_TRAINER_SET_PEWTER_R2, RL_TRAINER_SET_CERULEAN_R2,
      RL_TRAINER_SET_VERMILION_R2, RL_TRAINER_SET_CELADON_R2,
      RL_TRAINER_SET_FUCHSIA_R2, RL_TRAINER_SET_SAFFRON_R2,
      RL_TRAINER_SET_CINNABAR_R2, RL_TRAINER_SET_VIRIDIAN_R2,
      RL_TRAINER_SET_ELITE_FOUR_R2, RL_TRAINER_SET_RIVAL_R2, RL_TRAINER_SET_MEWTWO_R2 }
};

static const unsigned char rl_meta_rental_sets[RL_META_RENTAL_SOURCE_COUNT] = {
    RL_RENTAL_SET_POKE, RL_RENTAL_SET_PRIME,
    RL_RENTAL_SET_PETIT, RL_RENTAL_SET_PRIME_R2
};

int rl_load_encounter(const RunState* run, RlMon* output, int capacity,
                      unsigned char* ai) {
    RlEncounter encounter;
    int count;
    if (rl_encounter_select(run, &encounter) != RL_OK) {
        osSyncPrintf("rl_load_encounter: invalid map encounter\n");
        return -1;
    }
    count = rl_load_trainer(
        rl_encounter_sets[encounter.round - 1][encounter.roster],
        encounter.trainer, output, capacity, ai);
    if (count < 0) return -1;
    if (rl_enemy_scale(run, encounter.type, output, count) != RL_OK) {
        osSyncPrintf("rl_load_encounter: invalid scaled team\n");
        return -1;
    }
    return count;
}

static int rl_roster_mon(const RlGenMon* source, RlMon* output) {
    int i;
    int base;

    bzero(output, sizeof(*output));
    output->held_relic = RL_NO_SLOT;
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

int rl_load_meta_rental_catalog(unsigned int source,
                                RlMetaRentalLoadout* output, int capacity) {
    unsigned int i, set;
    const RlGenRentalSet* pool;

    if (source >= RL_META_RENTAL_SOURCE_COUNT || output == NULL) {
        osSyncPrintf("rl_load_meta_rental_catalog: invalid source/output\n");
        return -1;
    }
    set = rl_meta_rental_sets[source];
    pool = &gRlRentalSets[set];
    if (capacity < pool->count) {
        osSyncPrintf("rl_load_meta_rental_catalog: insufficient capacity\n");
        return -1;
    }
    for (i = 0; i < pool->count; i++) {
        output[i].species = pool->mons[i].species;
        output[i].source = (unsigned char)source;
        output[i].moves[0] = pool->mons[i].moves[0];
        output[i].moves[1] = pool->mons[i].moves[1];
        output[i].moves[2] = pool->mons[i].moves[2];
        output[i].moves[3] = pool->mons[i].moves[3];
    }
    return pool->count;
}

int rl_trainer_count(unsigned int set) {
    if (set >= RL_NUM_TRAINER_SETS) {
        osSyncPrintf("rl_trainer_count: invalid set\n");
        return -1;
    }
    return gRlTrainerSets[set].count;
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
