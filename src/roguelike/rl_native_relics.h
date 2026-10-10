#ifndef RL_NATIVE_RELICS_H
#define RL_NATIVE_RELICS_H

#include "include/rl_battle.h"
#include "rl_core.h"

#define RL_RELIC_TRACE_CAPACITY 256

typedef struct RlRelicTraceEvent {
    unsigned int kind, turn, a, b, c, d, e, f;
} RlRelicTraceEvent;

typedef struct RlRelicTrace {
    unsigned int count, dropped;
    RlRelicTraceEvent events[RL_RELIC_TRACE_CAPACITY];
} RlRelicTrace;

extern volatile RlRelicTrace gRlRelicTrace;
extern unsigned int gRlNativeRelics;

void rl_native_relics_reset(unsigned int relics);
void rl_native_relics_bind(RlBattleSide* player, RlBattleSide* enemy);
/* Returns nonzero when Speed changed, so the caller re-applies paralysis. */
int rl_native_relics_stats(RlBattleSide* side);
/* Both commands are committed. Fainted-mon replacement sends are not turns. */
void rl_native_relics_open_round(RlBattleSide* enemy);
int rl_native_relics_finish_round(unsigned int second_action);
unsigned int rl_native_relics_damage(RlBattleMon* attacker, RlBattleMon* defender,
                                      unsigned int damage, unsigned int type,
                                      int substitute);
void rl_native_relics_damage_trace(RlBattleMon* attacker, unsigned int type,
                                   unsigned int before, unsigned int modified,
                                   unsigned int hp, unsigned int committed);
unsigned int rl_native_relics_crit(RlBattleMon* attacker, unsigned int threshold);
void rl_native_relics_crit_trace(RlBattleMon* attacker, unsigned int before,
                                unsigned int after, unsigned int draw);
int rl_native_relics_block_status(RlBattleMon* target, unsigned int status,
                                  unsigned int move);
void rl_native_relics_end(unsigned int result);

#endif
