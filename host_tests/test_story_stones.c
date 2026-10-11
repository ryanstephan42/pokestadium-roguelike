#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/story/sc_ui.h"
#include "../src/story/sc_graph.h"

static ScCatalog catalog;
static ScState state, before;
static ScSnapshot snapshot, loaded, prior;
static ScUi ui;
static RlU8 note[SC_SAVE_NOTE_SIZE], bytes[SC_SAVE_SIZE], original[SC_SAVE_SIZE];
static int exists, fail_write;

static int open_note(void *context, unsigned company, RlU32 game, const RlU8 name[16],
                     const RlU8 extension[4], unsigned size, int create) {
    (void)context;
    assert(company == SC_SAVE_COMPANY && game == SC_SAVE_GAME && size == sizeof(note));
    assert(!memcmp(name, sc_save_name, 16) && !memcmp(extension, sc_save_extension, 4));
    if (!exists && !create) return SC_SAVE_NOT_FOUND;
    exists = 1;
    return SC_SAVE_OK;
}
static int read_note(void *context, unsigned offset, RlU8 *out, unsigned size) {
    (void)context;
    assert(offset + size <= sizeof(note));
    memcpy(out, note + offset, size);
    return SC_SAVE_OK;
}
static int write_note(void *context, unsigned offset, const RlU8 *in, unsigned size) {
    (void)context;
    assert(offset + size <= sizeof(note));
    if (fail_write) return SC_SAVE_IO;
    memcpy(note + offset, in, size);
    return SC_SAVE_OK;
}
static const ScSaveDevice device = {NULL, open_note, read_note, write_note};

static int choose(unsigned id) {
    unsigned i;
    for (i = 0; i < (unsigned)ui.list.count; i++) if (ui.ids[i] == id) break;
    assert(i < (unsigned)ui.list.count);
    while (ui.list.cursor < (int)i) sc_ui_input(&ui, RL_UI_DOWN);
    while (ui.list.cursor > (int)i) sc_ui_input(&ui, RL_UI_UP);
    return sc_ui_input(&ui, RL_UI_A);
}
static void confirm(void) {
    assert(ui.screen == SC_UI_CONFIRM);
    choose(1);
}
static void new_state(unsigned version) {
    ScOptions options = {0, SC_STANDARD, SC_STARTER_TRIO, 1};
    options.version = (RlU8)version;
    assert(sc_new(&state, &catalog, &options, 123) == SC_OK);
}
static void settle(void) {
    unsigned i;
    while (state.phase == SC_LEARNING)
        for (i = 0; i < state.party_count; i++)
            if (state.learn_mask & (1U << i)) assert(sc_learn(&state, &catalog, i, 0, 0) == SC_OK);
}
static void encounter(unsigned index) {
    unsigned i, variant = catalog.encounters[index].kind == SC_RIVAL ? state.rival_branch :
        index == catalog.graph->fossil_offer ? state.fossil_choice : 0;
    assert(sc_begin(&state, &catalog, index, variant) == SC_OK);
    if (state.phase == SC_BATTLE) {
        for (i = 0; i < catalog.encounters[index].teams[variant].count; i++) {
            assert(sc_on_ko(&state, &catalog, i, 1, variant) == SC_OK);
            settle();
        }
        assert(sc_finish(&state, &catalog, SC_WIN, variant) == SC_OK);
    }
    if (state.phase == SC_REWARD) assert(sc_decline(&state, &catalog) == SC_OK);
    assert(sc_state_valid(&state, &catalog));
}
static void reach(unsigned node) {
    unsigned i, pass;
    for (pass = 0; pass < 383 && !sc_graph_open(&state, &catalog, node); pass++) {
        for (i = 0; i < catalog.count; i++)
            if (sc_available(&state, &catalog, i)) break;
        if (i < catalog.count) encounter(i);
        else {
            for (; i < 383; i++)
                if (sc_action(&state, &catalog, i, 0, catalog.graph->nodes[i].destination_warp) == SC_OK)
                    break;
            assert(i < 383);
        }
    }
    assert(sc_graph_open(&state, &catalog, node));
}
static void save_and_continue(void) {
    memset(note, 0, sizeof(note)); exists = fail_write = 0;
    snapshot.state = state; snapshot.interrupted = 0;
    assert(sc_storage_store(&device, &catalog, &snapshot, 1) == SC_SAVE_OK);
    assert(sc_ui_init(&ui, &catalog, &device, 99) == SC_OK);
    choose(0);
    assert(ui.screen == SC_UI_REGIONS && !memcmp(&ui.state, &state, sizeof(state)));
}

