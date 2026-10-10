#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "scene_nodes_stubs.h"
#include "src/fragments/rl/rl_scene_nodes.h"
#include "src/fragments/rl/rl_ui.h"

SceneSpeciesFlags D_80070FA0[151];
static unk_D_80072B00 move_data = { 15 };
static const RlUiList* shown;
static const char* message;
static RlMon mons[4];
static RlPool pool;
static RlMeta profile;
#define rl_scene_nodes_begin(run, rentals, mode) \
    (rl_scene_nodes_begin)(run, rentals, mode, &profile)

unk_D_80072B00* func_8002ED40(s32 move) {
    return move > 0 && move <= 165 ? &move_data : NULL;
}

u8 func_80022A28(s32 index) {
    assert(index >= 0 && index < 55);
    return (u8)(index + 1);
}

s32 rl_ui_list(RlUiList* list, u16 pressed) {
    return rl_ui_list_step(list, pressed);
}

void rl_ui_list_draw(const RlUiList* list, s16 x, s16 y, s16 w) {
    (void)x; (void)y; (void)w;
    assert(rl_ui_list_valid(list));
    shown = list;
}

void rl_ui_text(s16 x, s16 y, const char* text, Color_RGBA8 color) {
    (void)x; (void)y; (void)color;
    if (y == 314) message = text;
}

static void draw(void) {
    message = NULL;
    rl_scene_nodes_draw();
    assert(shown != NULL);
}

static int choose(unsigned index) {
    draw();
    assert(index < (unsigned)shown->count);
    while ((unsigned)shown->cursor > index)
        assert(rl_scene_nodes_update(RL_UI_UP) == RL_SCENE_ONGOING);
    while ((unsigned)shown->cursor < index)
        assert(rl_scene_nodes_update(RL_UI_DOWN) == RL_SCENE_ONGOING);
    return rl_scene_nodes_update(RL_UI_A);
}

static void init_run(RunState* run, unsigned seed) {
    rl_run_new(run, seed, 0);
    assert(rl_party_recruit(run, &mons[0]) == RL_OK);
    assert(rl_party_recruit(run, &mons[1]) == RL_OK);
    run->phase = RL_NODE;
    run->row = run->lane = 0;
    run->money = 100000;
    run->party[0].cur_hp /= 2;
}

static unsigned event_id(unsigned seed) {
    RlU32 x;
    x = seed ^ 0x45564944U;
    x ^= x >> 16;
    x *= 0x7FEB352DU;
    x ^= x >> 15;
    x *= 0x846CA68BU;
    x ^= x >> 16;
    x = x ? x : 1;
    return rl_random_bounded(&x, RL_EVENT_COUNT);
}

static unsigned event_seed(unsigned event) {
    unsigned seed;
    for (seed = 1; seed < 10000; seed++)
        if (event_id(seed) == event) return seed;
    assert(0);
    return 0;
}

static void test_events(void) {
    RunState run, snapshot;
    unsigned event, choice, seed;
    for (event = 0; event < RL_EVENT_COUNT; event++) {
        seed = event_seed(event);
        for (choice = 0; choice < (event == 6 ? 3U : 2U); choice++) {
            init_run(&run, seed);
            snapshot = run;
            rl_scene_nodes_begin(&run, &pool, RL_EVENT);
            draw();
            assert(!strcmp(shown->title, rl_events[event].name));
            assert(!memcmp(&run, &snapshot, sizeof(run)));
            /* Simulated suspend/reentry with a different live RNG. */
            run.rng ^= 0x12345678U;
            snapshot.rng = run.rng;
            rl_scene_nodes_begin(&run, &pool, RL_EVENT);
            draw();
            assert(!strcmp(shown->title, rl_events[event].name));
            assert(rl_scene_nodes_update(RL_UI_B) == RL_SCENE_ONGOING);
            if ((event == 2 || event == 3 || event == 4) && choice == 0) {
                assert(choose(choice) == RL_SCENE_ONGOING);
                assert(choose(0) == RL_SCENE_COMPLETED);
            } else assert(choose(choice) == RL_SCENE_COMPLETED);
            assert(run.rng == snapshot.rng);
            assert(run.phase == RL_NODE);
            assert(rl_scene_nodes_update(0) == RL_SCENE_COMPLETED);
            if (event == 0)
                assert(run.party[2].species == (choice ? 138 : 140));
            if (event == 6)
                assert(run.party[2].species >= 1);
            if (event == 10 && choice == 0)
                assert(run.tm_count == 1 && run.tm_moves[0]);
        }
    }
    init_run(&run, event_seed(0));
    while (run.party_count < RL_PARTY)
        assert(rl_party_recruit(&run, &mons[0]) == RL_OK);
    snapshot = run;
    rl_scene_nodes_begin(&run, &pool, RL_EVENT);
    assert(choose(0) == RL_SCENE_ONGOING);
    draw();
    assert(message && strstr(message, "Full"));
    assert(!memcmp(&run, &snapshot, sizeof(run)));
    assert(choose(4) == RL_SCENE_COMPLETED);
}

