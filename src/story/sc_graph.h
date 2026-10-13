#ifndef SC_GRAPH_H
#define SC_GRAPH_H

#include "sc_core.h"

int sc_graph_valid(const ScCatalog *catalog);
int sc_graph_open(const ScState *state, const ScCatalog *catalog, unsigned index);
int sc_clause_open(const ScState *state, const ScClause *clause);
void sc_graph_retire(ScState *state, const ScCatalog *catalog);
int sc_apply_heal(ScState *state, unsigned location);
int sc_stone_inventory_valid(const ScState *state, const ScCatalog *catalog);

#endif
