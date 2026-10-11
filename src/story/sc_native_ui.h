#ifndef SC_NATIVE_UI_H
#define SC_NATIVE_UI_H

#ifdef STORY_NATIVE_UI
#include "sc_native_pak.h"
#include "sc_ui_driver.h"
#include "src/memory.h"

typedef struct ScNativeUi {
    MainPoolState *resource_scope;
    ScNativePak *pak;
    ScUi *prepared;
    int acquired;
} ScNativeUi;

/* Resource scope only: never allocates campaign/controller/request backing. */
int sc_native_ui_acquire(ScNativeUi *native, ScNativePak *bound_pak);
int sc_native_ui_prepare(ScNativeUi *native, ScUi *backing, const ScCatalog *catalog, RlU32 seed);
int sc_native_ui_draw(ScNativeUi *native, const ScUi *ui);
int sc_native_ui_input(ScNativeUi *native, ScUiDriver *driver);
int sc_native_ui_finish(ScNativeUi *native, ScUiDriver *driver, RlU32 operation_id, unsigned outcome);
/* Owner must fence submitted display tasks before declaring tasks_retired. */
int sc_native_ui_release(ScNativeUi *native, const ScUiDriver *driver, int tasks_retired);
#endif

#endif
