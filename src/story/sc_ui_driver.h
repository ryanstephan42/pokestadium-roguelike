#ifndef SC_UI_DRIVER_H
#define SC_UI_DRIVER_H

#include "sc_ui.h"

/* Injected calls are not admission authority. The native owner acquires backing,
   catalog, Pak and rendering resources before binding this source-only adapter.
   Driver backing must be zero-initialized; it may be bound again only after
   sc_ui_driver_retire has unbound an exited, drained driver. */
typedef struct ScUiDriverCalls {
    void *context;
    int (*launch)(void *context, const ScUiBattleRequest *request);
    void (*draw)(void *context, const ScUi *ui);
    void (*leave)(void *context);
} ScUiDriverCalls;

typedef struct ScUiDriver {
    ScUi *ui;
    ScUiBattleRequest *request;
    ScUiDriverCalls calls;
    RlU32 operation_id;
    int bound, launch_pending, launched, exit_requested;
} ScUiDriver;

int sc_ui_driver_bind(ScUiDriver *driver, ScUi *prepared_ui,
                      ScUiBattleRequest *request_backing, const ScUiDriverCalls *calls);
/* Ends the presentation transaction after the caller has copied the request.
   The request remains valid in caller-owned storage; this clears UI launch
   ownership so its presentation shell can retire before the battle starts. */
int sc_ui_driver_detach_battle(ScUiDriver *driver);
/* Unbinds only an exited driver with no launch, request or save in flight;
   SC_PENDING otherwise, SC_INVALID on identity mismatch. */
int sc_ui_driver_retire(ScUiDriver *driver, const ScUi *ui, const ScUiBattleRequest *request);
int sc_ui_driver_step(ScUiDriver *driver, unsigned pressed);
int sc_ui_driver_observe(ScUiDriver *driver, RlU32 operation_id,
                         const ScBattleSnapshot party[6], unsigned active_slot,
                         unsigned enemy_slot, unsigned enemy_hp);
int sc_ui_driver_finish(ScUiDriver *driver, RlU32 operation_id, unsigned outcome);

#endif
