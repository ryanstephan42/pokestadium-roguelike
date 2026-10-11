#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/story/sc_ui.h"
#include "../src/story/sc_scene.h"
#include "../src/fragments/rl/rl_ui.h"

#ifdef SC_MOON_UI_FIXTURE
static ScCatalog moon_ui_catalog;
static void init_moon_fixture(void) {
    moon_ui_catalog = sc_catalog;
    if (!moon_ui_catalog.graph_reviewed) {
        puts("Testing unreviewed Moon UI extension as an explicit host fixture");
        moon_ui_catalog.graph_reviewed = 1;
    }
}
#define sc_catalog moon_ui_catalog
#endif

/* All battle outcomes below are host fixtures, not native battle proof. */
static ScUi ui, cold;
static ScState before;
static ScUiBattleRequest request;
static ScBattleSnapshot snapshots[6];
static RlU8 note[SC_SAVE_NOTE_SIZE];
static int exists, fail_write, fail_open;
static unsigned battles, learnings, recruits, transfers, drawings;

static int device_open(void *context, unsigned company, RlU32 game,
                        const RlU8 name[16], const RlU8 extension[4],
                        unsigned size, int create) {
    (void)context;
    assert(company == SC_SAVE_COMPANY && game == SC_SAVE_GAME);
    assert(!memcmp(name, sc_save_name, 16) && !memcmp(extension, sc_save_extension, 4));
    assert(size == sizeof(note));
    if (fail_open) return fail_open;
    if (!exists && !create) return SC_SAVE_NOT_FOUND;
    exists = 1;
    return SC_SAVE_OK;
}

static int device_read(void *context, unsigned offset, RlU8 *out, unsigned size) {
    (void)context;
    assert(offset + size <= sizeof(note));
    memcpy(out, note + offset, size);
    return SC_SAVE_OK;
}

static int device_write(void *context, unsigned offset, const RlU8 *in, unsigned size) {
    (void)context;
    assert(offset + size <= sizeof(note));
    if (fail_write) return SC_SAVE_IO;
    memcpy(note + offset, in, size);
    return SC_SAVE_OK;
}

static const ScSaveDevice device = {NULL, device_open, device_read, device_write};

void rl_ui_text(s16 x, s16 y, const char *str, Color_RGBA8 color) {
    (void)color;
    assert(x >= 0 && x < 640 && y >= 0 && y < 480 && str);
    drawings++;
}
void rl_ui_text_wrap(s16 x, s16 y, s16 width, unsigned lines,
                     const char *str, Color_RGBA8 color) {
    assert(width > 0 && x + width <= 640 && lines > 0);
    rl_ui_text(x, y, str, color);
}
void rl_ui_window(s16 x, s16 y, s16 w, s16 h) {
    assert(x >= 0 && y >= 0 && x + w <= 640 && y + h <= 480);
}
void rl_ui_list_draw(const RlUiList *list, s16 x, s16 y, s16 w) {
    int i;
    assert(rl_ui_list_valid(list));
    assert(x + w <= 640 && y + 54 + list->visible * 26 <= 480);
    for (i = 0; i < list->count; i++) assert(strlen(list->items[i].text) <= 32);
}
const char *rl_ui_move_name(unsigned move) { assert(move <= 165); return "Move"; }
const char *rl_ui_species_name(unsigned species) { assert(species && species <= 151); return "Pokemon"; }
s32 rl_ui_mon_icon(s32 species, s16 x, s16 y) {
    assert(species && species <= 151 && x + 40 <= 640 && y + 40 <= 480);
    return 0;
}
void rl_ui_hp_bar(s16 x, s16 y, s16 w, u16 hp, u16 maximum) {
    assert(x + w <= 640 && y < 480 && maximum && hp <= maximum);
}

static int input(unsigned pressed) {
    int result = sc_ui_input(&ui, pressed);
    assert(rl_ui_list_valid(&ui.list));
    sc_scene_draw(&ui);
    return result;
}

