#ifndef RL_SAVE_PROBE_H
#define RL_SAVE_PROBE_H

#include "rl_types.h"

/* Harness mailbox; command=0 is inert. Processed once at RL entry. */
typedef struct RlSaveProbe {
    RlU32 command, status, controller, create;
    RlU32 runs, wins, best_floor, suspended, seed;
} RlSaveProbe;

extern volatile RlSaveProbe gRlSaveProbe;
int rl_save_probe_poll(void);

#endif
