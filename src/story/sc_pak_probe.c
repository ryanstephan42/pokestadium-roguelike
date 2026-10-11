#ifdef STORY_NATIVE_PROBE
#include "sc_native_pak.h"

static int native_call(unsigned controller, const ScCatalog *catalog,
                       ScSnapshot *load, const ScSnapshot *store, int create) {
    /* Owned by the synchronous, non-reentrant cold invocation. */
    static ScNativePak pak;
    int status, released;
    if (controller >= 4 || (!load && !store)) return SC_SAVE_INVALID;
    status = sc_native_pak_bind(&pak, controller);
    if (status != SC_SAVE_OK) return status;
    status = sc_native_pak_begin(&pak);
    if (status == SC_SAVE_OK) {
        if (store) status = sc_storage_store(&pak.device, catalog, store, create);
        else status = sc_storage_load(&pak.device, catalog, load);
        released = sc_native_pak_end(&pak);
        if (status == SC_SAVE_OK && released != SC_SAVE_OK) status = released;
    }
    released = sc_native_pak_release(&pak);
    if (status == SC_SAVE_OK && released != SC_SAVE_OK) status = released;
    return status;
}

int sc_pak_probe_load(unsigned controller, const ScCatalog *catalog, ScSnapshot *snapshot) {
    return native_call(controller, catalog, snapshot, NULL, 0);
}

int sc_pak_probe_store(unsigned controller, const ScCatalog *catalog,
                       const ScSnapshot *snapshot, int create) {
    return native_call(controller, catalog, NULL, snapshot, create);
}
#endif