static void test_center_recruit(void) {
    RunState run, snapshot;
    char first[48];
    unsigned hp;
    init_run(&run, 23);
    hp = run.party[0].cur_hp;
    run.party[1].cur_hp = 0;
    run.party[1].fainted = 1;
    rl_scene_nodes_begin(&run, &pool, RL_CENTER);
    assert(choose(0) == RL_SCENE_COMPLETED);
    assert(run.party[0].cur_hp > hp && !run.party[1].cur_hp);
    rl_scene_nodes_begin(&run, &pool, RL_CENTER);
    assert(choose(1) == RL_SCENE_ONGOING);
    assert(choose(0) == RL_SCENE_ONGOING);
    draw();
    assert(message);
    assert(choose(1) == RL_SCENE_COMPLETED);
    assert(run.party[1].cur_hp && !run.party[1].fainted);
    init_run(&run, 23);
    snapshot = run;
    rl_scene_nodes_begin(&run, &pool, RL_RECRUIT);
    draw();
    strcpy(first, shown->items[0].text);
    assert(!memcmp(&run, &snapshot, sizeof(run)));
    run.rng++;
    rl_scene_nodes_begin(&run, &pool, RL_RECRUIT);
    draw();
    assert(!strcmp(first, shown->items[0].text));
    assert(choose(0) == RL_SCENE_COMPLETED && run.party_count == 3);
    while (run.party_count < RL_PARTY)
        assert(rl_party_recruit(&run, &mons[0]) == RL_OK);
    rl_scene_nodes_begin(&run, &pool, RL_RECRUIT);
    assert(choose(0) == RL_SCENE_ONGOING);
    draw();
    assert(message && strstr(message, "Full"));
}

static void test_bag_party(void) {
    RunState run;
    unsigned hp, level, species;
    init_run(&run, 1);
    run.bag[RL_POTION] = 1;
    run.bag[RL_CANDY] = 1;
    run.bag[RL_VITAMIN] = 1;
    hp = run.party[0].cur_hp;
    rl_scene_nodes_begin(&run, &pool, RL_SCENE_BAG);
    assert(choose(0) == RL_SCENE_ONGOING);
    assert(choose(0) == RL_SCENE_ONGOING);
    assert(!run.bag[RL_POTION] && run.party[0].cur_hp > hp);
    draw();
    assert(shown->cursor == 0 && !strcmp(shown->title, "Bag: choose an item"));
    level = run.party[0].level;
    assert(choose(0) == RL_SCENE_ONGOING);
    assert(choose(0) == RL_SCENE_ONGOING);
    assert(!run.bag[RL_CANDY] && run.party[0].level == level + 1);
    assert(choose(0) == RL_SCENE_ONGOING);
    assert(choose(0) == RL_SCENE_ONGOING);
    assert(!run.bag[RL_VITAMIN] && run.party[0].stat_exp[0] == 2560);
    assert(rl_scene_nodes_update(RL_UI_B) == RL_SCENE_CANCEL);
    run.tm_moves[0] = 1;
    run.tm_count = 1;
    rl_scene_nodes_begin(&run, &pool, RL_SCENE_BAG);
    assert(choose(0) == RL_SCENE_ONGOING);
    assert(choose(1) == RL_SCENE_ONGOING);
    draw();
    assert(message && strstr(message, "cannot learn"));
    assert(!strcmp(shown->title, "Use on which Pokemon?"));
    assert(run.tm_count == 1);
    assert(choose(0) == RL_SCENE_ONGOING);
    draw();
    assert(!strcmp(shown->title, "Replace which move?"));
    assert(choose(0) == RL_SCENE_ONGOING);
    assert(!run.tm_count && !run.tm_moves[0]);
    assert(run.party[0].moves[0] == 1 && run.party[0].pp[0] == 15);
    draw();
    assert(shown->cursor == 0);
    rl_scene_nodes_begin(&run, &pool, RL_SCENE_PARTY);
    species = run.party[0].species;
    assert(choose(0) == RL_SCENE_ONGOING);
    assert(choose(0) == RL_SCENE_ONGOING);
    assert(choose(1) == RL_SCENE_ONGOING);
    assert(run.party[1].species == species);
    draw();
    assert(shown->cursor == 0);
    assert(choose(0) == RL_SCENE_ONGOING);
    assert(choose(1) == RL_SCENE_ONGOING);
    assert(choose(1) == RL_SCENE_ONGOING);
    assert(run.party_count == 1 && run.party[0].species == species);
    assert(choose(0) == RL_SCENE_ONGOING);
    assert(choose(1) == RL_SCENE_ONGOING);
    assert(choose(1) == RL_SCENE_ONGOING);
    assert(run.party_count == 1);
    draw();
    assert(message && strstr(message, "last living"));
    assert(rl_scene_nodes_update(RL_UI_B) == RL_SCENE_ONGOING);
    assert(rl_scene_nodes_update(RL_UI_B) == RL_SCENE_CANCEL);
}

