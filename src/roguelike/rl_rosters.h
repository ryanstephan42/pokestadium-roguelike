#ifndef RL_ROSTERS_H
#define RL_ROSTERS_H

#include "src/roguelike/rl_core.h"

/* Returns mon count, or -1 (logged) for invalid input/insufficient capacity.
 * Include build/rl_gen/rl_rosters.h for generated set IDs and set metadata. */
int rl_load_rental_pool(unsigned int set, RlMon* output, int capacity);
int rl_load_trainer(unsigned int set, unsigned int trainer, RlMon* output,
                    int capacity, unsigned char* ai);

#endif
