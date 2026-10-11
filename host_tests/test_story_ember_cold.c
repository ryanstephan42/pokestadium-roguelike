#include <stdio.h>
#include <string.h>
#include "src/story/sc_cold.h"
#include "src/story/sc_native_probe.h"
#include "tools/rltest/story_ember_fixture.h"
#undef assert
#include <assert.h>

static RlU8 saved[SC_SAVE_SIZE];
static unsigned writes;
static int fail_store;

void osSyncPrintf(const char *format, ...) { (void)format; }

int sc_pak_probe_load(unsigned controller, const ScCatalog *catalog, ScSnapshot *snapshot) {
    RlU32 generation;
    assert(controller == 0);
    return sc_save_decode(snapshot, catalog, &generation, saved, sizeof(saved));
}

int sc_pak_probe_store(unsigned controller, const ScCatalog *catalog,
                       const ScSnapshot *snapshot, int create) {
    assert(controller == 0 && (create == 0 || create == 1));
    if (fail_store) return SC_SAVE_IO;
    writes++;
    return sc_save_encode(snapshot, catalog, writes, saved);
}

int main(void) {
    ScColdRequest request;
    ScState state, expected, before;
    ScSnapshot checkpoint;
    ScCombat combat;
    RlMon enemies[6], canonical[6];
    RlU32 generation;
    unsigned count;
    memset(&request, 0, sizeof(request));
    request.state = &state;
    request.combat = &combat;
    request.opponents = enemies;
    request.seed = 42;
    request.command = SC_COLD_PREPARE;
    request.create = 1;
    assert(sc_ember_checkpoint(&expected, &sc_catalog, NULL, NULL) == SC_OK);
    assert(sc_cold_entry(0, &request) == SC_SAVE_OK && writes == 1);
    assert(sc_save_decode(&checkpoint, &sc_catalog, &generation, saved,
                          sizeof(saved)) == SC_SAVE_OK);
    assert(checkpoint.interrupted &&
           !memcmp(&checkpoint.state, &expected, sizeof(expected)));
    assert(state.encounter == 351 && state.encounter_variant == 0 &&
           state.phase == SC_BATTLE && request.enemy_count == 1 && combat.ready &&
           combat.encounter == 351 && combat.variant == 0 && combat.kind == SC_TRAINER);
    assert(sc_team(&state, &sc_catalog, 351, 0, canonical, &count) == SC_OK);
    assert(count == 1 && !memcmp(enemies, canonical, sizeof(enemies[0])));
    assert(sc_ko_values(&state, combat.party_species, combat.enemy_species,
                        &combat.team, combat.kind, 0, 1) == SC_OK);
    assert(state.phase == SC_LEARNING && state.party[0].exp == 437 &&
           state.learn_moves[0] == 52);
    assert(sc_learn_pp(&state, combat.pp, 0, 3, 1) == SC_OK);
    assert(state.party[0].pp[3] == 25 && writes == 1);
    request.command = SC_COLD_COMMIT;
    request.outcome = SC_WIN;
    fail_store = 1;
    before = state;
    assert(sc_cold_entry(0, &request) == SC_SAVE_IO && writes == 1 &&
           !memcmp(&state, &before, sizeof(state)));
    fail_store = 0;
    assert(sc_cold_entry(0, &request) == SC_SAVE_OK && writes == 2 &&
           state.phase == SC_REWARD);
    assert(state.completed[351 / 8] & (1U << (351 % 8)));
    request.command = SC_COLD_PREPARE;
    request.seed = 43;
    before = state;
    assert(sc_cold_entry(0, &request) == 0x100 + SC_INVALID && writes == 2 &&
           !memcmp(&state, &before, sizeof(state)));
    puts("Story Ember cold preparation/host KO/save: OK (not native wins)");
    return 0;
}