static int choose(unsigned id) {
    unsigned i;
    for (i = 0; i < (unsigned)ui.list.count; i++) if (ui.ids[i] == id) break;
    assert(i < (unsigned)ui.list.count);
    while (ui.list.cursor > (int)i) input(RL_UI_UP);
    while (ui.list.cursor < (int)i) input(RL_UI_DOWN);
    return input(RL_UI_A);
}

static int confirm_choice(void) {
    assert(ui.screen == SC_UI_CONFIRM && ui.list.cursor == 0);
    return choose(1);
}

static void reset(unsigned version, unsigned difficulty) {
    memset(note, 0, sizeof(note));
    exists = fail_write = fail_open = 0;
    assert(sc_ui_init(&ui, &sc_catalog, &device, 123) == SC_OK);
    choose(1);
    assert(ui.screen == SC_UI_CREATE);
    if (version) choose(0);
    while (ui.options.difficulty != difficulty) choose(1);
    choose(3); /* Deterministic Bulbasaur for the traversal fixture. */
    choose(5);
    before = ui.state;
    assert(ui.screen == SC_UI_CONFIRM);
    confirm_choice();
    assert(ui.screen == SC_UI_REGIONS && ui.state.party[0].species == 1);
    assert(ui.state.options.version == version && ui.state.options.difficulty == difficulty);
    assert(sc_state_valid(&ui.state, &sc_catalog));
}

static unsigned find_node(const char *id) {
    unsigned i;
    for (i = 0; i < sc_catalog.graph->count; i++)
        if (!strcmp(id, sc_catalog.graph->nodes[i].id)) return i;
    assert(!"Unknown canonical node");
    return 0;
}

static void enter_node(unsigned node) {
    char area[72];
    unsigned i, region_id = 0;
    assert(ui.screen == SC_UI_REGIONS);
    if (node >= sc_catalog.count) choose(2);
    else {
        for (i = 0; sc_catalog.encounters[node].id[i] != '/'; i++)
            area[i] = sc_catalog.encounters[node].id[i];
        area[i] = 0;
        for (i = 5; i < (unsigned)ui.list.count; i++)
            if (!strcmp(ui.labels[i], area)) { region_id = ui.ids[i]; break; }
        assert(region_id);
        choose(region_id);
    }
    choose(node);
    if (ui.screen == SC_UI_VARIANT) choose(0);
    confirm_choice();
}

static void settle_learning(void) {
    while (ui.state.phase == SC_LEARNING) {
        assert(ui.screen == SC_UI_LEARN && ui.slot < ui.state.party_count);
        before = ui.state;
        input(RL_UI_B);
        assert(ui.status == SC_PENDING && !memcmp(&before, &ui.state, sizeof(before)));
        choose(3);
        input(RL_UI_B);
        assert(ui.screen == SC_UI_LEARN);
        choose(4);
        confirm_choice();
        learnings++;
    }
}

static void snapshots_from_party(void) {
    unsigned i, j;
    memset(snapshots, 0, sizeof(snapshots));
    for (i = 0; i < ui.state.party_count; i++) {
        snapshots[i].hp = ui.state.party[i].cur_hp;
        snapshots[i].status = ui.state.party[i].status;
        for (j = 0; j < 4; j++) snapshots[i].pp[j] = ui.state.party[i].pp[j];
    }
}

static void win_fixture(void) {
    unsigned i;
    RlU32 ticket;
    assert(sc_ui_battle_request(&ui, &request) == SC_OK);
    ticket = request.operation_id;
    assert(request.party_count == ui.state.party_count);
    assert(request.variant == ui.state.encounter_variant);
    before = ui.state;
    snapshots_from_party();
    assert(sc_ui_observe(&ui, ticket + 1, snapshots, 0, 0, 0) == SC_INVALID);
    assert(sc_ui_finish(&ui, ticket + 1, SC_LOSS) == SC_INVALID);
    assert(!memcmp(&before, &ui.state, sizeof(before)));
    assert(sc_ui_finish(&ui, ticket, SC_WIN) == SC_PENDING);
    for (i = 0; i < request.enemy_count; i++) {
        snapshots_from_party();
        assert(sc_ui_observe(&ui, ticket, snapshots, 0, i, 0) == SC_OK);
        settle_learning();
    }
    assert(sc_ui_finish(&ui, ticket, SC_WIN) == SC_OK);
    assert(sc_ui_finish(&ui, ticket, SC_LOSS) == SC_INVALID);
    assert(sc_state_valid(&ui.state, &sc_catalog));
    battles++;
}

