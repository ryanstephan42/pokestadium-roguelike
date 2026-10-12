#ifndef SC_BATTLE_SERVICE_H
#define SC_BATTLE_SERVICE_H
#include "sc_ai_overlay.h"
#include "sc_module_arena.h"
#include "sc_batl_scope.h"
#include "host_tests/story_private_rom_native.h"

/* Default-off private service. No pointers are exposed to campaign/UI state. */
typedef struct ScServiceLifecycle {
    u32 starts, ready, drains, revokes, retires;
    u32 scope_generation, module_generation, pending, active, failed;
} ScServiceLifecycle;
extern volatile ScServiceLifecycle gScServiceLifecycle;
int sc_service_start(ScMaArena *, OSPiHandle *);
int sc_service_bootstrap(void);
/* One native arena shared by campaign backing and the callable service. */
ScMaArena *sc_service_native_arena(void);
int sc_service_poll(int deadline_expired);
int sc_service_prepare(unk_D_84390010 *, unsigned domain, const u8 mapping[6],
                       unsigned count, ScAiSelection *);
int sc_service_accept(unk_D_84390010 *, unsigned route);
int sc_service_reject(unk_D_84390010 *, unsigned route);
int sc_service_struggle(unk_D_84390010 *);
int sc_service_stop_poll(void);
void sc_service_require(int);
int sc_service_pending(void);

#endif