static void test_each_pickup(void) {
    unsigned version, i, node;
    for (version = 0; version < 2; version++) for (i = 0; i < 5; i++) {
        new_state(version);
        node = catalog.graph->pickups[i].node;
        before = state;
        assert(sc_pickup_stone(&state, &catalog, i) == SC_UNAVAILABLE);
        assert(sc_action(&state, &catalog, node, 0, 0) == SC_UNAVAILABLE);
        assert(!memcmp(&state, &before, sizeof(state)));
        reach(node);
        save_and_continue();
        choose(2); choose(node);
        before = ui.state;
        sc_ui_input(&ui, RL_UI_B);
        assert(ui.screen == SC_UI_ENCOUNTERS && !memcmp(&ui.state, &before, sizeof(before)));
        choose(node);
        fail_write = 1; confirm();
        assert(ui.screen == SC_UI_SAVE_ERROR && !memcmp(&ui.state, &before, sizeof(before)));
        assert(ui.pending.state.moon_stones == 1 && sc_state_valid(&ui.pending.state, &catalog));
        fail_write = 0; choose(0);
        assert(ui.screen == SC_UI_REGIONS && ui.state.moon_stones == 1);
        assert(sc_storage_load(&device, &catalog, &loaded) == SC_SAVE_OK);
        assert(!memcmp(&loaded.state, &ui.state, sizeof(state)) && loaded.wire_version == 2);
        before = ui.state;
        assert(sc_pickup_stone(&ui.state, &catalog, i) == SC_UNAVAILABLE);
        assert(sc_action(&ui.state, &catalog, node, 0, 0) == SC_UNAVAILABLE);
        assert(!memcmp(&ui.state, &before, sizeof(state)));
    }
}

static void make_species(unsigned species) {
    ScTemplate mon;
    memset(&mon, 0, sizeof(mon)); mon.species = (RlU8)species; mon.level = 30;
    assert(sc_make_mon(&catalog, &mon, 0x8889, &state.party[0]) == SC_OK);
}

