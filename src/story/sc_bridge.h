#ifndef SC_BRIDGE_H
#define SC_BRIDGE_H

#include "sc_save.h"

typedef struct ScBattleSnapshot {
    RlU16 hp;
    RlU8 status, pp[4];
} ScBattleSnapshot;
typedef struct ScBridge {
    RlU8 participants[6], enemy_count;
} ScBridge;

int sc_bridge_begin(ScState *state, ScBridge *bridge, const ScCatalog *catalog,
                    const ScSaveDevice *device, unsigned encounter, unsigned variant,
                    int *storage_status);
int sc_bridge_observe(ScState *state, ScBridge *bridge, const ScCatalog *catalog,
                      const ScBattleSnapshot party[6], unsigned active_slot,
                      unsigned enemy_slot, unsigned enemy_hp);
int sc_bridge_commit(ScState *state, const ScCatalog *catalog,
                     const ScSaveDevice *device, unsigned outcome, int *storage_status);
int sc_bridge_paused(const ScState *state);

#endif
