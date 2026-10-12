/* Host fixture: private SC scratch guards refuse nesting and release on every path. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/story/sc_ui.h"
#include "../src/story/sc_bridge.h"

void sc_scratch_test_hold_begin(int held);
int sc_scratch_test_busy_begin(void);
void sc_scratch_test_hold_new(int held);
int sc_scratch_test_busy_new(void);
void sc_scratch_test_hold_valid(int held);
int sc_scratch_test_busy_valid(void);
void sc_scratch_test_hold_decode(int held);
int sc_scratch_test_busy_decode(void);
void sc_scratch_test_hold_input(int held);
int sc_scratch_test_busy_input(void);

static RlU8 note[SC_SAVE_NOTE_SIZE];
static int exists, fail_write, nest_bridge, nest_input, nested_calls;
static ScState nested_state;
static ScBridge nested_bridge;
static ScUi ui, nested_ui;
static int nested_result, nested_status;
static unsigned nested_encounter, nested_variant;

static void assert_idle(void) {
    assert(!sc_scratch_test_busy_begin() && !sc_scratch_test_busy_new() &&
           !sc_scratch_test_busy_valid() && !sc_scratch_test_busy_decode() &&
           !sc_scratch_test_busy_input());
}

static int device_open(void *context, unsigned company, RlU32 game,
                       const RlU8 name[16], const RlU8 extension[4],
                       unsigned size, int create) {
    (void)context; (void)company; (void)game; (void)name; (void)extension;
    assert(size == sizeof(note));
    if (!exists && !create) return SC_SAVE_NOT_FOUND;
    exists = 1;
    return SC_SAVE_OK;
}

static int device_read(void *context, unsigned offset, RlU8 *out, unsigned size) {
    (void)context;
    assert(offset + size <= sizeof(note));
    if (nest_input) {
        ScState before;
        nest_input = 0;
        nested_calls++;
        assert(sc_scratch_test_busy_input());
        before = nested_ui.state;
        nested_result = sc_ui_input(&nested_ui, RL_UI_A);
        assert(nested_result == SC_UI_STAY && nested_ui.status == SC_INVALID);
        assert(!memcmp(&before, &nested_ui.state, sizeof(before)));
    }
    memcpy(out, note + offset, size);
    return SC_SAVE_OK;
}

static int device_write(void *context, unsigned offset, const RlU8 *in, unsigned size) {
    ScState before;
    (void)context;
    assert(offset + size <= sizeof(note));
    if (nest_bridge) {
        nest_bridge = 0;
        nested_calls++;
        assert(sc_scratch_test_busy_begin());
        before = nested_state;
        nested_status = -7;
        assert(sc_bridge_begin(&nested_state, &nested_bridge, &sc_catalog, NULL,
                               nested_encounter, nested_variant, &nested_status) == SC_INVALID);
        assert(nested_status == -7);
        assert(!memcmp(&before, &nested_state, sizeof(before)));
    }
    if (fail_write) return SC_SAVE_IO;
    memcpy(note + offset, in, size);
    return SC_SAVE_OK;
}

static const ScSaveDevice device = {NULL, device_open, device_read, device_write};

static unsigned find(const char *id) {
    unsigned i;
    for (i = 0; i < sc_catalog.count; i++)
        if (!strcmp(sc_catalog.encounters[i].id, id)) return i;
    assert(0);
    return 0;
}

static void fresh(ScState *state) {
    ScOptions options;
    memset(&options, 0, sizeof(options));
    options.starter = 1;
    assert(sc_new(state, &sc_catalog, &options, 42) == SC_OK);
    assert_idle();
}

static void test_held_refusal(void) {
    ScState state, copy;
    ScSnapshot snapshot, untouched;
    ScOptions options;
    RlU8 bytes[SC_SAVE_SIZE];
    RlU32 generation = 77;
    ScBridge bridge;
    int storage = -3;
    unsigned rival = find("OaksLab/rival-lab");

    fresh(&state);
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.state = state;
    assert(sc_save_encode(&snapshot, &sc_catalog, 5, bytes) == SC_SAVE_OK);

    memset(&options, 0, sizeof(options));
    options.starter = 1;
    copy = state;
    sc_scratch_test_hold_new(1);
    assert(sc_new(&copy, &sc_catalog, &options, 9) == SC_INVALID);
    assert(!memcmp(&copy, &state, sizeof(copy)));
    /* Validation depends on sc_new; a held sc_new fails validation closed. */
    assert(!sc_state_valid(&state, &sc_catalog));
    sc_scratch_test_hold_new(0);

    sc_scratch_test_hold_valid(1);
    assert(!sc_state_valid(&state, &sc_catalog));
    assert(sc_save_encode(&snapshot, &sc_catalog, 5, bytes) == SC_SAVE_INVALID);
    sc_scratch_test_hold_valid(0);
    assert(sc_save_encode(&snapshot, &sc_catalog, 5, bytes) == SC_SAVE_OK);

    memset(&untouched, 0x5a, sizeof(untouched));
    memcpy(&snapshot, &untouched, sizeof(snapshot));
    sc_scratch_test_hold_decode(1);
    assert(sc_save_decode(&snapshot, &sc_catalog, &generation, bytes, SC_SAVE_SIZE) ==
           SC_SAVE_INVALID);
    assert(!memcmp(&snapshot, &untouched, sizeof(snapshot)) && generation == 77);
    sc_scratch_test_hold_decode(0);
    assert(sc_save_decode(&snapshot, &sc_catalog, &generation, bytes, SC_SAVE_SIZE) ==
           SC_SAVE_OK);
    assert(generation == 5 && !memcmp(&snapshot.state, &state, sizeof(state)));

    copy = state;
    sc_scratch_test_hold_begin(1);
    assert(sc_bridge_begin(&copy, &bridge, &sc_catalog, &device, rival,
                           state.rival_branch, &storage) == SC_INVALID);
    assert(storage == -3 && !memcmp(&copy, &state, sizeof(copy)));
    sc_scratch_test_hold_begin(0);

    assert(sc_ui_init(&ui, &sc_catalog, &device, 3) == SC_OK);
    sc_scratch_test_hold_input(1);
    copy = ui.state;
    assert(sc_ui_input(&ui, RL_UI_A) == SC_UI_STAY && ui.status == SC_INVALID);
    assert(ui.screen == SC_UI_MENU && !memcmp(&copy, &ui.state, sizeof(copy)));
    sc_scratch_test_hold_input(0);
    assert(sc_ui_input(NULL, RL_UI_A) == SC_UI_STAY);
    assert_idle();
}