static void test_shop(void) {
    RunState run, snapshot;
    char labels[12][48];
    unsigned i, old_money, tm;
    init_run(&run, 7);
    snapshot = run;
    rl_scene_nodes_begin(&run, &pool, RL_MART);
    draw();
    assert(shown->count == 15);
    for (i = 0; i < 12; i++) strcpy(labels[i], shown->items[i].text);
    assert(!memcmp(&run, &snapshot, sizeof(run)));
    tm = 12;
    for (i = 0; i < 12; i++) {
        if (!strncmp(labels[i], "TM move", 7)) tm = i;
        old_money = run.money;
        assert(choose(i) == RL_SCENE_ONGOING);
        assert(run.money < old_money);
    }
    assert(tm < 12 && run.tm_count == 1);
    run.rng++;
    rl_scene_nodes_begin(&run, &pool, RL_MART);
    draw();
    for (i = 0; i < 12; i++) assert(!strcmp(labels[i], shown->items[i].text));
    assert(choose(12) == RL_SCENE_ONGOING);
    draw();
    assert(!strcmp(shown->title, "Bag: choose an item"));
    assert(choose(0) == RL_SCENE_ONGOING);
    assert(choose(0) == RL_SCENE_ONGOING);
    draw();
    assert(!strcmp(shown->title, "Poke Mart") && shown->cursor == 0);
    assert(choose(13) == RL_SCENE_ONGOING);
    assert(choose(0) == RL_SCENE_ONGOING);
    assert(choose(0) == RL_SCENE_ONGOING);
    assert(choose(1) == RL_SCENE_ONGOING);
    draw();
    assert(!strcmp(shown->title, "Poke Mart") && shown->cursor == 0);
    assert(rl_scene_nodes_update(RL_UI_B) == RL_SCENE_ONGOING);
    assert(choose(14) == RL_SCENE_COMPLETED);
}

static void test_locked_profile(void) {
    RunState run;
    unsigned seed, event, i;
    int found;
    rl_meta_init(&profile);
    for (seed = 1; seed <= 500; seed++) {
        init_run(&run, seed);
        rl_scene_nodes_begin(&run, &pool, RL_EVENT);
        draw();
        found = 0;
        for (event = 0; event < RL_META_INITIAL_EVENTS; event++) {
            if (!strcmp(shown->title, rl_events[event].name)) found = 1;
        }
        assert(found);
        rl_scene_nodes_begin(&run, &pool, RL_MART);
        draw();
        for (i = RL_META_INITIAL_RELICS; i < RL_RELIC_COUNT; i++) {
            for (event = 0; event < 12; event++)
                assert(strcmp(shown->items[event].text, rl_relic_defs[i].name));
        }
    }
    profile.unlocked_events = 1U << 8;
    init_run(&run, 99);
    rl_scene_nodes_begin(&run, &pool, RL_EVENT);
    draw();
    assert(!strcmp(shown->title, rl_events[8].name));
    assert(choose(0) == RL_SCENE_COMPLETED);
    assert(run.relics && !(run.relics & ~255U));
    profile.unlocked_events = 1U << 5;
    init_run(&run, 50);
    rl_scene_nodes_begin(&run, &pool, RL_EVENT);
    assert(choose(0) == RL_SCENE_ONGOING);
    assert(!run.relics);
}

int main(void) {
    unsigned i, stat;
    rl_meta_init(&profile);
    profile.unlocked_events = (1U << RL_EVENT_COUNT) - 1;
    profile.unlocked_relics = (1U << RL_RELIC_COUNT) - 1;
    memset(mons, 0, sizeof(mons));
    for (i = 0; i < 4; i++) {
        mons[i].species = (RlU8)(i == 2 ? 140 : i == 3 ? 138 : i + 1);
        mons[i].level = 30;
        mons[i].held_relic = RL_NO_SLOT;
        mons[i].moves[0] = 33;
        mons[i].pp[0] = mons[i].max_pp[0] = 35;
        for (stat = 0; stat < 5; stat++) mons[i].base_stats[stat] = 60;
        mons[i].exp = rl_xp_for_level(30, RL_MEDIUM_FAST);
        rl_mon_recalculate_stats(&mons[i], 0);
        mons[i].cur_hp = mons[i].max_hp;
    }
    D_80070FA0[0].bytes[15] = 1;
    pool.mons = mons;
    pool.count = 4;
    pool.fixed_order = 0;
    test_events();
    test_center_recruit();
    test_bag_party();
    test_shop();
    test_locked_profile();
    puts("Scene interactions: all twelve events, Center, recruit, shop, Bag, TM and Party passed");
    return 0;
}
