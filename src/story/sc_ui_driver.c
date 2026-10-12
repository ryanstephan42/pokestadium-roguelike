/* PURE. Missing acquisitions/calls fail closed; no native symbols or permits. */
#include "sc_ui_driver.h"

static int failure(ScUiDriver *driver, int status, const char *message) {
    if (driver && driver->ui) {
        driver->ui->status = status;
        driver->ui->message = message;
    }
    return status;
}

int sc_ui_driver_bind(ScUiDriver *driver, ScUi *prepared_ui,
                      ScUiBattleRequest *request_backing, const ScUiDriverCalls *calls) {
    if (!driver) return SC_INVALID;
    if (driver->bound)
        return failure(driver, SC_INVALID, "Bound story UI driver cannot be rebound");
    driver->bound = driver->launch_pending = driver->launched = driver->exit_requested = 0;
    driver->operation_id = 0;
    driver->ui = prepared_ui;
    driver->request = request_backing;
    if (!prepared_ui || !request_backing || !calls || !calls->launch || !calls->draw || !calls->leave)
        return failure(driver, SC_UNAVAILABLE, "Story UI acquisition/callback binding is unavailable");
    if (!prepared_ui->catalog || !prepared_ui->catalog->graph_reviewed ||
        !prepared_ui->device || !prepared_ui->device->open || !prepared_ui->device->read ||
        !prepared_ui->device->write || !rl_ui_list_valid(&prepared_ui->list) ||
        prepared_ui->request_ready || prepared_ui->pending_save)
        return failure(driver, SC_INVALID, "Story UI backing/catalog/save state is not ready to bind");
    driver->calls = *calls;
    driver->bound = 1;
    return SC_OK;
}

int sc_ui_driver_retire(ScUiDriver *driver, const ScUi *ui, const ScUiBattleRequest *request) {
    if (!driver || !driver->bound || !ui || driver->ui != ui || driver->request != request)
        return SC_INVALID;
    if (!driver->exit_requested || driver->launched || driver->launch_pending ||
        ui->request_ready || ui->pending_save)
        return SC_PENDING;
    driver->bound = driver->launch_pending = driver->launched = driver->exit_requested = 0;
    driver->operation_id = 0;
    driver->ui = 0;
    driver->request = 0;
    driver->calls.context = 0;
    driver->calls.launch = 0;
    driver->calls.draw = 0;
    driver->calls.leave = 0;
    return SC_OK;
}

static int launch(ScUiDriver *driver) {
    int status;
    if (!driver->ui->request_ready || driver->ui->state.phase != SC_BATTLE)
        return failure(driver, SC_INVALID, "Story battle request changed before launch");
    status = sc_ui_battle_request(driver->ui, driver->request);
    if (status != SC_OK) return failure(driver, status, "Cannot copy story battle request");
    if (driver->operation_id && driver->operation_id != driver->request->operation_id)
        return failure(driver, SC_INVALID, "Pending story battle operation changed");
    driver->operation_id = driver->request->operation_id;
    status = driver->calls.launch(driver->calls.context, driver->request);
    if (status != SC_OK) {
        if (status < SC_INVALID || status > SC_UNREVIEWED) status = SC_INVALID;
        return failure(driver, status, "Battle launch refused; A retries unchanged checkpointed request");
    }
    driver->launch_pending = 0;
    driver->launched = 1;
    return SC_OK;
}

int sc_ui_driver_step(ScUiDriver *driver, unsigned pressed) {
    int result, status = SC_OK;
    if (!driver || !driver->bound || driver->exit_requested)
        return failure(driver, SC_UNAVAILABLE, "Story UI driver is not bound or already exited");
    if (driver->launch_pending) {
        if (pressed & RL_UI_B)
            status = failure(driver, SC_PENDING, "Checkpointed launch remains pending; retry with A");
        else if (!(pressed & (RL_UI_UP | RL_UI_DOWN | RL_UI_LEFT | RL_UI_RIGHT)) &&
                 (pressed & RL_UI_A))
            status = launch(driver);
    } else {
        result = sc_ui_input(driver->ui, pressed);
        if (result == SC_UI_EXIT) {
            if (driver->launched || driver->ui->pending_save || driver->ui->request_ready)
                return failure(driver, SC_PENDING, "Cannot leave an unresolved campaign transaction");
            driver->exit_requested = 1;
            driver->calls.leave(driver->calls.context);
        } else if (result == SC_UI_REQUEST) {
            driver->operation_id = 0;
            driver->launch_pending = 1;
            status = launch(driver);
        }
    }
    if (!driver->exit_requested) driver->calls.draw(driver->calls.context, driver->ui);
    return status;
}

int sc_ui_driver_observe(ScUiDriver *driver, RlU32 operation_id,
                         const ScBattleSnapshot party[6], unsigned active_slot,
                         unsigned enemy_slot, unsigned enemy_hp) {
    if (!driver || !driver->bound || !driver->launched ||
        operation_id != driver->operation_id)
        return failure(driver, SC_INVALID, "Snapshot does not belong to the launched story operation");
    return sc_ui_observe(driver->ui, operation_id, party, active_slot, enemy_slot, enemy_hp);
}

int sc_ui_driver_finish(ScUiDriver *driver, RlU32 operation_id, unsigned outcome) {
    int status;
    if (!driver || !driver->bound || !driver->launched ||
        operation_id != driver->operation_id)
        return failure(driver, SC_INVALID, "Result does not belong to the launched story operation");
    status = sc_ui_finish(driver->ui, operation_id, outcome);
    if (!driver->ui->request_ready) driver->launched = 0;
    return status;
}
