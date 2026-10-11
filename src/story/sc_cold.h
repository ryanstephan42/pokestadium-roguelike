#ifndef SC_COLD_H
#define SC_COLD_H

#include "sc_combat.h"

enum ScColdCommand { SC_COLD_LOAD = 1, SC_COLD_NEW, SC_COLD_PREPARE, SC_COLD_COMMIT, SC_COLD_CHECKPOINT };
typedef struct ScColdRequest {
    ScState *state;
    ScCombat *combat;
    RlMon *opponents;
    RlU32 seed;
    unsigned command, controller, create, outcome, interrupted, enemy_count;
} ScColdRequest;

int sc_cold_call(ScColdRequest *request);
int sc_cold_entry(int unused, ScColdRequest *request);

#endif
