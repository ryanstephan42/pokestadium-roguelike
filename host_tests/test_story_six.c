#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "tools/rltest/story_six_fixture.h"
#include "src/story/sc_save.h"

static unsigned recruits, learns, rows;
static ScState nested;

static void reenter(const char *operation, unsigned index, const ScState *state,
                     int status, void *context) {
    (void)operation; (void)index; (void)state; (void)status; (void)context;
    assert(sc_six_checkpoint(&nested, &sc_catalog, NULL, NULL) == SC_OK);
}

static void trace(const char *operation, unsigned index, const ScState *state,
                   int status, void *context) {
    unsigned i, j;
    (void)context;
    assert(status == SC_OK && state->seed == 42 && state->rival_branch == 0);
    if (!strcmp(operation, "recruit")) recruits++;
    if (!strcmp(operation, "learn")) learns++;
    printf("{\"scope\":\"pure-host-prehistory-not-native-wins\","
           "\"operation\":\"%s\",\"index\":%u,\"rng\":%lu,\"phase\":%u,"
           "\"offer_slot\":%u,\"offer_species\":%u,\"party\":[",
           operation, index, (unsigned long)state->rng, state->phase,
           state->offered_slot, state->offer.species);
    for (i = 0; i < state->party_count; i++) {
        const RlMon *mon = &state->party[i];
        printf("%s{\"species\":%u,\"level\":%u,\"xp\":%lu,\"dvs\":%u,"
               "\"hp\":%u,\"max_hp\":%u,\"status\":%u,\"fainted\":%u,"
               "\"growth\":%u,\"held_relic\":%u,\"nickname\":\"",
               i ? "," : "", mon->species, mon->level, (unsigned long)mon->exp,
               mon->dvs, mon->cur_hp, mon->max_hp, mon->status, mon->fainted,
               mon->growth, mon->held_relic);
        for (j = 0; j < 11; j++) printf("%02x", (unsigned char)mon->nickname[j]);
        printf("\",\"base_stats\":[%u,%u,%u,%u,%u],\"max_pp\":[%u,%u,%u,%u],"
               "\"moves\":[%u,%u,%u,%u],"
               "\"pp\":[%u,%u,%u,%u],\"stats\":[%u,%u,%u,%u,%u],\"stat_exp\":[",
               mon->base_stats[0], mon->base_stats[1], mon->base_stats[2],
               mon->base_stats[3], mon->base_stats[4],
               mon->max_pp[0], mon->max_pp[1], mon->max_pp[2], mon->max_pp[3],
               mon->moves[0], mon->moves[1],
               mon->moves[2], mon->moves[3], mon->pp[0], mon->pp[1], mon->pp[2], mon->pp[3],
               mon->current_stats[0], mon->current_stats[1], mon->current_stats[2],
               mon->current_stats[3], mon->current_stats[4]);
        for (j = 0; j < 5; j++) printf("%s%u", j ? "," : "", mon->stat_exp[j]);
        printf("]}");
    }
    printf("],\"completed\":\"");
    for (i = 0; i < SC_BITS; i++) printf("%02x", state->completed[i]);
    printf("\"}\n");
    rows++;
}

int main(int argc, char **argv) {
    ScState state, before;
    ScState team;
    ScSnapshot snapshot, decoded;
    RlU8 encoded[SC_SAVE_SIZE];
    RlU32 generation;
    unsigned i;
    FILE *file;
    assert(sc_six_checkpoint(&state, &sc_catalog, trace, NULL) == SC_OK);
    assert(sc_six_checkpoint(&team, &sc_catalog, reenter, NULL) == SC_OK);
    assert(!memcmp(&state, &team, sizeof(state)) && !memcmp(&state, &nested, sizeof(state)));
    assert(recruits == 5 && learns && rows && state.party_count == 6);
    assert(sc_state_valid(&state, &sc_catalog) && sc_available(&state, &sc_catalog, 58));
    assert(state.rng == 1441438134U && rows == 37 && learns == 1);
    {
        static const unsigned species[6] = {4,7,10,13,13,50};
        static const unsigned level[6] = {12,5,6,7,9,11};
        static const RlU32 xp[6] = {1108,135,216,343,729,1331};
        for (i = 0; i < 6; i++)
            assert(state.party[i].species == species[i] && state.party[i].level == level[i] &&
                   state.party[i].exp == xp[i] && state.party[i].dvs == (i ? 0x8889 : 0x4528));
    }
    {
        ScCatalog bad = sc_catalog;
        before = state;
        bad.graph_reviewed = 0;
        assert(sc_six_checkpoint(&state, &bad, NULL, NULL) == SC_INVALID);
        assert(!memcmp(&state, &before, sizeof(state)));
    }
    team = state;
    {
        unsigned count;
        assert(sc_team(&state, &sc_catalog, 58, 0, team.party, &count) == SC_OK && count == 2);
        team.party_count = (RlU8)count;
        trace("target-team", 58, &team, SC_OK, NULL);
    }
    for (i = 0; i < 6; i++) {
        assert(state.party[i].cur_hp == state.party[i].max_hp &&
               !state.party[i].status && !state.party[i].fainted);
        assert(!memcmp(state.party[i].pp, state.party[i].max_pp, 4));
    }
    snapshot.state = state; snapshot.interrupted = 1;
    assert(sc_save_encode(&snapshot, &sc_catalog, 1, encoded) == SC_SAVE_OK);
    assert(sc_save_decode(&decoded, &sc_catalog, &generation, encoded, sizeof(encoded)) == SC_SAVE_OK);
    assert(generation == 1 && decoded.interrupted &&
           !memcmp(&state, &decoded.state, sizeof(state)));
    if (argc == 2) {
        file = fopen(argv[1], "wb");
        if (!file) { perror(argv[1]); return 1; }
        if (fwrite(encoded, 1, sizeof(encoded), file) != sizeof(encoded)) {
            perror("Six-party checkpoint write"); fclose(file); return 1;
        }
        if (fclose(file)) { perror("Six-party checkpoint close"); return 1; }
    } else if (argc != 1) return 1;
    before = state;
    assert(sc_begin(&state, &sc_catalog, 58, 0) == SC_OK);
    assert(sc_on_ko(&state, &sc_catalog, 0, 1, 0) == SC_OK);
    while (state.learn_mask)
        for (i = 0; i < 6; i++)
            if (state.learn_mask & (1U << i)) assert(sc_learn(&state, &sc_catalog, i, 3, 0) == SC_OK);
    assert(sc_on_ko(&state, &sc_catalog, 1, 1, 0) == SC_OK);
    while (state.learn_mask)
        for (i = 0; i < 6; i++)
            if (state.learn_mask & (1U << i)) assert(sc_learn(&state, &sc_catalog, i, 3, 0) == SC_OK);
    assert(sc_finish(&state, &sc_catalog, SC_WIN, 0) == SC_OK);
    assert(!sc_available(&state, &sc_catalog, 59));
    snapshot.state = state;
    assert(sc_recruit(&state, &sc_catalog, RL_NO_SLOT, 1) == SC_FULL);
    assert(!memcmp(&state, &snapshot.state, sizeof(state)));
    assert(sc_recruit(&state, &sc_catalog, 2, 0) == SC_PENDING);
    assert(!memcmp(&state, &snapshot.state, sizeof(state)));
    assert(sc_recruit(&state, &sc_catalog, 2, 1) == SC_OK && state.party_count == 6);
    assert(!memcmp(&state.party[1], &before.party[1], sizeof(RlMon)));
    return 0;
}