static void resolve_reward(void) {
    unsigned i, j, k;
    if (ui.screen != SC_UI_REWARD) return;
    if (ui.state.party_count < 6) {
        choose(0); confirm_choice(); recruits++;
    } else if (!transfers) {
        for (i = 0; i < 4; i++) {
            if (!ui.state.offer.moves[i]) continue;
            for (j = 0; j < ui.state.party_count; j++) {
                for (k = 0; k < 4; k++)
                    if (ui.state.party[j].moves[k] == ui.state.offer.moves[i]) break;
                if (k == 4) break;
            }
            if (j < ui.state.party_count) break;
        }
        if (i < 4) {
            RlU8 move = ui.state.offer.moves[i];
            choose(1); choose(i); choose(j); choose(3); confirm_choice();
            assert(ui.state.party[j].moves[3] == move);
            assert(ui.state.party[j].pp[3] == sc_catalog.pp[move]);
            transfers++;
        } else { choose(2); confirm_choice(); }
    } else { choose(2); confirm_choice(); }
    assert(ui.screen == SC_UI_REGIONS && sc_state_valid(&ui.state, &sc_catalog));
}

static void test_creation_bounds_and_saves(void) {
    unsigned i;
    ScCatalog bad = sc_catalog;
    bad.graph_reviewed = 0;
    assert(sc_ui_init(&cold, &bad, &device, 1) == SC_UNREVIEWED);
    exists = fail_write = fail_open = 0;
    memset(note, 0, sizeof(note));
    assert(sc_ui_init(&ui, &sc_catalog, &device, 7) == SC_OK);
    choose(0);
    assert(ui.screen == SC_UI_MENU && ui.storage_status == SC_SAVE_NOT_FOUND);
    choose(1);
    for (i = 0; i < 20; i++) input(RL_UI_UP);
    assert(ui.list.cursor == 0);
    for (i = 0; i < 20; i++) input(RL_UI_DOWN);
    assert(ui.list.cursor == 5 && ui.list.scroll == 0);
    choose(4); input(RL_UI_RIGHT); assert(ui.seed == 8);
    input(RL_UI_LEFT); assert(ui.seed == 7);
    choose(2); assert(ui.options.starter_pool == SC_UNEVOLVED);
    choose(3); assert(ui.options.starter_pool == SC_STARTER_TRIO);
    choose(5);
    input(RL_UI_B); assert(ui.screen == SC_UI_CREATE && !exists);
    choose(5);
    fail_write = 1;
    confirm_choice();
    assert(ui.screen == SC_UI_SAVE_ERROR && ui.pending_save && !ui.active);
    input(RL_UI_B); assert(ui.screen == SC_UI_SAVE_ERROR);
    fail_write = 0;
    choose(0);
    assert(ui.screen == SC_UI_REGIONS && ui.active);
    before = ui.state;
    assert(sc_ui_unsupported(&ui) == SC_UI_STAY);
    assert(!memcmp(&before, &ui.state, sizeof(before)));
    choose(3);
    assert(ui.screen == SC_UI_MENU);
    assert(sc_ui_init(&cold, &sc_catalog, &device, 99) == SC_OK);
    sc_ui_input(&cold, RL_UI_A);
    assert(cold.screen == SC_UI_REGIONS && !memcmp(&cold.state, &before, sizeof(before)));
    fail_open = SC_SAVE_DEVICE_CHANGED;
    choose(0);
    assert(ui.storage_status == SC_SAVE_DEVICE_CHANGED && ui.screen == SC_UI_MENU);
    fail_open = 0;
    choose(0);
    choose(3);
    assert(ui.screen == SC_UI_MENU);
}

