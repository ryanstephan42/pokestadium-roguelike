#ifndef SC_UI_DRIVER_H
#define SC_UI_DRIVER_H

#include "sc_ui.h"

/* Injected calls are not admission authority. The native owner acquires backing,
   catalog, Pak and rendering resources before binding this source-only adapter.
   Driver backing must be zero-initialized and may be bound only once. */
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
int sc_ui_driver_step(ScUiDriver *driver, unsigned pressed);
int sc_ui_driver_observe(ScUiDriver *driver, RlU32 operation_id,
                         const ScBattleSnapshot party[6], unsigned active_slot,
                         unsigned enemy_slot, unsigned enemy_hp);
int sc_ui_driver_finish(ScUiDriver *driver, RlU32 operation_id, unsigned outcome);

#endif
