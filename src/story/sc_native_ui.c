#ifdef STORY_NATIVE_UI
#include "sc_native_ui.h"
#include "sc_scene.h"
#include "src/controller.h"
#include "sc_rl_ui.h"
#include "src/stage_loader.h"

static ScNativeUi *sResources;

static int report(ScUi *ui, int status, const char *message) {
    HAL_Printf(8, 8, "%s", message);
    if (ui) { ui->status = status; ui->message = message; }
    return status;
}

static int ready(const ScNativeUi *native) {
    return native && sResources == native && native->acquired && native->pak && native->pak->bound &&
        native->pak_binding_id == native->pak->binding_id &&
        main_pool_get_pool()->mainState == native->resource_scope;
}

static int descendant(const ScNativeUi *native) {
    MainPoolState *scope;
    if (!native || sResources != native || !native->acquired || !native->pak ||
        !native->pak->bound || native->pak_binding_id != native->pak->binding_id) return 0;
    for (scope = main_pool_get_pool()->mainState; scope; scope = scope->prev)
        if (scope == native->resource_scope) return 1;
    return 0;
}

static int icons_ready(const ScUi *ui, int prefetch) {
    unsigned i, species;
    if (ui->state.party_count > 6) return 0;
    for (i = 0; i <= ui->state.party_count; i++) {
        species = i == ui->state.party_count ? ui->state.offer.species : ui->state.party[i].species;
        if (!species) continue;
        if (prefetch ? !rl_ui_story_icon_prefetch(species) : !rl_ui_story_icon_cached(species))
            return 0;
    }
    return 1;
}

int sc_native_ui_acquire(ScNativeUi *native, ScNativePak *bound_pak) {
    MainPoolState *before;
    if (!native || sResources || native->acquired || native->resource_scope || !bound_pak ||
        !bound_pak->bound || bound_pak->session)
        return report(NULL, SC_INVALID, "Story native UI: invalid resource acquisition");
    before = main_pool_get_pool()->mainState;
    main_pool_push_state(0);
    native->resource_scope = main_pool_get_pool()->mainState;
    if (native->resource_scope == before) {
        native->resource_scope = NULL;
        return report(NULL, SC_UNAVAILABLE, "Story native UI: resource scope unavailable");
    }
    if (rl_ui_init() != RL_UI_NONE) {
        main_pool_pop_state(0);
        native->resource_scope = NULL;
        return report(NULL, SC_UNAVAILABLE, "Story native UI: font/icon/name acquisition failed");
    }
    native->pak = bound_pak;
    native->pak_binding_id = bound_pak->binding_id;
    native->prepared = NULL;
    native->driver = NULL;
    native->acquired = 1;
    sResources = native;
    return SC_OK;
}

int sc_native_ui_prepare(ScNativeUi *native, ScUi *backing, const ScCatalog *catalog, RlU32 seed) {
    int status, ended;
    if (!ready(native) || !backing || native->prepared)
        return report(NULL, SC_UNAVAILABLE, "Story native UI: admitted backing/resources required");
    status = sc_native_pak_begin(native->pak);
    if (status != SC_SAVE_OK)
        return report(backing, SC_UNAVAILABLE, "Story native UI: Pak session acquisition failed");
    status = sc_ui_init(backing, catalog, &native->pak->device, seed);
    ended = sc_native_pak_end(native->pak);
    if (ended != SC_SAVE_OK)
        return report(backing, SC_INVALID, "Story native UI: Pak session release failed");
    if (status == SC_OK) native->prepared = backing;
    return status;
}

int sc_native_ui_draw(ScNativeUi *native, const ScUi *ui) {
    if (!descendant(native) || !ui || ui != native->prepared || !rl_ui_list_valid(&ui->list) ||
        !native->driver || !native->driver->bound || native->driver->ui != ui ||
        ui->device != &native->pak->device ||
        (!ready(native) && (!native->driver->launched ||
         (ui->state.phase != SC_LEARNING && ui->state.phase != SC_BATTLE))))
        return report(NULL, SC_UNAVAILABLE, "Story native UI: drawing resources unavailable");
    if (!icons_ready(ui, ready(native)))
        return report(NULL, SC_UNAVAILABLE, "Story native UI: retained icon cache unavailable");
    sc_scene_draw(ui);
    return SC_OK;
}

