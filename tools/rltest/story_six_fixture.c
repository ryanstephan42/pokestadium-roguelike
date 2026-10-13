/* PURE. Prior victories are host preparation, not native battle coverage. */
#include "story_six_fixture.h"

static int same_id(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static int record(ScSixTrace trace, void *context, const char *operation,
                   unsigned index, const ScState *state, int status) {
    if (trace) trace(operation, index, state, status, context);
    return status;
}

static int checkpoint(ScState *output, ScState *state, const ScCatalog *catalog,
                      ScSixTrace trace, void *context, int transform) {
    static const char *ids[] = {
        "Route22/rival-early",
        "ViridianForest/TEXT_VIRIDIANFOREST_YOUNGSTER2",
        "ViridianForest/TEXT_VIRIDIANFOREST_YOUNGSTER3",
        "ViridianForest/TEXT_VIRIDIANFOREST_YOUNGSTER4",
        "PewterGym/TEXT_PEWTERGYM_COOLTRAINER_M",
        "PewterGym/TEXT_PEWTERGYM_BROCK"
    };
    ScOptions options = {0, SC_STANDARD, SC_STARTER_TRIO, 4};
    RlU32 seed = 42;
    unsigned encounters[6], heal = SC_NO_ENCOUNTER, i, j, variant, count, slot;
    int status;
    if (!output || !sc_catalog_valid(catalog) || !catalog->graph_reviewed ||
        !catalog->graph) return SC_INVALID;
    for (i = 0; i < 6; i++) {
        encounters[i] = SC_NO_ENCOUNTER;
        for (j = 0; j < catalog->count; j++)
            if (same_id(catalog->encounters[j].id, ids[i])) encounters[i] = j;
        if (encounters[i] == SC_NO_ENCOUNTER) return SC_INVALID;
    }
    for (i = 0; i < catalog->graph->healing_count; i++)
        if (same_id(catalog->graph->healing[i].id, "heal:ViridianPokecenter")) heal = i;
    if (heal == SC_NO_ENCOUNTER) return SC_INVALID;
    if (transform) {
        options.starter_pool = SC_ALL_SPECIES;
        options.starter = 0;
        for (seed = 1; seed < 100000U; seed++) {
            status = sc_new(state, catalog, &options, seed);
            if (status != SC_OK) return status;
            if (state->party[0].species == 132) break;
        }
        if (seed == 100000U) return SC_UNAVAILABLE;
    }
    status = sc_new(state, catalog, &options, seed);
    if (status != SC_OK) return status;
    record(trace, context, "new", seed, state, status);
    for (i = 0; i < 5; i++) {
        variant = catalog->encounters[encounters[i]].kind == SC_RIVAL ? state->rival_branch : 0;
        status = record(trace, context, "begin", encounters[i], state,
                        sc_begin(state, catalog, encounters[i], variant));
        if (status != SC_OK) return status;
        count = catalog->encounters[encounters[i]].teams[variant].count;
        for (slot = 0; slot < count; slot++) {
            status = record(trace, context, "pure-ko", slot, state,
                            sc_on_ko(state, catalog, slot, 1, variant));
            if (status != SC_OK) return status;
            for (j = 0; j < state->party_count; j++) {
                if (!(state->learn_mask & (1U << j))) continue;
                status = record(trace, context, "learn", j, state,
                                sc_learn(state, catalog, j, 3, 1));
                if (status != SC_OK) return status;
            }
        }
        status = record(trace, context, "pure-win", encounters[i], state,
                        sc_finish(state, catalog, SC_WIN, variant));
        if (status != SC_OK) return status;
        record(trace, context, "offer", state->offered_slot, state, SC_OK);
        status = record(trace, context, "recruit", state->party_count, state,
                        sc_recruit(state, catalog, RL_NO_SLOT, 1));
        if (status != SC_OK) return status;
        status = record(trace, context, "location-heal", heal, state,
                        sc_location_heal(state, catalog, heal));
        if (status != SC_OK) return status;
    }
    status = state->party_count == 6 && sc_available(state, catalog, encounters[5]) ?
        SC_OK : SC_UNAVAILABLE;
    record(trace, context, "target-available", encounters[5], state, status);
    if (status != SC_OK) return status;
    *output = *state;
    return SC_OK;
}

int sc_six_checkpoint(ScState *output, const ScCatalog *catalog,
                      ScSixTrace trace, void *context) {
#ifdef STORY_SIX_PROBE
    static ScState scratch;
    static int owned;
    int status;
    if (owned) return SC_INVALID;
    owned = 1;
    status = checkpoint(output, &scratch, catalog, trace, context, 0);
    owned = 0;
    return status;
#else
    ScState scratch;
    return checkpoint(output, &scratch, catalog, trace, context, 0);
#endif
}

#ifndef STORY_SIX_PROBE
int sc_six_transform_checkpoint(ScState *output, const ScCatalog *catalog,
                                ScSixTrace trace, void *context) {
    ScState scratch;
    return checkpoint(output, &scratch, catalog, trace, context, 1);
}
#endif
