#ifndef RL_ROSTERS_H
#define RL_ROSTERS_H

#include "src/roguelike/rl_core.h"
#include "src/roguelike/rl_meta_content.h"

/* Returns mon count, or -1 (logged) for invalid input/insufficient capacity.
 * Include rl_gen/rl_rosters.h from the build directory for generated set IDs. */
int rl_load_rental_pool(unsigned int set, RlMon* output, int capacity);
/* Copy canonical species/moves only for a meta move-variant source. */
int rl_load_meta_rental_catalog(unsigned int source,
                                RlMetaRentalLoadout* output, int capacity);
int rl_trainer_count(unsigned int set);
int rl_load_trainer(unsigned int set, unsigned int trainer, RlMon* output,
                    int capacity, unsigned char* ai);
/* Complete source team (six for trainers, one for Stadium's VS Mewtwo).
 * Scales in place, keeps signature order/AI, and never advances run RNG. */
int rl_load_encounter(const RunState* run, RlMon* output, int capacity,
                      unsigned char* ai);

#endif
