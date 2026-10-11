#ifdef STORY_NATIVE_PROBE
#include "sc_cold.h"
#include "sc_native_probe.h"

int sc_cold_entry(int unused, ScColdRequest *request) {
    static ScSnapshot snapshot;
    ScOptions options = {0, SC_STANDARD, SC_STARTER_TRIO, 1};
    ScState *state;
    unsigned i, lab = SC_NO_ENCOUNTER, count;
    int status;
    (void)unused;
    if (!request || !request->state || !sc_catalog_valid(&sc_catalog)) {
        osSyncPrintf("story probe: cold request/catalog validation failed\n");
        return 0x100 + SC_INVALID;
    }
    state = request->state;
    if (request->command == SC_COLD_LOAD) {
        status = sc_pak_probe_load(request->controller, &sc_catalog, &snapshot);
        if (status == SC_SAVE_OK || status == SC_SAVE_RECOVERED) {
            *state = snapshot.state;
            request->interrupted = snapshot.interrupted;
        }
        return status;
    }
    if (request->command == SC_COLD_COMMIT) {
        if (request->outcome > SC_WIN) return 0x100 + SC_INVALID;
        snapshot.state = *state;
        snapshot.interrupted = 0;
        status = sc_finish(&snapshot.state, &sc_catalog, request->outcome, state->encounter_variant);
        if (status != SC_OK) return 0x100 + status;
        status = sc_pak_probe_store(request->controller, &sc_catalog, &snapshot, 0);
        if (status == SC_SAVE_OK) *state = snapshot.state;
        return status;
    }
    if (request->command != SC_COLD_NEW && request->command != SC_COLD_PREPARE &&
        request->command != SC_COLD_CHECKPOINT)
        return 0x100 + SC_INVALID;
    if (request->command == SC_COLD_PREPARE && (!request->combat || !request->opponents))
        return 0x100 + SC_INVALID;
    status = sc_new(&snapshot.state, &sc_catalog, &options, request->seed);
    if (status != SC_OK) return 0x100 + status;
    if (request->command != SC_COLD_NEW) {
        snapshot.state.party[0].dvs = 0x8889;
        snapshot.state.party[0].level = 6;
        snapshot.state.party[0].exp = sc_xp(7, snapshot.state.party[0].growth) - 1;
        sc_recalculate(&snapshot.state.party[0], 0);
        if (request->command == SC_COLD_CHECKPOINT) {
            snapshot.state.party[0].cur_hp = 17;
            snapshot.state.party[0].pp[0] = 7;
            snapshot.state.party[0].status = 4;
        }
    }
    snapshot.interrupted = request->command != SC_COLD_NEW;
    status = sc_pak_probe_store(request->controller, &sc_catalog, &snapshot, request->create);
    if (status != SC_SAVE_OK) return status;
    if (request->command != SC_COLD_PREPARE) {
        *state = snapshot.state;
        return SC_SAVE_OK;
    }
    for (i = 0; i < sc_catalog.count; i++) {
        const char *a = sc_encounters[i].id, *b = "OaksLab/rival-lab";
        while (*a && *a == *b) { a++; b++; }
        if (*a == *b) lab = i;
    }
    if (sc_begin(&snapshot.state, &sc_catalog, lab, snapshot.state.rival_branch) != SC_OK ||
        sc_team(&snapshot.state, &sc_catalog, lab, snapshot.state.rival_branch,
                 request->opponents, &count) != SC_OK ||
        sc_combat_prepare(request->combat, &snapshot.state, &sc_catalog) != SC_OK) {
        osSyncPrintf("story probe: cold combat preparation failed\n");
        return 0x100 + SC_INVALID;
    }
    request->enemy_count = count;
    *state = snapshot.state;
    return SC_SAVE_OK;
}
#endif
