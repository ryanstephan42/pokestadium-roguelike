#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/story/sc_ui.h"
#include "../src/story/sc_scene.h"
#include "../src/fragments/rl/rl_ui.h"

static ScUi ui, continued;
static ScUiBattleRequest request;
static ScBattleSnapshot party[6];
static RlU8 note[SC_SAVE_NOTE_SIZE];
static int exists, fail_write;
static unsigned draws, frames, screens;

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

void rl_ui_text(s16 x, s16 y, const char *str, Color_RGBA8 color) {
    (void)color;
    assert(str && x >= 0 && x < 640 && y >= 0 && y < 480);
    draws++;
}
void rl_ui_text_wrap(s16 x, s16 y, s16 width, unsigned lines,
                     const char *str, Color_RGBA8 color) {
    assert(width > 0 && x + width <= 640 && lines);
    rl_ui_text(x, y, str, color);
}
void rl_ui_window(s16 x, s16 y, s16 width, s16 height) {
    assert(x >= 0 && y >= 0 && x + width <= 640 && y + height <= 480);
}
void rl_ui_list_draw(const RlUiList *list, s16 x, s16 y, s16 width) {
    int i;
    assert(rl_ui_list_valid(list) && list->visible == 7);
    assert(x + width <= 640 && y + 54 + list->visible * 26 <= 480);
    for (i = 0; i < list->count; i++) assert(strlen(list->items[i].text) <= 32);
}
const char *rl_ui_move_name(unsigned move) { assert(move <= 165); return "Canonical move name"; }
const char *rl_ui_species_name(unsigned species) { assert(species && species <= 151); return "Canonical species"; }
s32 rl_ui_mon_icon(s32 species, s16 x, s16 y) {
    assert(species && species <= 151 && x + 40 <= 640 && y + 40 <= 480);
    return 0;
}
void rl_ui_hp_bar(s16 x, s16 y, s16 width, u16 hp, u16 maximum) {
    assert(x + width <= 640 && y < 480 && maximum && hp <= maximum);
}

static int frame(ScUi *controller, unsigned pressed) {
    int result = sc_ui_input(controller, pressed);
    assert(rl_ui_list_valid(&controller->list));
    sc_scene_draw(controller);
    frames++;
    screens |= 1U << controller->screen;
    return result;
}

