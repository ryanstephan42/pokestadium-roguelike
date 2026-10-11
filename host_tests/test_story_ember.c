#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "tools/rltest/story_ember_fixture.h"
#include "src/story/sc_save.h"

static unsigned rows;

static void trace(const char *operation, unsigned index, const ScState *state,
                   int status, void *context) {
    const RlMon *mon = &state->party[0];
    unsigned i;
    static const RlU32 xp[] = {135, 135, 205, 205, 205, 205, 205,
                              271, 338, 338, 338, 338, 338, 437};
    (void)context;
    assert(rows < sizeof(xp) / sizeof(xp[0]) && status == SC_OK &&
           mon->exp == xp[rows] && state->seed == 42 && mon->dvs == 0x4528);
    printf("{\"scope\":\"pure-host-prehistory-not-native-wins\","
           "\"operation\":\"%s\",\"index\":%u,\"status\":%d,\"phase\":%u,"
           "\"seed\":%lu,\"rng\":%lu,\"rival\":%u,\"party_count\":%u,"
           "\"xp\":%lu,\"level\":%u,\"dvs\":%u,\"hp\":%u,\"max_hp\":%u,"
           "\"learn_mask\":%u,\"healing_used\":%lu,\"completed\":\"",
           operation, index, status, state->phase, (unsigned long)state->seed,
           (unsigned long)state->rng, state->rival_branch, state->party_count,
           (unsigned long)mon->exp, mon->level, mon->dvs, mon->cur_hp,
           mon->max_hp, state->learn_mask, (unsigned long)state->healing_used);
    for (i = 0; i < SC_BITS; i++) printf("%02x", state->completed[i]);
    printf("\",\"stat_exp\":[%u,%u,%u,%u,%u],\"stats\":[%u,%u,%u,%u,%u],"
           "\"moves\":[%u,%u,%u,%u],\"pp\":[%u,%u,%u,%u]}\n",
           mon->stat_exp[0], mon->stat_exp[1], mon->stat_exp[2], mon->stat_exp[3],
           mon->stat_exp[4], mon->current_stats[0], mon->current_stats[1],
           mon->current_stats[2], mon->current_stats[3], mon->current_stats[4],
           mon->moves[0], mon->moves[1], mon->moves[2], mon->moves[3],
           mon->pp[0], mon->pp[1], mon->pp[2], mon->pp[3]);
    rows++;
}