static void test_consumption(void) {
    unsigned i;
    new_state(0);
    reach(catalog.graph->pickups[1].node);
    assert(sc_pickup_stone(&state, &catalog, 1) == SC_OK);
    make_species(35); /* Clefairy is a real canonical Moon Stone evolution fixture. */
    state.party[0].cur_hp -= 2; state.party[0].status = 8; state.party[0].pp[0]--;
    before = state;
    assert(sc_evolve_owned(&state, &catalog, 0, 0, 0) == SC_PENDING);
    assert(!memcmp(&state, &before, sizeof(state)));
    assert(sc_evolve_owned(&state, &catalog, 6, 0, 1) == SC_INVALID);
    assert(!memcmp(&state, &before, sizeof(state)));
    save_and_continue();
    choose(0); choose(0); choose(1); choose(0);
    fail_write = 1; confirm();
    assert(ui.screen == SC_UI_SAVE_ERROR && !memcmp(&ui.state, &before, sizeof(before)));
    assert(ui.pending.state.party[0].species == 36 && ui.pending.state.moon_stones == 0 &&
           ui.pending.state.moon_stones_spent == 1);
    fail_write = 0; choose(0);
    assert(ui.state.party[0].species == 36 && ui.state.moon_stones == 0 && ui.state.moon_stones_spent == 1);
    assert(ui.state.party[0].max_hp - ui.state.party[0].cur_hp == 2 && ui.state.party[0].status == 8);
    for (i = 0; i < 4; i++) {
        assert(ui.state.party[0].moves[i] == before.party[0].moves[i]);
        assert(ui.state.party[0].pp[i] == before.party[0].pp[i]);
    }
    assert(sc_storage_load(&device, &catalog, &loaded) == SC_SAVE_OK);
    assert(loaded.state.moon_stones_spent == 1 && loaded.state.party[0].species == 36);
    state = loaded.state;
    make_species(35);
    before = state;
    assert(sc_evolve_owned(&state, &catalog, 0, 0, 1) == SC_UNAVAILABLE);
    assert(!memcmp(&state, &before, sizeof(state)));
    reach(catalog.graph->pickups[2].node);
    assert(sc_pickup_stone(&state, &catalog, 2) == SC_OK);
    make_species(25);
    before = state;
    assert(sc_evolve_owned(&state, &catalog, 0, 0, 1) == SC_UNAVAILABLE);
    assert(!memcmp(&state, &before, sizeof(state)));
    make_species(64);
    before = state;
    assert(sc_evolve_owned(&state, &catalog, 0, 0, 1) == SC_UNAVAILABLE);
    assert(!memcmp(&state, &before, sizeof(state)));
    make_species(1);
    state.evolution_mask = 1;
    assert(sc_evolve_owned(&state, &catalog, 0, 0, 1) == SC_OK);
    settle();
    assert(state.party[0].species == 2 && state.moon_stones == 1 && state.moon_stones_spent == 1);
}

