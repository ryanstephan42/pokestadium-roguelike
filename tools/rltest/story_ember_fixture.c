/* PURE. Prior victories are host fixture preparation, not native battle coverage. */
#include "story_ember_fixture.h"

static int same_id(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static unsigned encounter(const ScCatalog *catalog, const char *id) {
    unsigned i;
    for (i = 0; i < catalog->count; i++)
        if (same_id(catalog->encounters[i].id, id)) return i;
    return SC_NO_ENCOUNTER;
}

static int record(ScEmberTrace trace, void *context, const char *operation,
                   unsigned index, const ScState *state, int status) {
    if (trace) trace(operation, index, state, status, context);
    return status;
}

static int victory(ScState *state, const ScCatalog *catalog, unsigned index,
                    ScEmberTrace trace, void *context) {
    unsigned slot, count, variant;
    int status;
    if (index >= catalog->count) return SC_INVALID;
    variant = catalog->encounters[index].kind == SC_RIVAL ? state->rival_branch : 0;
    status = record(trace, context, "begin", index, state,
                    sc_begin(state, catalog, index, variant));
    if (status != SC_OK) return status;
    count = catalog->encounters[index].teams[variant].count;
    for (slot = 0; slot < count; slot++) {
        status = record(trace, context, "pure-ko", slot, state,
                        sc_on_ko(state, catalog, slot, 1, variant));
        if (status != SC_OK) return status;
        /* This route must reach its first learning prompt only in the target battle. */
        if (state->learn_mask || state->phase == SC_LEARNING) return SC_PENDING;
    }
    status = record(trace, context, "pure-win", index, state,
                    sc_finish(state, catalog, SC_WIN, variant));
    if (status != SC_OK) return status;
    return record(trace, context, "decline", index, state, sc_decline(state, catalog));
}

int sc_ember_checkpoint(ScState *output, const ScCatalog *catalog,
                        ScEmberTrace trace, void *context) {
    ScOptions options = {0, SC_STANDARD, SC_STARTER_TRIO, 4};
    ScState state;
    unsigned lab, forest, target, healing, i;
    int status;
    if (!output || !sc_catalog_valid(catalog) || !catalog->graph_reviewed ||
        !catalog->graph) return SC_INVALID;
    lab = encounter(catalog, "OaksLab/rival-lab");
    forest = encounter(catalog, "ViridianForest/TEXT_VIRIDIANFOREST_YOUNGSTER2");
    target = encounter(catalog, "ViridianForest/TEXT_VIRIDIANFOREST_YOUNGSTER4");
    healing = SC_NO_ENCOUNTER;
    for (i = 0; i < catalog->graph->healing_count; i++)
        if (same_id(catalog->graph->healing[i].id, "heal:ViridianPokecenter"))
            healing = i;
    if (lab == SC_NO_ENCOUNTER || forest == SC_NO_ENCOUNTER ||
        target == SC_NO_ENCOUNTER || healing == SC_NO_ENCOUNTER) return SC_INVALID;
    status = sc_new(&state, catalog, &options, 42);
    if (status != SC_OK) return status;
    record(trace, context, "new", 42, &state, status);
    status = victory(&state, catalog, lab, trace, context);
    if (status != SC_OK) return status;
    status = record(trace, context, "location-heal", healing, &state,
                    sc_location_heal(&state, catalog, healing));
    if (status != SC_OK) return status;
    status = victory(&state, catalog, forest, trace, context);
    if (status != SC_OK) return status;
    status = record(trace, context, "location-heal", healing, &state,
                    sc_location_heal(&state, catalog, healing));
    if (status != SC_OK) return status;
    status = sc_available(&state, catalog, target) ? SC_OK : SC_UNAVAILABLE;
    record(trace, context, "target-available", target, &state, status);
    if (status != SC_OK) return status;
    *output = state;
    return SC_OK;
}
