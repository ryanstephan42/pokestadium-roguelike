#if defined(STORY_NATIVE_UI) && defined(STORY_SERVICE_CANDIDATE)
#include "sc_native_terminal.h"
#include "sc_battle_service.h"
#include "src/memory.h"

void sc_native_terminal_witness(ScNativeTerminalWitness *w) {
    MainPool *pool = main_pool_get_pool();
    w->starts = gScServiceLifecycle.starts;
    w->drains = gScServiceLifecycle.drains;
    w->revokes = gScServiceLifecycle.revokes;
    w->retires = gScServiceLifecycle.retires;
    w->active = gScServiceLifecycle.active;
    w->pending = gScServiceLifecycle.pending;
    w->failed = gScServiceLifecycle.failed;
    w->scope_idle = sc_batl_scope_map_change();
    w->pool_state = pool ? pool->mainState : 0;
}
#endif