int main(int argc, char **argv) {
    ScState state, unchanged;
    ScSnapshot snapshot, decoded;
    ScCatalog bad;
    ScGraph graph;
    ScNode nodes[SC_ENCOUNTER_CAPACITY];
    RlU8 encoded[SC_SAVE_SIZE];
    RlU32 generation;
    FILE *file;
    RlMon enemies[6];
    unsigned i, target = SC_NO_ENCOUNTER, count;
    assert(sc_ember_checkpoint(&state, &sc_catalog, trace, NULL) == SC_OK);
    assert(sc_state_valid(&state, &sc_catalog));
    assert(state.party_count == 1 && state.rival_branch == 0);
    assert(state.party[0].species == 4 && state.party[0].exp == 338 &&
           state.party[0].level == 8 && state.party[0].dvs == 0x4528 &&
           !state.learn_mask && state.party[0].cur_hp == 24 &&
           !state.party[0].status && !state.party[0].fainted);
    assert(state.party[0].current_stats[0] == 24 && state.party[0].current_stats[1] == 14 &&
           state.party[0].current_stats[2] == 12 && state.party[0].current_stats[3] == 16 &&
           state.party[0].current_stats[4] == 13);
    assert(state.party[0].stat_exp[0] == 129 && state.party[0].stat_exp[1] == 113 &&
           state.party[0].stat_exp[2] == 130 && state.party[0].stat_exp[3] == 138 &&
           state.party[0].stat_exp[4] == 90);
    for (i = 0; i < sc_catalog.count; i++)
        if (!strcmp(sc_catalog.encounters[i].id,
                     "ViridianForest/TEXT_VIRIDIANFOREST_YOUNGSTER4")) target = i;
    assert(target != SC_NO_ENCOUNTER && sc_available(&state, &sc_catalog, target));
    assert(target == 351 && !(state.completed[350 / 8] & (1U << (350 % 8))));
    snapshot.state = state;
    snapshot.interrupted = 1;
    assert(sc_save_encode(&snapshot, &sc_catalog, 1, encoded) == SC_SAVE_OK);
    assert(sc_save_decode(&decoded, &sc_catalog, &generation, encoded,
                          sizeof(encoded)) == SC_SAVE_OK);
    assert(generation == 1 && decoded.interrupted &&
           !memcmp(&decoded.state, &state, sizeof(state)));
    if (argc == 2) {
        file = fopen(argv[1], "wb");
        if (!file) { perror(argv[1]); return 1; }
        if (fwrite(encoded, 1, sizeof(encoded), file) != sizeof(encoded)) {
            perror("Ember checkpoint write");
            fclose(file);
            return 1;
        }
        if (fclose(file)) { perror("Ember checkpoint close"); return 1; }
    } else if (argc != 1) return 1;
    bad = sc_catalog;
    graph = *bad.graph;
    memcpy(nodes, graph.nodes, graph.count * sizeof(nodes[0]));
    graph.nodes = nodes;
    bad.graph = &graph;
    nodes[target].clauses[0].prior[0] = (RlU16)target;
    nodes[target].clauses[0].count = 1;
    unchanged = state;
    assert(sc_ember_checkpoint(&state, &bad, NULL, NULL) == SC_UNAVAILABLE);
    assert(!memcmp(&unchanged, &state, sizeof(state)));
    memcpy(nodes, sc_catalog.graph->nodes, graph.count * sizeof(nodes[0]));
    {
        ScHealing healing[15];
        memcpy(healing, graph.healing, sizeof(healing));
        graph.healing = healing;
        healing[0].required.count = 1;
        healing[0].required.prior[0] = (RlU16)target;
        assert(sc_ember_checkpoint(&state, &bad, NULL, NULL) == SC_UNAVAILABLE);
        assert(!memcmp(&unchanged, &state, sizeof(state)));
    }
    assert(sc_team(&state, &sc_catalog, target, 0, enemies, &count) == SC_OK);
    assert(count == 1 && enemies[0].species == 13 && enemies[0].level == 9);
    assert(enemies[0].moves[0] == 40 && enemies[0].moves[1] == 81 &&
           enemies[0].pp[0] == 35 && enemies[0].pp[1] == 40);
    /* sc_team draws RNG, but canonical fixed-DV templates select their authored DVs. */
    assert(sc_catalog.encounters[target].teams[0].mons[0].fixed_dvs == 1 &&
           enemies[0].dvs == sc_catalog.encounters[target].teams[0].mons[0].dvs &&
           enemies[0].dvs == 0x8889);
    printf("{\"scope\":\"canonical-target-from-sc_team\",\"encounter\":%u,"
           "\"dvs\":%u,\"hp\":%u,\"stats\":[%u,%u,%u,%u,%u]}\n", target,
           enemies[0].dvs, enemies[0].max_hp, enemies[0].current_stats[0],
           enemies[0].current_stats[1], enemies[0].current_stats[2],
           enemies[0].current_stats[3], enemies[0].current_stats[4]);
    assert(sc_begin(&state, &sc_catalog, target, 0) == SC_OK);
    assert(sc_on_ko(&state, &sc_catalog, 0, 1, 0) == SC_OK);
    trace("target-host-ko-proof-not-native", target, &state, SC_OK, NULL);
    assert(state.party[0].exp == 437 && state.party[0].level == 9 &&
           state.learn_mask == 1 && state.learn_moves[0] == 52);
    assert(state.party[0].current_stats[0] == 26 && state.party[0].current_stats[1] == 16 &&
           state.party[0].current_stats[2] == 13 && state.party[0].current_stats[3] == 17 &&
           state.party[0].current_stats[4] == 14);
    assert(state.party[0].stat_exp[0] == 169 && state.party[0].stat_exp[1] == 148 &&
           state.party[0].stat_exp[2] == 160 && state.party[0].stat_exp[3] == 188 &&
           state.party[0].stat_exp[4] == 110);
    assert(sc_learn(&state, &sc_catalog, 0, 3, 1) == SC_OK);
    assert(state.party[0].moves[3] == 52 && state.party[0].pp[3] == 25 &&
           state.party[0].max_pp[3] == 25);
    assert(sc_finish(&state, &sc_catalog, SC_WIN, 0) == SC_OK);
    assert(state.phase == SC_REWARD && sc_state_valid(&state, &sc_catalog));
    assert(rows == 14);
    return 0;
}
