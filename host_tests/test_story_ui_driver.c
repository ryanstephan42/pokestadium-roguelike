#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/story/sc_ui_driver.h"

static ScUi ui;
static ScUiDriver driver;
static ScUiBattleRequest backing, captured;
static ScUiBattleRequest first_request;
static ScSnapshot checkpoint;
static RlU8 note[SC_SAVE_NOTE_SIZE];
static unsigned launches, draws, leaves;
static int launch_status, fail_write;

static int open_note(void *context, unsigned company, RlU32 game, const RlU8 name[16],
                     const RlU8 extension[4], unsigned size, int create) {
    (void)context; (void)create;
    assert(company == SC_SAVE_COMPANY && game == SC_SAVE_GAME && size == sizeof(note));
    assert(!memcmp(name, sc_save_name, 16) && !memcmp(extension, sc_save_extension, 4));
    return SC_SAVE_OK;
}
static int read_note(void *context, unsigned offset, RlU8 *out, unsigned size) {
    (void)context; assert(offset + size <= sizeof(note));
    memcpy(out, note + offset, size); return SC_SAVE_OK;
}
static int write_note(void *context, unsigned offset, const RlU8 *in, unsigned size) {
    (void)context; assert(offset + size <= sizeof(note));
    if (fail_write) return SC_SAVE_IO;
    memcpy(note + offset, in, size); return SC_SAVE_OK;
}
static const ScSaveDevice device = {NULL, open_note, read_note, write_note};

static int launch(void *context, const ScUiBattleRequest *request) {
    (void)context;
    captured = *request;
    launches++;
    assert(request->operation_id && request->enemy_count && request->party_count == 1);
    assert(sc_storage_load(&device, &sc_catalog, &checkpoint) == SC_SAVE_OK);
    assert(checkpoint.interrupted && checkpoint.state.phase == SC_ROUTE);
    return launch_status;
}
static void draw(void *context, const ScUi *controller) {
    (void)context;
    assert(controller == &ui && rl_ui_list_valid(&controller->list));
    draws++;
}
static void leave(void *context) { (void)context; leaves++; }
static ScUiDriverCalls calls = {NULL, launch, draw, leave};

static int choose(unsigned id) {
    unsigned i;
    for (i = 0; i < (unsigned)ui.list.count; i++) if (ui.ids[i] == id) break;
    assert(i < (unsigned)ui.list.count);
    while (ui.list.cursor < (int)i) assert(sc_ui_driver_step(&driver, RL_UI_DOWN) == SC_OK);
    while (ui.list.cursor > (int)i) assert(sc_ui_driver_step(&driver, RL_UI_UP) == SC_OK);
    return sc_ui_driver_step(&driver, RL_UI_A);
}