static void test_healing(unsigned difficulty) {
    unsigned i;
    reset(0, difficulty);
    ui.state.party[0].cur_hp--;
    ui.state.party[0].pp[0]--;
    ui.state.party[0].status = 8;
    before = ui.state;
    choose(1);
    if (difficulty == SC_BRUTAL) {
        choose(0);
        assert(ui.status == SC_UNAVAILABLE && !memcmp(&before, &ui.state, sizeof(before)));
        input(RL_UI_B);
        return;
    }
    choose(0); confirm_choice();
    assert(ui.state.party[0].cur_hp == ui.state.party[0].max_hp);
    assert(ui.state.party[0].pp[0] == ui.state.party[0].max_pp[0] && !ui.state.party[0].status);
    assert(ui.state.options.difficulty == difficulty);
    choose(1);
    for (i = 0; i < (unsigned)ui.list.count; i++)
        if (ui.ids[i] == 0) assert(ui.items[i].disabled == (difficulty == SC_LIMITED));
    if (difficulty == SC_LIMITED) {
        choose(0); assert(ui.status == SC_UNAVAILABLE); input(RL_UI_B);
    } else { choose(0); confirm_choice(); }
}

static void test_loss_and_interruption(void) {
    RlU32 ticket;
    unsigned lab = find_node("OaksLab/rival-lab");
    reset(0, SC_STANDARD);
    enter_node(lab);
    assert(sc_ui_battle_request(&ui, &request) == SC_OK);
    ticket = request.operation_id;
    assert(sc_ui_init(&cold, &sc_catalog, &device, 55) == SC_OK);
    sc_ui_input(&cold, RL_UI_A);
    assert(cold.state.phase == SC_LOST && cold.screen == SC_UI_TERMINAL);
    fail_write = 1;
    assert(sc_ui_finish(&ui, ticket, SC_DRAW) == SC_PENDING);
    assert(ui.screen == SC_UI_SAVE_ERROR && ui.state.phase == SC_BATTLE);
    fail_write = 0; choose(0);
    assert(ui.state.phase == SC_LOST && ui.screen == SC_UI_TERMINAL);
    choose(1); choose(0);
    assert(ui.state.phase == SC_LOST && ui.screen == SC_UI_TERMINAL);
}

static void test_checkpoint_failure(void) {
    unsigned lab = find_node("OaksLab/rival-lab");
    reset(0, SC_STANDARD);
    before = ui.state;
    fail_write = 1;
    enter_node(lab);
    assert(ui.screen == SC_UI_ENCOUNTERS && ui.storage_status == SC_SAVE_IO);
    assert(!ui.request_ready && !memcmp(&before, &ui.state, sizeof(before)));
    fail_write = 0;
    choose(lab); confirm_choice(); win_fixture(); resolve_reward();
}

static void test_full_graph(unsigned version) {
    unsigned i, passes, encounters = 0, actions = 0;
    reset(version, SC_STANDARD);
    for (passes = 0; passes < sc_catalog.graph->count; passes++) {
        ScState probe = ui.state;
        for (i = 0; i < sc_catalog.count; i++)
            if (sc_available(&ui.state, &sc_catalog, i)) break;
        if (i == sc_catalog.count) {
            for (; i < sc_catalog.graph->count; i++) {
                probe = ui.state;
                if (sc_action(&probe, &sc_catalog, i, 0,
                    sc_catalog.graph->nodes[i].destination_warp) == SC_OK) break;
            }
        }
        if (i == sc_catalog.graph->count) break;
        enter_node(i);
        if (i < sc_catalog.count) {
            encounters++;
            if (ui.state.phase == SC_BATTLE) win_fixture();
            resolve_reward();
        } else actions++;
        assert(ui.screen == SC_UI_REGIONS && sc_state_valid(&ui.state, &sc_catalog));
    }
    assert(encounters > 330 && actions == 22U + sc_catalog.graph->pickup_count && ui.state.party_count == 6);
    assert(ui.state.completed[sc_catalog.graph->champion / 8] &
        (1U << (sc_catalog.graph->champion % 8)));
    choose(4);
    input(RL_UI_B);
    assert(ui.screen == SC_UI_REGIONS);
    choose(4); confirm_choice();
    assert(ui.state.phase == SC_FINISHED && ui.screen == SC_UI_TERMINAL);
    choose(1); choose(0);
    assert(ui.state.phase == SC_FINISHED);
}

