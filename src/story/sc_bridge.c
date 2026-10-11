/* PURE. No native pool pointers survive a call. */
#include "sc_bridge.h"
#include "sc_combat.h"

int sc_bridge_begin(ScState *state, ScBridge *bridge, const ScCatalog *catalog,
                    const ScSaveDevice *device, unsigned encounter, unsigned variant,
                    int *storage_status) {
    ScState next;
    ScSnapshot checkpoint;
    unsigned i;
    int status;
    if (!state || !bridge || !catalog || !storage_status || encounter >= catalog->count)
        return SC_INVALID;
    *storage_status = SC_SAVE_OK;
    next = *state;
    status = sc_begin(&next, catalog, encounter, variant);
    if (status != SC_OK) return status;
    if (next.phase != SC_BATTLE) return SC_INVALID;
    checkpoint.state = *state;
    checkpoint.interrupted = 1;
    status = sc_storage_store(device, catalog, &checkpoint, 0);
    *storage_status = status;
    if (status != SC_SAVE_OK) return SC_PENDING;
    for (i = 0; i < 6; i++) bridge->participants[i] = 0;
    bridge->enemy_count = catalog->encounters[encounter].teams[variant].count;
    *state = next;
    return SC_OK;
}

int sc_bridge_paused(const ScState *state) {
    return state && state->phase == SC_LEARNING;
}

int sc_bridge_observe(ScState *state, ScBridge *bridge, const ScCatalog *catalog,
                      const ScBattleSnapshot party[6], unsigned active_slot,
                      unsigned enemy_slot, unsigned enemy_hp) {
    ScCombat combat;
    if (!state || !bridge || !catalog || !party || state->party_count > 6 ||
        active_slot >= state->party_count || enemy_slot >= bridge->enemy_count ||
        bridge->enemy_count > 6 || enemy_hp > 999) return SC_INVALID;
    if (state->phase == SC_LEARNING) return SC_PENDING;
    if (state->phase != SC_BATTLE) return SC_INVALID;
    if (!catalog->encounters || !catalog->species || state->encounter >= catalog->count ||
        state->encounter_variant >= 3) return SC_INVALID;
    if (sc_combat_prepare(&combat, state, catalog) != SC_OK) return SC_INVALID;
    return sc_observe_values(state, bridge, combat.party_species, combat.enemy_species,
                             &combat.team, combat.kind,
                             party, active_slot, enemy_slot, enemy_hp);
}

int sc_bridge_commit(ScState *state, const ScCatalog *catalog,
                     const ScSaveDevice *device, unsigned outcome, int *storage_status) {
    ScSnapshot next;
    int status;
    if (!state || !storage_status) return SC_INVALID;
    *storage_status = SC_SAVE_OK;
    next.state = *state;
    next.interrupted = 0;
    status = sc_finish(&next.state, catalog, outcome, state->encounter_variant);
    if (status != SC_OK) return status;
    status = sc_storage_store(device, catalog, &next, 0);
    *storage_status = status;
    if (status != SC_SAVE_OK) return SC_PENDING;
    *state = next.state;
    return SC_OK;
}