static void crc(void) {
    RlU32 value;
    unsigned i;
    for (i = 8; i < 12; i++) bytes[i] = 0;
    value = sc_save_crc(bytes, sizeof(bytes));
    for (i = 0; i < 4; i++) bytes[8 + i] = (RlU8)(value >> (8 * i));
}
static void decode(int expected) {
    RlU32 generation = 0xabcdefU;
    prior = loaded;
    assert(sc_save_decode(&loaded, &catalog, &generation, bytes, sizeof(bytes)) == expected);
    if (expected != SC_SAVE_OK) {
        assert(!memcmp(&loaded, &prior, sizeof(loaded)));
        assert(generation == 0xabcdefU);
    } else assert(generation == 7);
}
static void test_codec(const char *v1_fixture) {
    unsigned i, node;
    FILE *file;
    new_state(0);
    snapshot.state = state; snapshot.interrupted = 0;
    assert(sc_save_encode(&snapshot, &catalog, 7, bytes) == SC_SAVE_OK);
    assert(bytes[4] == 2 && bytes[5] == 0 && bytes[SC_SAVE_INVENTORY_OFFSET] == 0);
    memcpy(original, bytes, sizeof(bytes));
    decode(SC_SAVE_OK);
    assert(loaded.wire_version == 2);
    bytes[SC_SAVE_INVENTORY_OFFSET] = 1; crc(); decode(SC_SAVE_BAD_DATA);
    memcpy(bytes, original, sizeof(bytes));
    bytes[SC_SAVE_INVENTORY_OFFSET + 1] = 1; crc(); decode(SC_SAVE_BAD_DATA);
    memcpy(bytes, original, sizeof(bytes));
    bytes[SC_SAVE_INVENTORY_OFFSET + 2] = 1; crc(); decode(SC_SAVE_BAD_DATA);
    memcpy(bytes, original, sizeof(bytes));
    bytes[4] = 3; crc(); decode(SC_SAVE_BAD_VERSION);
    memcpy(bytes, original, sizeof(bytes));
    memcpy(bytes + 32, catalog.graph->legacy_review_identity, 64);
    crc(); decode(SC_SAVE_BAD_VERSION);
    file = fopen(v1_fixture, "rb");
    assert(file && fread(bytes, 1, sizeof(bytes), file) == sizeof(bytes));
    assert(fgetc(file) == EOF && fclose(file) == 0);
    assert(bytes[4] == 1 && bytes[5] == 0);
    decode(SC_SAVE_OK);
    assert(loaded.wire_version == 1 && !loaded.state.moon_stones && !loaded.state.moon_stones_spent);
    assert(!memcmp(&loaded.state, &state, sizeof(state)));
    memset(note, 0, sizeof(note));
    memcpy(note, bytes, sizeof(bytes)); exists = 1; fail_write = 0;
    assert(sc_ui_init(&ui, &catalog, &device, 123) == SC_OK);
    choose(0);
    assert(ui.screen == SC_UI_REGIONS && strstr(ui.message, "Legacy save"));
    choose(3);
    assert(note[4] == 1 && note[SC_SAVE_SLOT_SIZE + 4] == 2);
    assert(sc_storage_load(&device, &catalog, &loaded) == SC_SAVE_OK && loaded.wire_version == 2);
    assert(!loaded.state.moon_stones && !loaded.state.moon_stones_spent);
    snapshot = loaded;
    assert(sc_save_encode(&snapshot, &catalog, 7, bytes) == SC_SAVE_OK);
    assert(bytes[4] == 2 && !memcmp(bytes + 32, catalog.graph->review_identity, 64));
    memcpy(bytes, original, sizeof(bytes));
    bytes[4] = 1; memcpy(bytes + 32, catalog.graph->legacy_review_identity, 64);
    node = catalog.graph->pickups[0].node;
    bytes[134 + node / 8] |= (RlU8)(1U << (node % 8));
    crc(); decode(SC_SAVE_BAD_DATA);
    memcpy(bytes, original, sizeof(bytes));
    bytes[4] = 1; memcpy(bytes + 32, catalog.graph->legacy_review_identity, 64);
    bytes[SC_SAVE_INVENTORY_OFFSET] = 1; crc(); decode(SC_SAVE_BAD_DATA);
    memcpy(bytes, original, sizeof(bytes));
    bytes[32] ^= 1; crc(); decode(SC_SAVE_BAD_VERSION);
    new_state(0);
    for (i = 0; i < 5; i++) {
        node = catalog.graph->pickups[i].node;
        reach(node);
        assert(sc_pickup_stone(&state, &catalog, i) == SC_OK);
        assert(state.moon_stones == i + 1 && sc_state_valid(&state, &catalog));
    }
    assert(state.moon_stones == 5 && state.moon_stones_spent == 0);
    before = state; state.moon_stones_spent = 1;
    assert(!sc_state_valid(&state, &catalog));
    state = before; state.moon_stones = 6;
    assert(!sc_state_valid(&state, &catalog));
}

int main(int argc, char **argv) {
    unsigned i;
    catalog = sc_catalog;
    if (!catalog.graph_reviewed) {
        /* Candidate extension fixture only; never promotes native catalog admission. */
        ScOptions options = {0, SC_STANDARD, SC_STARTER_TRIO, 1};
        memset(&state, 0xa5, sizeof(state)); before = state;
        assert(sc_new(&state, &catalog, &options, 123) == SC_UNREVIEWED);
        assert(!memcmp(&state, &before, sizeof(state)));
        catalog.graph_reviewed = 1;
        puts("Testing unreviewed Moon extension as an explicit host fixture");
    }
    assert(sc_catalog_valid(&catalog) && catalog.graph->count == 388 && catalog.graph->pickup_count == 5);
    for (i = 0; i < 5; i++) assert(catalog.graph->pickups[i].node == 383 + i);
    test_each_pickup();
    test_consumption();
    assert(argc == 2);
    test_codec(argv[1]);
    puts("Finite Moon Stones: all five gates, atomic UI saves, consumption, v1 migration/v2 corruption; OK");
    return 0;
}
