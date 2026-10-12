#ifndef SC_NATIVE_TERMINAL_H
#define SC_NATIVE_TERMINAL_H
#if defined(STORY_NATIVE_UI) && defined(STORY_SERVICE_CANDIDATE)
#include "sc_native_operation.h"

/* Native retirement facts, read by sc_native_terminal_witness from the actual
   service lifecycle, BATL scope and main-pool state; never caller supplied. */
typedef struct ScNativeTerminalWitness {
    RlU32 starts, drains, revokes, retires;
    unsigned active, pending, failed, scope_idle;
    const void *pool_state;
} ScNativeTerminalWitness;

typedef struct ScNativeTerminal {
    const void *pool_state;
    RlU32 operation, retires;
    unsigned armed, failed;
} ScNativeTerminal;

void sc_native_terminal_witness(ScNativeTerminalWitness *);
/* arm before the battle pool push: the operation is RUNNING, no service is
   live and the BATL scope is idle. drain after the launcher pops its pool
   state: exactly one more service start/drain/revoke/retire completed, the
   scope is idle again and the pool state equals the armed state. The outcome
   is derived from the raw per-side alive flags (equal -> DRAW). PENDING keeps
   the authority armed; any refusal fail-stops it. */
int sc_native_terminal_arm(ScNativeTerminal *, const ScNativeOperation *);
int sc_native_terminal_drain(ScNativeTerminal *, ScNativeOperation *,
                             const ScNativeCampaignBacking *, unsigned player_alive,
                             unsigned enemy_alive);
#endif
#endif
