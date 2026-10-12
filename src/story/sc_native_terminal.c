#if defined(STORY_NATIVE_UI) && defined(STORY_SERVICE_CANDIDATE)
#include "sc_native_terminal.h"

static int stop(ScNativeTerminal *terminal, const char *message) {
    terminal->failed = 1;
    terminal->armed = 0;
    HAL_Printf(8, 8, "%s", message);
    return SC_INVALID;
}

static int quiet(const ScNativeTerminalWitness *w) {
    return !w->active && !w->pending && !w->failed && w->scope_idle && w->pool_state &&
        w->starts == w->drains && w->drains == w->revokes && w->revokes == w->retires;
}

int sc_native_terminal_arm(ScNativeTerminal *terminal, const ScNativeOperation *operation) {
    ScNativeTerminalWitness w;
    if (!terminal) return SC_INVALID;
    if (terminal->failed || terminal->armed)
        return stop(terminal, "Story terminal: authority already armed or failed");
    if (!operation || operation->phase != SC_NATIVE_OPERATION_RUNNING ||
        !operation->operation || operation->queued)
        return stop(terminal, "Story terminal: no running operation to arm");
    sc_native_terminal_witness(&w);
    if (!quiet(&w) || w.retires == 0xffffffffU)
        return stop(terminal, "Story terminal: service or BATL scope not retired");
    terminal->pool_state = w.pool_state;
    terminal->operation = operation->operation;
    terminal->retires = w.retires;
    terminal->armed = 1;
    return SC_OK;
}

int sc_native_terminal_drain(ScNativeTerminal *terminal, ScNativeOperation *operation,
                             const ScNativeCampaignBacking *backing, unsigned player_alive,
                             unsigned enemy_alive) {
    ScNativeTerminalWitness w;
    unsigned outcome;
    int status;
    if (!terminal) return SC_INVALID;
    if (terminal->failed || !terminal->armed)
        return stop(terminal, "Story terminal: authority not armed");
    if (!operation || operation->operation != terminal->operation ||
        (operation->phase != SC_NATIVE_OPERATION_RUNNING &&
         operation->phase != SC_NATIVE_OPERATION_HELD))
        return stop(terminal, "Story terminal: operation changed");
    sc_native_terminal_witness(&w);
    if (!quiet(&w) || w.retires != terminal->retires + 1 || w.pool_state != terminal->pool_state)
        return stop(terminal, "Story terminal: retirement or pool pop unproven");
    player_alive = player_alive != 0;
    enemy_alive = enemy_alive != 0;
    outcome = player_alive == enemy_alive ? SC_DRAW : player_alive ? SC_WIN : SC_LOSS;
    status = sc_native_operation_drain(operation, backing, outcome);
    if (status == SC_PENDING) return status;
    if (status != SC_OK) return stop(terminal, "Story terminal: drain refused");
    terminal->armed = 0;
    terminal->operation = terminal->retires = 0;
    terminal->pool_state = 0;
    return SC_OK;
}

static void hook_stop(ScNativeTerminalBinding *b, const char *message) {
    b->status = b->terminal ? stop(b->terminal, message) : SC_INVALID;
}

static int hook_arm(void *context) {
    ScNativeTerminalBinding *b = context;
    if (!b || gScNativeLaunchHook != &b->hook || b->drained) {
        if (b) hook_stop(b, "Story terminal: stale launch hook");
        return SC_INVALID;
    }
    b->status = sc_native_terminal_arm(b->terminal, b->operation);
    return b->status;
}

static void hook_drain(void *context, unsigned player_alive, unsigned enemy_alive) {
    ScNativeTerminalBinding *b = context;
    if (!b) return;
    if (gScNativeLaunchHook != &b->hook || b->drained) {
        hook_stop(b, "Story terminal: stale launch hook");
        return;
    }
    b->drained = 1;
    b->player_alive = player_alive != 0;
    b->enemy_alive = enemy_alive != 0;
    b->status = sc_native_terminal_drain(b->terminal, b->operation, b->backing,
                                         b->player_alive, b->enemy_alive);
}

static void hook_abort(void *context) {
    ScNativeTerminalBinding *b = context;
    if (!b) return;
    b->drained = 1;
    hook_stop(b, "Story terminal: native battle initialization failed");
}

int sc_native_terminal_install(ScNativeTerminalBinding *b, ScNativeTerminal *terminal,
                               ScNativeOperation *operation,
                               const ScNativeCampaignBacking *backing) {
    if (!b || !terminal || !operation || !backing || gScNativeLaunchHook ||
        terminal->armed || terminal->failed)
        return SC_INVALID;
    b->hook.context = b;
    b->hook.arm = hook_arm;
    b->hook.drain = hook_drain;
    b->hook.abort = hook_abort;
    b->terminal = terminal;
    b->operation = operation;
    b->backing = backing;
    b->player_alive = b->enemy_alive = b->drained = 0;
    b->status = SC_PENDING;
    gScNativeLaunchHook = &b->hook;
    return SC_OK;
}

int sc_native_terminal_uninstall(ScNativeTerminalBinding *b) {
    if (!b || gScNativeLaunchHook != &b->hook || !b->terminal || b->terminal->armed)
        return SC_INVALID;
    gScNativeLaunchHook = 0;
    return SC_OK;
}
#endif