static void test_level_evolution_fixture(void) {
    RlU8 learning;
    ScTemplate mon;
    reset(0, SC_STANDARD);
    /* Prepare a real level-16 Bulbasaur; no native XP claim is made. */
    memset(&mon, 0, sizeof(mon)); mon.species = 1; mon.level = 15;
    assert(sc_make_mon(&sc_catalog, &mon, 0x8889, &ui.state.party[0]) == SC_OK);
    assert(sc_add_xp(&ui.state.party[0],
        sc_xp(16, ui.state.party[0].growth) - ui.state.party[0].exp,
        &learning, &sc_catalog) == SC_OK);
    ui.state.evolution_mask = 1;
    ui.state.party[0].cur_hp--;
    before = ui.state;
    choose(0); choose(0); choose(1);
    assert(ui.screen == SC_UI_EVOLVE && !ui.items[0].disabled);
    choose(0);
    input(RL_UI_B);
    assert(!memcmp(&before, &ui.state, sizeof(before)));
    choose(0); confirm_choice();
    settle_learning();
    assert(ui.state.party[0].species == 2 && ui.state.evolution_mask == 0);
    assert(ui.state.party[0].max_hp - ui.state.party[0].cur_hp == 1);
}

static void fill_party_fixture(void) {
    static const RlU8 species[6] = {1, 4, 7, 25, 133, 63};
    ScTemplate mon;
    unsigned i;
    memset(&mon, 0, sizeof(mon));
    mon.level = 5;
    for (i = 0; i < 6; i++) {
        mon.species = species[i];
        assert(sc_make_mon(&sc_catalog, &mon, 0x8889, &ui.state.party[i]) == SC_OK);
    }
    ui.state.party_count = 6;
    assert(sc_state_valid(&ui.state, &sc_catalog));
}

static void test_replacement_and_duplicate(void) {
    unsigned donor, i;
    RlU32 old_ticket;
    reset(0, SC_STANDARD);
    fill_party_fixture();
    enter_node(find_node("OaksLab/rival-lab"));
    assert(sc_ui_battle_request(&ui, &request) == SC_OK);
    old_ticket = request.operation_id;
    win_fixture();
    before = ui.state;
    for (donor = 0; donor < 4; donor++)
        if (ui.state.offer.moves[donor] == ui.state.party[0].moves[1]) break;
    assert(donor < 4);
    choose(1); choose(donor); choose(0); choose(0); confirm_choice();
    assert(ui.status == SC_DUPLICATE && ui.screen == SC_UI_MOVE);
    assert(!memcmp(&before, &ui.state, sizeof(before)));
    input(RL_UI_B); input(RL_UI_B);
    choose(0);
    assert(ui.screen == SC_UI_REPLACE && ui.list.count == 6);
    choose(3); input(RL_UI_B);
    assert(ui.screen == SC_UI_REPLACE && !memcmp(&before, &ui.state, sizeof(before)));
    choose(3);
    fail_write = 1;
    confirm_choice();
    assert(ui.screen == SC_UI_SAVE_ERROR && !memcmp(&before, &ui.state, sizeof(before)));
    fail_write = 0; choose(0);
    assert(ui.state.party_count == 6 && !memcmp(&ui.state.party[3], &before.offer, sizeof(RlMon)));
    for (i = 0; i < 6; i++) if (i != 3)
        assert(!memcmp(&ui.state.party[i], &before.party[i], sizeof(RlMon)));
    assert(sc_state_valid(&ui.state, &sc_catalog));
    for (i = 0; i < sc_catalog.count; i++)
        if (sc_available(&ui.state, &sc_catalog, i)) break;
    assert(i < sc_catalog.count);
    enter_node(i);
    snapshots_from_party();
    before = ui.state;
    assert(sc_ui_observe(&ui, old_ticket, snapshots, 0, 0, 0) == SC_INVALID);
    assert(sc_ui_finish(&ui, old_ticket, SC_LOSS) == SC_INVALID);
    assert(!memcmp(&before, &ui.state, sizeof(before)));
    assert(sc_ui_finish(&ui, ui.operation_id, SC_LOSS) == SC_OK);
}