static void test_error_release_and_retry(void) {
    ScState state, copy;
    ScSnapshot snapshot;
    ScBridge bridge;
    ScOptions options;
    RlU8 bytes[SC_SAVE_SIZE];
    RlU32 generation;
    int storage;
    unsigned rival = find("OaksLab/rival-lab");

    memset(&options, 0, sizeof(options));
    options.difficulty = 9;
    assert(sc_new(&state, &sc_catalog, &options, 1) == SC_INVALID);
    assert_idle();
    fresh(&state);

    copy = state;
    copy.party_count = 0;
    assert(!sc_state_valid(&copy, &sc_catalog));
    assert_idle();
    assert(sc_state_valid(&state, &sc_catalog));
    assert_idle();

    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.state = state;
    assert(sc_save_encode(&snapshot, &sc_catalog, 1, bytes) == SC_SAVE_OK);
    bytes[200] ^= 1;
    assert(sc_save_decode(&snapshot, &sc_catalog, &generation, bytes, SC_SAVE_SIZE) ==
           SC_SAVE_BAD_CRC);
    assert_idle();
    bytes[200] ^= 1;
    assert(sc_save_decode(&snapshot, &sc_catalog, &generation, bytes, SC_SAVE_SIZE) ==
           SC_SAVE_OK);
    assert_idle();

    memset(note, 0, sizeof(note));
    exists = 1;
    copy = state;
    assert(sc_bridge_begin(&copy, &bridge, &sc_catalog, &device, rival,
                           (state.rival_branch + 1) % 3, &storage) != SC_OK);
    assert_idle();
    assert(!memcmp(&copy, &state, sizeof(copy)));
    fail_write = 1;
    assert(sc_bridge_begin(&copy, &bridge, &sc_catalog, &device, rival,
                           state.rival_branch, &storage) == SC_PENDING);
    assert(storage == SC_SAVE_IO && !memcmp(&copy, &state, sizeof(copy)));
    assert_idle();
    fail_write = 0;
    assert(sc_bridge_begin(&copy, &bridge, &sc_catalog, &device, rival,
                           state.rival_branch, &storage) == SC_OK);
    assert(storage == SC_SAVE_OK && copy.phase == SC_BATTLE);
    assert_idle();
}

static void test_nested_callbacks(void) {
    ScState state, copy;
    ScBridge bridge;
    int storage;
    unsigned rival = find("OaksLab/rival-lab");

    fresh(&state);
    memset(note, 0, sizeof(note));
    exists = 1;
    nested_state = state;
    nested_encounter = rival;
    nested_variant = state.rival_branch;
    copy = state;
    nested_calls = 0;
    nest_bridge = 1;
    assert(sc_bridge_begin(&copy, &bridge, &sc_catalog, &device, rival,
                           state.rival_branch, &storage) == SC_OK);
    assert(nested_calls == 1 && storage == SC_SAVE_OK && copy.phase == SC_BATTLE);
    assert(!memcmp(&nested_state, &state, sizeof(state)));
    assert_idle();

    /* A saved route state: the menu load path reads it through the device. */
    {
        ScSnapshot snapshot;
        memset(&snapshot, 0, sizeof(snapshot));
        snapshot.state = state;
        memset(note, 0, sizeof(note));
        exists = 0;
        assert(sc_storage_store(&device, &sc_catalog, &snapshot, 1) == SC_SAVE_OK);
    }
    assert(sc_ui_init(&ui, &sc_catalog, &device, 5) == SC_OK);
    assert(sc_ui_init(&nested_ui, &sc_catalog, &device, 6) == SC_OK);
    assert(ui.ids[ui.list.cursor] == 0);
    nested_calls = 0;
    nest_input = 1;
    assert(sc_ui_input(&ui, RL_UI_A) == SC_UI_STAY);
    assert(nested_calls == 1 && ui.active && ui.storage_status == SC_SAVE_OK);
    assert(!memcmp(&ui.state, &state, sizeof(state)));
    assert_idle();
    /* Retry after refusal on the formerly nested UI proceeds normally. */
    assert(sc_ui_input(&nested_ui, RL_UI_A) == SC_UI_STAY);
    assert(nested_ui.active && !memcmp(&nested_ui.state, &state, sizeof(state)));
    assert_idle();
}

int main(void) {
    assert_idle();
    test_held_refusal();
    test_error_release_and_retry();
    test_nested_callbacks();
    puts("story scratch guard tests passed");
    return 0;
}