int main(void) {
    unsigned region = 0, i, encounter;
    ScBattleSnapshot party[6];
    ScState before;
    assert(sc_ui_init(&ui, &sc_catalog, &device, 123) == SC_OK);
    assert(sc_ui_driver_bind(&driver, &ui, &backing, NULL) == SC_UNAVAILABLE);
    assert(!driver.bound && sc_ui_driver_step(&driver, RL_UI_A) == SC_UNAVAILABLE);
    calls.launch = NULL;
    assert(sc_ui_driver_bind(&driver, &ui, &backing, &calls) == SC_UNAVAILABLE);
    calls.launch = launch;
    assert(sc_ui_driver_bind(&driver, &ui, NULL, &calls) == SC_UNAVAILABLE);
    assert(sc_ui_driver_bind(&driver, &ui, &backing, &calls) == SC_OK);
    assert(sc_ui_driver_observe(&driver, 1, party, 0, 0, 0) == SC_INVALID);
    assert(sc_ui_driver_finish(&driver, 1, SC_WIN) == SC_INVALID);
    assert(choose(1) == SC_OK);
    assert(choose(3) == SC_OK);
    assert(choose(5) == SC_OK);
    assert(choose(1) == SC_OK && ui.screen == SC_UI_REGIONS);
    for (i = 0; i < (unsigned)ui.list.count; i++)
        if (!strcmp(ui.labels[i], "OaksLab")) region = ui.ids[i];
    assert(region && choose(region) == SC_OK);
    for (encounter = 0; encounter < sc_catalog.count; encounter++)
        if (!strcmp(sc_catalog.encounters[encounter].id, "OaksLab/rival-lab")) break;
    assert(choose(encounter) == SC_OK);
    launch_status = SC_UNAVAILABLE;
    assert(choose(1) == SC_UNAVAILABLE);
    assert(driver.launch_pending && !driver.launched && launches == 1);
    before = ui.state;
    assert(sc_ui_driver_step(&driver, 0) == SC_OK && launches == 1);
    assert(sc_ui_driver_step(&driver, RL_UI_B | RL_UI_A) == SC_PENDING && launches == 1);
    assert(sc_ui_driver_step(&driver, RL_UI_DOWN | RL_UI_A) == SC_OK && launches == 1);
    assert(sc_ui_driver_step(&driver, RL_UI_RIGHT | RL_UI_A) == SC_OK && launches == 1);
    assert(!memcmp(&before, &ui.state, sizeof(before)));
    launch_status = SC_OK;
    assert(sc_ui_driver_step(&driver, RL_UI_A) == SC_OK && driver.launched && launches == 2);
    assert(sc_ui_driver_bind(&driver, &ui, &backing, &calls) == SC_INVALID);
    assert(driver.bound && driver.launched && driver.operation_id == captured.operation_id);
    assert(sc_ui_driver_step(&driver, RL_UI_A) == SC_OK && launches == 2);
    memset(party, 0, sizeof(party));
    party[0].hp = ui.state.party[0].cur_hp;
    for (i = 0; i < 4; i++) party[0].pp[i] = ui.state.party[0].pp[i];
    assert(sc_ui_driver_observe(&driver, captured.operation_id + 1, party, 0, 0, 1) == SC_INVALID);
    assert(sc_ui_driver_observe(&driver, captured.operation_id, party, 0, 0, 1) == SC_OK);
    first_request = captured;
    assert(sc_ui_driver_observe(&driver, first_request.operation_id, party, 0, 0, 0) == SC_OK);
    while (ui.screen == SC_UI_LEARN) assert(choose(4) == SC_OK && choose(1) == SC_OK);
    assert(sc_ui_driver_finish(&driver, first_request.operation_id, SC_WIN) == SC_OK);
    assert(!driver.launched && ui.screen == SC_UI_REWARD);
    assert(sc_ui_driver_finish(&driver, first_request.operation_id, SC_WIN) == SC_INVALID);
    assert(choose(2) == SC_OK && choose(1) == SC_OK && ui.screen == SC_UI_REGIONS);
    assert(sc_storage_load(&device, &sc_catalog, &checkpoint) == SC_SAVE_OK);
    assert(!checkpoint.interrupted && checkpoint.state.phase == SC_ROUTE);
    assert(!memcmp(&checkpoint.state, &ui.state, sizeof(ui.state)));
    before = ui.state;
    assert(choose(3) == SC_OK && ui.screen == SC_UI_MENU);
    assert(sc_ui_driver_step(&driver, RL_UI_A) == SC_OK && ui.screen == SC_UI_REGIONS);
    assert(!memcmp(&before, &ui.state, sizeof(before)));
    region = 0;
    for (i = 0; i < (unsigned)ui.list.count; i++)
        if (!strcmp(ui.labels[i], "ViridianForest")) region = ui.ids[i];
    assert(region && choose(region) == SC_OK);
    encounter = ui.ids[0];
    assert(choose(encounter) == SC_OK && choose(1) == SC_OK);
    assert(driver.launched && launches == 3);
    assert(captured.operation_id != first_request.operation_id &&
           captured.encounter != first_request.encounter);
    before = ui.state;
    assert(sc_ui_driver_observe(&driver, first_request.operation_id, party, 0, 0, 1) == SC_INVALID);
    assert(sc_ui_driver_finish(&driver, first_request.operation_id, SC_WIN) == SC_INVALID);
    assert(!memcmp(&before, &ui.state, sizeof(before)));
    fail_write = 1;
    assert(sc_ui_driver_finish(&driver, captured.operation_id, SC_LOSS) == SC_PENDING);
    assert(!driver.launched && ui.screen == SC_UI_SAVE_ERROR);
    assert(sc_ui_driver_finish(&driver, captured.operation_id, SC_WIN) == SC_INVALID);
    fail_write = 0;
    assert(choose(0) == SC_OK && ui.state.phase == SC_LOST && ui.screen == SC_UI_TERMINAL);
    assert(choose(1) == SC_OK && ui.screen == SC_UI_MENU);
    assert(choose(2) == SC_OK && leaves == 1 && driver.exit_requested);
    assert(sc_ui_driver_step(&driver, 0) == SC_UNAVAILABLE && leaves == 1);
    assert(draws);
    puts("Typed story UI driver: refusal/retry, two sequential operations, save/continue, stale completion and terminal; OK");
    return 0;
}