static void test_unsupported_evolutions(void) {
    unsigned i, member;
    reset(0, SC_STANDARD);
    fill_party_fixture();
    for (member = 3; member < 6; member++) {
        /* Pikachu/Eevee/Abra exercise stone/trade permissions without toggles. */
        ui.state.party[member].level = 30;
        ui.state.party[member].exp = sc_xp(30, ui.state.party[member].growth);
        sc_recalculate(&ui.state.party[member], 1);
        choose(0); choose(member); choose(1);
        assert(ui.screen == SC_UI_EVOLVE);
        for (i = 0; i < (unsigned)ui.list.count; i++) {
            unsigned id = ui.ids[i];
            const ScSpecies *species = &sc_catalog.species[ui.state.party[member].species];
            if (id < species->evolution_count &&
                species->evolution[id].method != SC_LEVEL_EVOLUTION) {
                before = ui.state;
                choose(id);
                assert(ui.status == SC_UNAVAILABLE && !memcmp(&before, &ui.state, sizeof(before)));
            }
        }
        input(RL_UI_B); input(RL_UI_B); input(RL_UI_B);
        assert(ui.screen == SC_UI_REGIONS);
    }
}

static void test_pending_learning_fixture(void) {
    ScTemplate mon;
    RlU32 ticket;
    reset(0, SC_STANDARD);
    fill_party_fixture();
    memset(&mon, 0, sizeof(mon)); mon.species = 1; mon.level = 6;
    assert(sc_make_mon(&sc_catalog, &mon, 0x8889, &ui.state.party[0]) == SC_OK);
    mon.species = 4; mon.level = 8;
    assert(sc_make_mon(&sc_catalog, &mon, 0x8889, &ui.state.party[1]) == SC_OK);
    ui.state.party[0].exp = sc_xp(7, ui.state.party[0].growth) - 1;
    ui.state.party[1].exp = sc_xp(9, ui.state.party[1].growth) - 1;
    enter_node(find_node("OaksLab/rival-lab"));
    ticket = ui.operation_id;
    snapshots_from_party();
    assert(sc_ui_observe(&ui, ticket, snapshots, 1, 0, 1) == SC_OK);
    assert(sc_ui_observe(&ui, ticket, snapshots, 0, 0, 0) == SC_OK);
    assert(ui.state.learn_mask == 3 && ui.screen == SC_UI_LEARN && ui.slot == 0);
    before = ui.state;
    assert(sc_ui_observe(&ui, ticket, snapshots, 0, 0, 0) == SC_PENDING);
    assert(!memcmp(&before, &ui.state, sizeof(before)));
    choose(0); confirm_choice();
    assert(ui.state.party[0].moves[0] == 73 && ui.state.party[0].pp[0] == sc_catalog.pp[73]);
    assert(ui.screen == SC_UI_LEARN && ui.slot == 1 && ui.state.learn_mask == 2);
    choose(4); confirm_choice();
    assert(ui.state.phase == SC_BATTLE);
    assert(sc_ui_finish(&ui, ticket, SC_WIN) == SC_OK);
    resolve_reward();
}

int main(void) {
#ifdef SC_MOON_UI_FIXTURE
    init_moon_fixture();
#endif
    test_creation_bounds_and_saves();
    test_healing(SC_STANDARD);
    test_healing(SC_LIMITED);
    test_healing(SC_BRUTAL);
    test_loss_and_interruption();
    test_checkpoint_failure();
    test_level_evolution_fixture();
    test_replacement_and_duplicate();
    test_unsupported_evolutions();
    test_pending_learning_fixture();
    test_full_graph(0);
    test_full_graph(1);
    assert(learnings && recruits >= 10 && transfers && battles > 650 && drawings);
    printf("Story UI host fixtures: %u battles, %u learning choices, %u recruits, %u transfers; OK\n",
        battles, learnings, recruits, transfers);
    return 0;
}