/* Test fixture for buttonPressed edge delivery; not a new native input driver. */
static int physical_frame(ScUi *controller, unsigned held, unsigned *previous) {
    unsigned pressed = held & ~*previous;
    *previous = held;
    return frame(controller, pressed);
}
static void tap(ScUi *controller, unsigned button) {
    frame(controller, button);
    frame(controller, 0);
}
static void move_to(ScUi *controller, unsigned row) {
    assert(row < (unsigned)controller->list.count);
    while ((unsigned)controller->list.cursor < row) tap(controller, RL_UI_DOWN);
    while ((unsigned)controller->list.cursor > row) tap(controller, RL_UI_UP);
}
static unsigned index_of(const ScUi *controller, unsigned id) {
    unsigned i;
    for (i = 0; i < (unsigned)controller->list.count; i++) if (controller->ids[i] == id) return i;
    assert(!"Missing actual menu row");
    return 0;
}
static void select(ScUi *controller, unsigned id) {
    move_to(controller, index_of(controller, id));
    tap(controller, RL_UI_A);
}
static void confirm(ScUi *controller) {
    assert(controller->screen == SC_UI_CONFIRM && controller->list.cursor == 0);
    select(controller, 1);
}
static void copy_snapshot(void) {
    unsigned i, j;
    for (i = 0; i < ui.state.party_count; i++) {
        party[i].hp = ui.state.party[i].cur_hp;
        party[i].status = ui.state.party[i].status;
        for (j = 0; j < 4; j++) party[i].pp[j] = ui.state.party[i].pp[j];
    }
}
static void sweep_bounds(ScUi *controller) {
    unsigned i;
    int count = controller->list.count;
    for (i = 0; i < (unsigned)count + 3; i++) tap(controller, RL_UI_DOWN);
    assert(controller->list.cursor == count - 1);
    for (i = 0; i < (unsigned)count + 3; i++) tap(controller, RL_UI_RIGHT);
    assert(controller->list.cursor == count - 1);
    for (i = 0; i < (unsigned)count + 3; i++) tap(controller, RL_UI_LEFT);
    assert(controller->list.cursor == 0 && controller->list.scroll == 0);
    tap(controller, RL_UI_UP);
    assert(controller->list.cursor == 0);
}
static void input_edges(void) {
    unsigned previous = 0;
    assert(sc_ui_init(&ui, &sc_catalog, &device, 123) == SC_OK);
    physical_frame(&ui, RL_UI_DOWN, &previous);
    assert(ui.list.cursor == 1);
    physical_frame(&ui, RL_UI_DOWN, &previous);
    assert(ui.list.cursor == 1);
    physical_frame(&ui, 0, &previous);
    physical_frame(&ui, RL_UI_DOWN, &previous);
    assert(ui.list.cursor == 2);
    /* Each explicitly delivered repeat pulse advances once; no repeat timer here. */
    frame(&ui, RL_UI_UP); frame(&ui, RL_UI_UP);
    assert(ui.list.cursor == 0);
    assert(frame(&ui, RL_UI_A | RL_UI_B) == SC_UI_EXIT);
    assert(ui.screen == SC_UI_MENU);
    frame(&ui, RL_UI_UP | RL_UI_DOWN | RL_UI_A);
    assert(ui.screen == SC_UI_MENU && ui.list.cursor == 0);
    frame(&ui, RL_UI_DOWN | RL_UI_A);
    assert(ui.screen == SC_UI_MENU && ui.list.cursor == 1);
}
static void create(void) {
    ScTemplate starter;
    ScSnapshot checkpoint;
    input_edges();
    sweep_bounds(&ui);
    select(&ui, 1);
    assert(ui.screen == SC_UI_CREATE);
    sweep_bounds(&ui);
    select(&ui, 3);
    select(&ui, 5);
    tap(&ui, RL_UI_B);
    assert(ui.screen == SC_UI_CREATE && !exists);
    select(&ui, 5); confirm(&ui);
    assert(ui.screen == SC_UI_REGIONS && ui.state.options.difficulty == SC_STANDARD);
    /* Canonical level-boundary host fixture guarantees an actual pending learning API transition. */
    memset(&starter, 0, sizeof(starter)); starter.species = 1; starter.level = 6;
    assert(sc_make_mon(&sc_catalog, &starter, 0x8889, &ui.state.party[0]) == SC_OK);
    ui.state.party[0].exp = sc_xp(7, ui.state.party[0].growth) - 1;
    checkpoint.state = ui.state; checkpoint.interrupted = 0;
    assert(sc_storage_store(&device, &sc_catalog, &checkpoint, 0) == SC_SAVE_OK);
}
static void first_battle(void) {
    RlU32 ticket;
    ScState before;
    unsigned i, region = 0, encounter;
    for (encounter = 0; encounter < sc_catalog.count; encounter++)
        if (!strcmp(sc_catalog.encounters[encounter].id, "OaksLab/rival-lab")) break;
    assert(encounter < sc_catalog.count);
    select(&ui, 0); assert(ui.screen == SC_UI_PARTY);
    sweep_bounds(&ui); select(&ui, 0); assert(ui.screen == SC_UI_MON);
    select(&ui, 1); assert(ui.screen == SC_UI_EVOLVE);
    tap(&ui, RL_UI_B); tap(&ui, RL_UI_B); tap(&ui, RL_UI_B);
    assert(ui.screen == SC_UI_REGIONS);
    for (i = 0; i < (unsigned)ui.list.count; i++)
        if (!strcmp(ui.labels[i], "OaksLab")) region = ui.ids[i];
    assert(region);
    select(&ui, region); assert(ui.screen == SC_UI_ENCOUNTERS);
    sweep_bounds(&ui);
    select(&ui, encounter);
    assert(ui.screen == SC_UI_CONFIRM && !ui.request_ready);
    move_to(&ui, 1);
    assert(frame(&ui, RL_UI_A) == SC_UI_REQUEST);
    frame(&ui, 0);
    assert(ui.screen == SC_UI_BATTLE && ui.request_ready);
    assert(sc_ui_battle_request(&ui, &request) == SC_OK && request.enemy_count == 1);
    ticket = request.operation_id;
    before = ui.state;
    tap(&ui, RL_UI_B); assert(ui.status == SC_PENDING && ui.screen == SC_UI_BATTLE);
    copy_snapshot();
    assert(sc_ui_observe(&ui, ticket + 1, party, 0, 0, 0) == SC_INVALID);
    assert(!memcmp(&before, &ui.state, sizeof(before)));
    party[0].hp--; party[0].pp[0]--; party[0].status = 8;
    assert(sc_ui_observe(&ui, ticket, party, 0, 0, 0) == SC_OK);
    assert(ui.screen == SC_UI_LEARN && ui.state.learn_mask == 1);
    frame(&ui, 0); sweep_bounds(&ui);
    select(&ui, 0); confirm(&ui);
    assert(ui.screen == SC_UI_BATTLE && ui.state.party[0].moves[0] == 73);
    assert(sc_ui_finish(&ui, ticket, SC_WIN) == SC_OK && ui.screen == SC_UI_REWARD);
    frame(&ui, 0); sweep_bounds(&ui);
    select(&ui, 0);
    before = ui.state;
    fail_write = 1; confirm(&ui);
    assert(ui.screen == SC_UI_SAVE_ERROR && !memcmp(&before, &ui.state, sizeof(before)));
    tap(&ui, RL_UI_B); assert(ui.screen == SC_UI_SAVE_ERROR);
    fail_write = 0; select(&ui, 0);
    assert(ui.screen == SC_UI_REGIONS && ui.state.party_count == 2 && !ui.pending_save);
    assert(ui.state.party[0].status == 8 && ui.state.party[0].pp[1] == before.party[0].pp[1]);
}
static void resume_and_terminal(void) {
    unsigned i, count, region, encounter;
    int cursor;
    ScState saved = ui.state;
    select(&ui, 3);
    assert(ui.screen == SC_UI_MENU);
    assert(sc_ui_init(&continued, &sc_catalog, &device, 999) == SC_OK);
    frame(&continued, RL_UI_A);
    assert(continued.screen == SC_UI_REGIONS && !memcmp(&continued.state, &saved, sizeof(saved)));
    sweep_bounds(&continued);
    region = 0;
    for (i = 5; i < (unsigned)continued.list.count; i++)
        if (!strcmp(continued.labels[i], "ViridianForest")) region = continued.ids[i];
    assert(region);
    select(&continued, region);
    assert(continued.screen == SC_UI_ENCOUNTERS);
    sweep_bounds(&continued);
    count = (unsigned)continued.list.count;
    for (i = 0; i < count; i++) {
        move_to(&continued, i);
        cursor = continued.list.cursor;
        frame(&continued, 0);
        assert(continued.list.cursor == cursor);
    }
    encounter = continued.ids[0];
    select(&continued, encounter); confirm(&continued);
    assert(continued.screen == SC_UI_BATTLE);
    assert(sc_ui_battle_request(&continued, &request) == SC_OK);
    assert(sc_ui_finish(&continued, request.operation_id, SC_LOSS) == SC_OK);
    assert(continued.screen == SC_UI_TERMINAL && continued.state.phase == SC_LOST);
    frame(&continued, 0);
    select(&continued, 1);
    frame(&continued, RL_UI_A);
    assert(continued.screen == SC_UI_TERMINAL && continued.state.phase == SC_LOST);
}

int main(void) {
    create();
    first_battle();
    resume_and_terminal();
    assert(draws && frames && (screens & (1U << SC_UI_SAVE_ERROR)) &&
           (screens & (1U << SC_UI_LEARN)) && (screens & (1U << SC_UI_TERMINAL)));
    printf("Story input/renderer flow: %u frames, %u text draws; host callbacks only; OK\n", frames, draws);
    return 0;
}
