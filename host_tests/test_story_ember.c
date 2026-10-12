#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "tools/rltest/story_ember_fixture.h"
#include "src/story/sc_save.h"

static unsigned rows;
static RlU8 reload_note[SC_SAVE_NOTE_SIZE];

static int reload_open(void *context, RlU32 company, RlU32 game,
                       const RlU8 *name, const RlU8 *extension, unsigned size, int create) {
    (void)context; (void)name; (void)extension;
    return company == SC_SAVE_COMPANY && game == SC_SAVE_GAME &&
           size == sizeof(reload_note) && !create ? SC_SAVE_OK : SC_SAVE_INVALID;
}

static int reload_read(void *context, unsigned offset, RlU8 *data, unsigned size) {
    (void)context;
    if (offset > sizeof(reload_note) || size > sizeof(reload_note) - offset)
        return SC_SAVE_INVALID;
    memcpy(data, reload_note + offset, size);
    return SC_SAVE_OK;
}

static int reload_write(void *context, unsigned offset, const RlU8 *data, unsigned size) {
    (void)context; (void)offset; (void)data; (void)size;
    return SC_SAVE_IO;
}

static int read_exact(const char *path, void *data, unsigned size) {
    FILE *file = fopen(path, "rb");
    int valid;
    if (!file) { perror(path); return 0; }
    valid = fread(data, 1, size, file) == size && fgetc(file) == EOF;
    if (fclose(file)) valid = 0;
    if (!valid) fprintf(stderr, "Invalid reload input size: %s\n", path);
    return valid;
}

static void from_be(void *field, unsigned size) {
    RlU8 *bytes = field;
    RlU32 value = 0;
    unsigned i;
    for (i = 0; i < size; i++) value = (value << 8) | bytes[i];
    if (size == 2) *(RlU16 *)field = (RlU16)value;
    else *(RlU32 *)field = value;
}

static int validate_reload(const char *note_path, const char *state_path) {
    ScState expected;
    RlU8 legacy_state[644];
    ScSnapshot snapshot;
    ScSaveDevice device = {NULL, reload_open, reload_read, reload_write};
    unsigned i, j;
    int status;
    memset(&expected, 0, sizeof(expected));
    if (sizeof(expected) != 648 || sizeof(RlMon) != 68 ||
        (RlU8 *)&expected.moon_stones - (RlU8 *)&expected != sizeof(legacy_state) ||
        !read_exact(note_path, reload_note, sizeof(reload_note)) ||
        !read_exact(state_path, legacy_state, sizeof(legacy_state))) return 1;
    /* Pinned historical raw v1 ABI; appended inventory must be fabricated-free. */
    memcpy(&expected, legacy_state, sizeof(legacy_state));
    from_be(&expected.seed, 4); from_be(&expected.rng, 4);
    from_be(&expected.milestones, 4); from_be(&expected.healing_used, 4);
    from_be(&expected.encounter, 2);
    for (i = 0; i < 7; i++) {
        RlMon *mon = i == 6 ? &expected.offer : &expected.party[i];
        from_be(&mon->exp, 4); from_be(&mon->dvs, 2);
        from_be(&mon->cur_hp, 2); from_be(&mon->max_hp, 2);
        for (j = 0; j < 5; j++) {
            from_be(&mon->stat_exp[j], 2);
            from_be(&mon->current_stats[j], 2);
        }
    }
    status = sc_storage_load(&device, &sc_catalog, &snapshot);
    if (status != SC_SAVE_OK || snapshot.interrupted || snapshot.wire_version != 1 ||
        !sc_state_valid(&expected, &sc_catalog) ||
        memcmp(&expected, &snapshot.state, sizeof(expected))) {
        fprintf(stderr, "Completed reload codec/state mismatch: %d\n", status);
        return 1;
    }
    puts("Completed historical v1/644 reload migrated to current 648-byte state: OK");
    return 0;
}

