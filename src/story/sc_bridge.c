/* PURE. No native pool pointers survive a call. */
#include "sc_bridge.h"

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
    ScState next;
    RlU8 participants;
    unsigned i, j;
    int status;
    if (!state || !bridge || !catalog || !party || state->party_count > 6 ||
        active_slot >= state->party_count || enemy_slot >= bridge->enemy_count ||
        bridge->enemy_count > 6 || enemy_hp > 999) return SC_INVALID;
    if (state->phase == SC_LEARNING) return SC_PENDING;
    if (state->phase != SC_BATTLE) return SC_INVALID;
    next = *state;
    for (i = 0; i < state->party_count; i++) {
        if (party[i].hp > next.party[i].max_hp) return SC_INVALID;
        for (j = 0; j < 4; j++) if (party[i].pp[j] > next.party[i].max_pp[j]) return SC_INVALID;
        next.party[i].cur_hp = party[i].hp;
        next.party[i].fainted = party[i].hp == 0;
        next.party[i].status = party[i].status;
        for (j = 0; j < 4; j++) next.party[i].pp[j] = party[i].pp[j];
    }
    participants = bridge->participants[enemy_slot] | (RlU8)(1U << active_slot);
    if (!enemy_hp) {
        status = sc_on_ko(&next, catalog, enemy_slot, participants, next.encounter_variant);
        if (status != SC_OK) return status;
    }
    bridge->participants[enemy_slot] = participants;
    *state = next;
    return SC_OK;
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
