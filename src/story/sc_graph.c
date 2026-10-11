/* PURE */
#include "sc_graph.h"

static int completed(const ScState *state, unsigned index) {
    return (state->completed[index / 8] >> (index % 8)) & 1;
}

int sc_clause_open(const ScState *state, const ScClause *clause) {
    unsigned i;
    for (i = 0; i < clause->count; i++)
        if (!completed(state, clause->prior[i])) return 0;
    return 1;
}

int sc_graph_open(const ScState *state, const ScCatalog *catalog, unsigned index) {
    const ScNode *node;
    unsigned i;
    if (!catalog->graph || index >= catalog->graph->count) return 0;
    node = &catalog->graph->nodes[index];
    for (i = 0; i < node->retirement_count; i++)
        if (completed(state, node->retires_after[i])) return 0;
    for (i = 0; i < node->clause_count; i++)
        if (sc_clause_open(state, &node->clauses[i])) return 1;
    return 0;
}

static int clause_valid(const ScClause *clause, unsigned count) {
    unsigned i;
    if (clause->count > 12) return 0;
    for (i = 0; i < clause->count; i++)
        if (clause->prior[i] >= count) return 0;
    return 1;
}

int sc_graph_valid(const ScCatalog *catalog) {
    const ScGraph *graph = catalog->graph;
    unsigned i, j;
    if (!graph) return 1; /* Synthetic host fixtures use the original gate model. */
    if (!graph->nodes || !graph->healing || !graph->review_identity ||
        !graph->review_identity[0] || graph->count <= catalog->count ||
        graph->count > SC_ENCOUNTER_CAPACITY || graph->healing_count != 15 ||
        graph->fossil_action < catalog->count || graph->fossil_action >= graph->count ||
        graph->fossil_offer >= catalog->count || graph->champion >= catalog->count) return 0;
    for (i = 0; i < 4; i++) if (graph->league[i] >= catalog->count) return 0;
    for (i = 0; i < 64; i++)
        if (!((graph->review_identity[i] >= '0' && graph->review_identity[i] <= '9') ||
              (graph->review_identity[i] >= 'a' && graph->review_identity[i] <= 'f'))) return 0;
    if (graph->review_identity[64]) return 0;
    for (i = 0; i < graph->count; i++) {
        const ScNode *node = &graph->nodes[i];
        if (!node->id || !node->id[0] || !node->clause_count ||
            node->clause_count > 2 || node->retirement_count > 2 ||
            !node->variants || node->variants > 3) return 0;
        if (i < catalog->count) {
            const char *a = node->id, *b = catalog->encounters[i].id;
            while (*a && *a == *b) { a++; b++; }
            if (*a != *b || node->variants != catalog->encounters[i].variants) return 0;
        }
        for (j = 0; j < node->clause_count; j++)
            if (!clause_valid(&node->clauses[j], graph->count)) return 0;
        for (j = 0; j < node->retirement_count; j++)
            if (node->retires_after[j] >= graph->count) return 0;
    }
    for (i = 0; i < graph->healing_count; i++)
        if (!graph->healing[i].id ||
            !clause_valid(&graph->healing[i].required, graph->count) ||
            (graph->healing[i].retires_after != SC_NO_ENCOUNTER &&
             graph->healing[i].retires_after >= graph->count)) return 0;
    return 1;
}

void sc_graph_retire(ScState *state, const ScCatalog *catalog) {
    unsigned i, j;
    if (!catalog->graph) return;
    for (i = 0; i < catalog->graph->count; i++) {
        const ScNode *node = &catalog->graph->nodes[i];
        if (completed(state, i)) continue;
        for (j = 0; j < node->retirement_count; j++)
            if (completed(state, node->retires_after[j]))
                state->retired[i / 8] |= (RlU8)(1U << (i % 8));
    }
}

int sc_action(ScState *state, const ScCatalog *catalog, unsigned node,
              unsigned choice, unsigned destination_warp) {
    const ScNode *entry;
    if (!state || !catalog || !catalog->graph_reviewed || !catalog->graph ||
        node < catalog->count || node >= catalog->graph->count ||
        state->phase != SC_ROUTE) return SC_INVALID;
    entry = &catalog->graph->nodes[node];
    if (choice >= entry->variants) return SC_INVALID;
    if (completed(state, node) || !sc_graph_open(state, catalog, node) ||
        (entry->destination_warp && destination_warp != entry->destination_warp))
        return SC_UNAVAILABLE;
    if (node == catalog->graph->fossil_action) state->fossil_choice = (RlU8)choice;
    state->completed[node / 8] |= (RlU8)(1U << (node % 8));
    sc_graph_retire(state, catalog);
    return SC_OK;
}

int sc_location_heal(ScState *state, const ScCatalog *catalog, unsigned location) {
    const ScHealing *healing;
    unsigned i;
    if (!state || !catalog || !catalog->graph_reviewed || !catalog->graph ||
        location >= catalog->graph->healing_count) return SC_INVALID;
    healing = &catalog->graph->healing[location];
    if (!sc_clause_open(state, &healing->required) ||
        (healing->retires_after != SC_NO_ENCOUNTER &&
         completed(state, healing->retires_after))) return SC_UNAVAILABLE;
    if (!completed(state, catalog->graph->champion))
        for (i = 0; i < 4; i++)
            if (completed(state, catalog->graph->league[i])) return SC_UNAVAILABLE;
    return sc_apply_heal(state, location);
}