static int read_current_state(const char *path, ScState *state) {
    unsigned i, j;
    if (sizeof(*state) != 648 || sizeof(ScSnapshot) != 652 || sizeof(RlMon) != 68 ||
        (RlU8 *)&state->party - (RlU8 *)state != 168 ||
        (RlU8 *)&state->party_count - (RlU8 *)state != 21 ||
        (RlU8 *)&state->moon_stones - (RlU8 *)state != 644 ||
        (RlU8 *)&state->moon_stones_spent - (RlU8 *)state != 645 ||
        !read_exact(path, state, sizeof(*state))) return 0;
    if (((RlU8 *)state)[646] || ((RlU8 *)state)[647]) return 0;
    from_be(&state->seed, 4); from_be(&state->rng, 4);
    from_be(&state->milestones, 4); from_be(&state->healing_used, 4);
    from_be(&state->encounter, 2);
    for (i = 0; i < 7; i++) {
        RlMon *mon = i == 6 ? &state->offer : &state->party[i];
        from_be(&mon->exp, 4); from_be(&mon->dvs, 2);
        from_be(&mon->cur_hp, 2); from_be(&mon->max_hp, 2);
        for (j = 0; j < 5; j++) {
            from_be(&mon->stat_exp[j], 2);
            from_be(&mon->current_stats[j], 2);
        }
    }
    return 1;
}

static int validate_current_reload(const char *note_path, const char *state_path,
                                   const char *precommit_path) {
    ScState expected, precommit;
    ScSnapshot snapshot;
    ScSaveDevice device = {NULL, reload_open, reload_read, reload_write};
    unsigned i, node;
    int status;
    if (!sc_catalog.graph || sc_catalog.graph->count != 388 ||
        sc_catalog.graph->pickup_count != 5 ||
        !sc_catalog.graph->review_identity || !sc_catalog.graph->legacy_review_identity ||
        strcmp(sc_catalog.graph->review_identity,
               "cc2fc13257f1e82205661a8fb282cddc0257f3f8a261a48a2650e29c45a4ee83") ||
        strcmp(sc_catalog.graph->legacy_review_identity,
               "42abbcfced4b4e824b8495b0579b0b443d14715001e85d5700588b7a534eb42e") ||
        !read_exact(note_path, reload_note, sizeof(reload_note)) ||
        !read_current_state(state_path, &expected) ||
        !read_current_state(precommit_path, &precommit)) {
        fputs("Current six reload ABI/catalog/input refusal\n", stderr);
        return 1;
    }
    if (expected.moon_stones || expected.moon_stones_spent ||
        precommit.moon_stones || precommit.moon_stones_spent) {
        fputs("Current six reload unexpected stone inventory\n", stderr);
        return 1;
    }
    for (i = 0; i < 5; i++) {
        node = sc_catalog.graph->pickups[i].node;
        if ((expected.completed[node / 8] | precommit.completed[node / 8]) &
            (1U << (node % 8))) {
            fputs("Current six reload unexpected stone pickup\n", stderr);
            return 1;
        }
    }
    status = sc_storage_load(&device, &sc_catalog, &snapshot);
    if (status != SC_SAVE_OK || snapshot.interrupted || snapshot.wire_version != 2 ||
        !sc_state_valid(&expected, &sc_catalog) ||
        precommit.phase != SC_BATTLE || precommit.party_count != 6 ||
        precommit.encounter != 58 || precommit.encounter_variant || precommit.learn_mask ||
        precommit.defeated_mask != 3 ||
        sc_finish(&precommit, &sc_catalog, SC_WIN, precommit.encounter_variant) != SC_OK ||
        !sc_state_valid(&precommit, &sc_catalog) ||
        memcmp(&precommit, &expected, sizeof(expected)) ||
        memcmp(&expected, &snapshot.state, sizeof(expected))) {
        fprintf(stderr, "Current v2/648 full-state/precommit mismatch: %d\n", status);
        return 1;
    }
    puts("Current v2/648 reload and canonical full-state precommit transition: OK");
    return 0;
}

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
    if (argc == 4 && !strcmp(argv[1], "--validate-reload"))
        return validate_reload(argv[2], argv[3]);
    if (argc == 5 && !strcmp(argv[1], "--validate-current-reload"))
        return validate_current_reload(argv[2], argv[3], argv[4]);
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