int sc_native_ui_input(ScNativeUi *native, ScUiDriver *driver) {
    int status, ended;
    if (!descendant(native) || !driver || !driver->bound || !gPlayer1Controller ||
        (native->driver && native->driver != driver) ||
        driver->ui != native->prepared || driver->ui->device != &native->pak->device)
        return report(driver ? driver->ui : NULL, SC_UNAVAILABLE,
                      "Story native UI: input/backing/Pak binding unavailable");
    native->driver = driver;
    if (!ready(native)) {
        if (!driver->launched || driver->launch_pending || driver->ui->pending_save ||
            (driver->ui->state.phase != SC_LEARNING && driver->ui->state.phase != SC_BATTLE) ||
            !icons_ready(driver->ui, 0))
            return report(driver->ui, SC_UNAVAILABLE, "Story native UI: child-scope learning resources unavailable");
        return sc_ui_driver_step(driver, gPlayer1Controller->buttonPressed);
    }
    if (!icons_ready(driver->ui, 1))
        return report(driver->ui, SC_UNAVAILABLE, "Story native UI: icon prefetch failed before submission");
    status = sc_native_pak_begin(native->pak);
    if (status != SC_SAVE_OK)
        return report(driver->ui, SC_UNAVAILABLE, "Story native UI: Pak session acquisition failed");
    status = sc_ui_driver_step(driver, gPlayer1Controller->buttonPressed);
    ended = sc_native_pak_end(native->pak);
    if (ended != SC_SAVE_OK)
        return report(driver->ui, SC_INVALID, "Story native UI: Pak session release failed");
    return status;
}

int sc_native_ui_finish(ScNativeUi *native, ScUiDriver *driver, RlU32 operation_id, unsigned outcome) {
    int status, ended;
    if (!ready(native) || !driver || !driver->bound ||
        native->driver != driver ||
        driver->ui != native->prepared || driver->ui->device != &native->pak->device)
        return report(driver ? driver->ui : NULL, SC_UNAVAILABLE,
                      "Story native UI: result/backing/Pak binding unavailable");
    status = sc_native_pak_begin(native->pak);
    if (status != SC_SAVE_OK)
        return report(driver->ui, SC_UNAVAILABLE, "Story native UI: Pak session acquisition failed");
    status = sc_ui_driver_finish(driver, operation_id, outcome);
    ended = sc_native_pak_end(native->pak);
    if (ended != SC_SAVE_OK)
        return report(driver->ui, SC_INVALID, "Story native UI: Pak session release failed");
    return status;
}

int sc_native_ui_release(ScNativeUi *native, const ScUiDriver *driver, int tasks_retired) {
    if (!ready(native) || native->pak->session)
        return report(NULL, SC_INVALID, "Story native UI: resource release order mismatch");
    if (driver && (native->driver != driver || !driver->bound || driver->ui != native->prepared ||
        !driver->ui || driver->ui->device != &native->pak->device))
        return report(NULL, SC_INVALID, "Story native UI: resource release identity mismatch");
    if (!tasks_retired || (native->prepared && !driver) ||
        (driver && (!driver->exit_requested ||
        driver->launched || driver->launch_pending ||
        driver->ui->request_ready || driver->ui->pending_save)))
        return report(NULL, SC_PENDING, "Story native UI: campaign/tasks/transactions have not retired");
    main_pool_pop_state(0);
    native->resource_scope = NULL;
    native->pak = NULL;
    native->prepared = NULL;
    native->driver = NULL;
    native->pak_binding_id = 0;
    native->acquired = 0;
    sResources = NULL;
    return SC_OK;
}
#endif
