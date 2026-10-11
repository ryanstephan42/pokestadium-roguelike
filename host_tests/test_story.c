#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/story/sc_core.h"
#include "../src/story/sc_save.h"
#include "../src/story/sc_bridge.h"

static ScState state, before;
static ScEncounter fixtures[4];
static ScCatalog catalog;

static unsigned find(const char *id) {
    unsigned i;
    for (i = 0; i < sc_catalog.count; i++)
        if (strcmp(sc_catalog.encounters[i].id, id) == 0) return i;
    assert(!"Missing canonical encounter");
    return 0;
}

static void setup(void) {
    unsigned i;
    ScOptions options = {0, SC_STANDARD, SC_STARTER_TRIO, 1};
    catalog = sc_catalog;
    memset(fixtures, 0, sizeof(fixtures));
    fixtures[0] = sc_encounters[find("OaksLab/rival-lab")];
    fixtures[1] = sc_encounters[find("PewterGym/TEXT_PEWTERGYM_BROCK")];
    fixtures[2] = sc_encounters[find("CeladonMansionRoofHouse/gift-eevee")];
    fixtures[3] = fixtures[1];
    fixtures[3].id = "fixture/optional";
    fixtures[0].grants = 1;
    fixtures[1].gate.required = 1;
    fixtures[1].gate.prior[0] = 0;
    fixtures[1].gate.prior_count = 1;
    fixtures[1].grants = 2;
    fixtures[2].gate.any = 2;
    fixtures[2].grants = 4;
    fixtures[3].retires_at = 2;
    catalog.encounters = fixtures;
    catalog.count = 4;
    catalog.graph = NULL;
    catalog.graph_reviewed = 1; /* Reviewed synthetic graph, NOT the pending canon graph. */
    assert(sc_catalog_valid(&catalog));
    assert(sc_new(&state, &catalog, &options, 123) == SC_OK);
    for (i = 0; i < 6; i++) assert(state.learn_moves[i] == 0);
}

static void settle_learning(void) {
    unsigned i;
    for (i = 0; i < state.party_count; i++)
        if (state.learn_mask & (1U << i)) assert(sc_learn(&state, &catalog, i, 3, 0) == SC_OK);
}

static void win(unsigned encounter) {
    unsigned i, variant;
    variant = fixtures[encounter].kind == SC_RIVAL ? state.rival_branch : 0;
    assert(sc_begin(&state, &catalog, encounter, variant) == SC_OK);
    if (state.phase == SC_REWARD) return;
    for (i = 0; i < fixtures[encounter].teams[variant].count; i++) {
        assert(sc_on_ko(&state, &catalog, i, 1, variant) == SC_OK);
        settle_learning();
    }
    assert(sc_finish(&state, &catalog, SC_WIN, variant) == SC_OK);
}

static void test_starters(void) {
    ScOptions options;
    unsigned seed, pool;
    ScCatalog unreviewed = sc_catalog;
    unreviewed.graph_reviewed = 0;
    memset(&before, 0xa5, sizeof(before));
    state = before;
    memset(&options, 0, sizeof(options));
    assert(sc_new(&state, &unreviewed, &options, 1) == SC_UNREVIEWED);
    assert(memcmp(&state, &before, sizeof(state)) == 0);
    setup();
    for (pool = 0; pool < 3; pool++) {
        for (seed = 0; seed < 1000; seed++) {
            options.version = seed % 2;
            options.difficulty = seed % 3;
            options.starter_pool = (RlU8)pool;
            options.starter = 0;
            assert(sc_new(&state, &catalog, &options, seed) == SC_OK);
            assert(state.party_count == 1 && state.party[0].level == 5);
            assert(state.rival_branch < 3 && state.options.difficulty == seed % 3);
            assert(state.party[0].held_relic == RL_NO_SLOT);
            if (pool == SC_STARTER_TRIO) {
                unsigned species = state.party[0].species;
                assert(species == 1 || species == 4 || species == 7);
                assert(state.rival_branch == (species == 1 ? 2 : species == 4 ? 0 : 1));
            }
            if (pool == SC_UNEVOLVED) {
                unsigned j, k, species = state.party[0].species;
                assert(species != 144 && species != 145 && species != 146 &&
                       species != 150 && species != 151);
                for (j = 1; j <= 151; j++)
                    for (k = 0; k < sc_species[j].evolution_count; k++)
                        assert(sc_species[j].evolution[k].species != species);
            }
            before = state;
            assert(sc_new(&state, &catalog, &options, seed) == SC_OK);
            assert(memcmp(&state, &before, sizeof(state)) == 0);
        }
    }
}

static void test_stat_source(void) {
    static const RlU16 dvs[] = {0, 1, 0x10, 0x100, 0x1000, 0x8889, 0x1248, 0xffff};
    RlMon mon;
    unsigned d, effort, root, stat, expected, dv;
    memset(&mon, 0, sizeof(mon));
    mon.level = 100;
    for (stat = 0; stat < 5; stat++) mon.base_stats[stat] = 100;
    for (d = 0; d < sizeof(dvs) / sizeof(dvs[0]); d++) {
        mon.dvs = dvs[d];
        root = 0;
        for (effort = 0; effort <= 65535; effort++) {
            while (root < 255 && root * root < effort) root++;
            for (stat = 0; stat < 5; stat++) {
                mon.stat_exp[stat] = (RlU16)effort;
                if (stat == RL_HP)
                    dv = ((dvs[d] & 1) ? 8 : 0) + ((dvs[d] & 0x10) ? 4 : 0) +
                         ((dvs[d] & 0x100) ? 2 : 0) + ((dvs[d] & 0x1000) ? 1 : 0);
                else dv = (dvs[d] >> ((stat - 1) * 4)) & 15;
                expected = 2 * (100 + dv) + root / 4 + (stat == RL_HP ? 110 : 5);
                assert(sc_stat(&mon, stat) == expected);
            }
        }
    }
    setup();
    {
        RlMon team[6];
        unsigned count;
        assert(sc_team(&state, &catalog, 1, 0, team, &count) == SC_OK);
        assert(count == 2);
        assert(team[0].species == 74 && team[0].max_hp == 33);
        assert(team[1].species == 95 && team[1].max_hp == 36);
    }
}

static void test_all_species_levels(void) {
    ScTemplate input;
    RlMon mon;
    unsigned s, level, i, j, move, empty;
    RlU8 reference[4];
    memset(&input, 0, sizeof(input));
    input.fixed_dvs = 1;
    input.dvs = 0x8889;
    for (s = 1; s <= 151; s++) {
        input.species = (RlU8)s;
        for (level = 1; level <= 100; level++) {
            input.level = (RlU8)level;
            assert(sc_make_mon(&sc_catalog, &input, 0, &mon) == SC_OK);
            assert(mon.exp == sc_xp(level, sc_species[s].growth));
            assert(mon.cur_hp == mon.max_hp && !mon.fainted && !mon.status);
            memcpy(reference, sc_species[s].initial, sizeof(reference));
            for (i = 0; i < sc_species[s].learn_count; i++) {
                if (sc_species[s].learn[i].level > level) break;
                move = sc_species[s].learn[i].move;
                if (reference[0] == move || reference[1] == move ||
                    reference[2] == move || reference[3] == move) continue;
                empty = 4;
                for (j = 0; j < 4; j++) if (!reference[j]) { empty = j; break; }
                if (empty == 4) {
                    memmove(reference, reference + 1, 3);
                    empty = 3;
                }
                reference[empty] = (RlU8)move;
            }
            assert(memcmp(reference, mon.moves, 4) == 0);
            for (i = 0; i < 4; i++) assert(mon.pp[i] == sc_pp[mon.moves[i]]);
        }
    }
    assert(sc_xp(100, RL_MEDIUM_FAST) == 1000000);
    assert(sc_xp(100, RL_MEDIUM_SLOW) == 1059860);
    assert(sc_xp(100, RL_FAST) == 800000);
    assert(sc_xp(100, RL_SLOW) == 1250000);
}

static void test_graph_reward(void) {
    RlU32 rng;
    unsigned i;
    setup();
    assert(sc_available(&state, &catalog, 0));
    assert(!sc_available(&state, &catalog, 1));
    before = state;
    assert(sc_begin(&state, &catalog, 1, 0) == SC_UNAVAILABLE);
    assert(memcmp(&before, &state, sizeof(state)) == 0);
    assert(sc_begin(&state, &catalog, 0, 0) == SC_INVALID); /* Wrong rival branch. */
    win(0);
    rng = state.rng;
    before = state;
    assert(sc_recruit(&state, &catalog, 6, 0) == SC_PENDING);
    assert(memcmp(&before, &state, sizeof(state)) == 0);
    assert(sc_recruit(&state, &catalog, 6, 1) == SC_OK);
    assert(state.rng == rng && state.party_count == 2);
    assert(!sc_available(&state, &catalog, 0));
    assert(sc_available(&state, &catalog, 1));
    win(1);
    assert(state.phase == SC_REWARD);
    assert(state.offer.moves[0] && state.offer.cur_hp == state.offer.max_hp);
    assert(sc_decline(&state, &catalog) == SC_OK);
    assert(!sc_available(&state, &catalog, 3)); /* Retired by leader milestone. */
    assert(sc_available(&state, &catalog, 2));
    win(2);
    for (i = state.party_count; i < 6; i++) state.party[i] = state.party[0];
    state.party_count = 6;
    before = state;
    assert(sc_recruit(&state, &catalog, 6, 1) == SC_FULL);
    assert(sc_recruit(&state, &catalog, 3, 0) == SC_PENDING);
    assert(memcmp(&before, &state, sizeof(state)) == 0);
    assert(sc_recruit(&state, &catalog, 3, 1) == SC_OK);
    assert(state.party_count == 6 && state.party[3].species == 133);
    assert((state.milestones & 4) && !sc_available(&state, &catalog, 2));
}

static void test_transfer(void) {
    unsigned i;
    setup();
    win(0);
    state.offer.moves[0] = 126; /* Fire Blast is not in Bulbasaur's learnset/TM list. */
    state.party[0].pp[0] = 2;
    state.party[0].cur_hp--;
    state.party[0].status = 8;
    before = state;
    assert(sc_transfer(&state, &catalog, 0, 0, 2, 0) == SC_PENDING);
    assert(memcmp(&before, &state, sizeof(state)) == 0);
    assert(sc_transfer(&state, &catalog, 0, 0, 2, 1) == SC_OK);
    assert(state.party[0].moves[2] == 126 && state.party[0].pp[2] == sc_pp[126]);
    assert(state.party[0].cur_hp == before.party[0].cur_hp && state.party[0].status == 8);
    assert(state.party[0].pp[0] == 2);
    for (i = 0; i < 5; i++) assert(state.party[0].stat_exp[i] == before.party[0].stat_exp[i]);
    assert(sc_transfer(&state, &catalog, 0, 0, 1, 1) == SC_INVALID);
}

static void test_ko_and_learning(void) {
    RlU32 old_exp, award;
    unsigned i, count;
    int old_hp;
    ScTemplate input = {1, 6, {0, 0, 0, 0}, 1, 0xffff};
    setup();
    assert(sc_make_mon(&catalog, &input, 0, &state.party[0]) == SC_OK);
    state.party[0].exp = sc_xp(7, RL_MEDIUM_SLOW) - 1;
    state.party[0].cur_hp--;
    old_hp = state.party[0].max_hp - state.party[0].cur_hp;
    state.party[1] = state.party[0];
    state.party[1].cur_hp = 0;
    state.party[1].fainted = 1;
    state.party_count = 2;
    assert(sc_begin(&state, &catalog, 0, 2) == SC_OK);
    old_exp = state.party[0].exp;
    count = 2; /* Denominator includes flagged fainted participants, as in Red/Blue. */
    award = (RlU32)(sc_species[4].yield / count) * 5 / 7;
    award += award / 2;
    assert(sc_on_ko(&state, &catalog, 0, 3, 2) == SC_OK);
    assert(state.party[0].exp == old_exp + award);
    assert(state.party[1].exp == old_exp);
    assert(state.phase == SC_LEARNING && state.learn_moves[0] == 73);
    assert(state.party[0].max_hp - state.party[0].cur_hp == old_hp);
    for (i = 0; i < 5; i++) {
        assert(state.party[0].stat_exp[i] == sc_species[4].base[i] / count);
        assert(state.party[1].stat_exp[i] == 0);
    }
    assert(sc_finish(&state, &catalog, SC_WIN, 2) == SC_INVALID);
    assert(sc_learn(&state, &catalog, 0, 3, 1) == SC_OK);
    assert(state.party[0].moves[3] == 73 && state.phase == SC_BATTLE);
    before = state;
    assert(sc_on_ko(&state, &catalog, 0, 3, 2) == SC_DUPLICATE);
    assert(memcmp(&before, &state, sizeof(state)) == 0);
    assert(sc_finish(&state, &catalog, SC_WIN, 2) == SC_OK);
    /* Large award visits only the final learn level and caps by species growth. */
    state.party[0].stat_exp[0] = 65535;
    {
        RlU8 learned;
        assert(sc_add_xp(&state.party[0], 0xffffffffU, &learned, &catalog) == SC_OK);
        assert(state.party[0].level == 100 && state.party[0].exp == 1059860 && learned == 0);
    }
}

static void test_healing_and_loss(void) {
    ScGate gate;
    unsigned difficulty;
    memset(&gate, 0, sizeof(gate));
    for (difficulty = 0; difficulty < 3; difficulty++) {
        setup();
        state.options.difficulty = (RlU8)difficulty;
        state.party[0].cur_hp = 0;
        state.party[0].fainted = 1;
        state.party[0].pp[0] = 0;
        state.party[0].status = 8;
        before = state;
        if (difficulty == SC_BRUTAL) {
            assert(sc_heal(&state, &catalog, 0, &gate) == SC_UNAVAILABLE);
            assert(memcmp(&before, &state, sizeof(state)) == 0);
        } else {
            assert(sc_heal(&state, &catalog, 0, &gate) == SC_OK);
            assert(state.party[0].cur_hp == state.party[0].max_hp);
            assert(state.party[0].pp[0] == state.party[0].max_pp[0]);
            assert(!state.party[0].fainted && !state.party[0].status);
            assert(sc_heal(&state, &catalog, 0, &gate) ==
                   (difficulty == SC_LIMITED ? SC_UNAVAILABLE : SC_OK));
        }
    }
    setup();
    assert(sc_begin(&state, &catalog, 0, 2) == SC_OK);
    assert(sc_finish(&state, &catalog, SC_DRAW, 2) == SC_OK);
    assert(state.phase == SC_LOST && !sc_available(&state, &catalog, 0));
    assert(sc_heal(&state, &catalog, 0, &gate) == SC_INVALID);
}

static void test_evolution(void) {
    ScTemplate input = {1, 16, {33, 45, 0, 0}, 1, 0x1248};
    int hp;
    setup();
    assert(sc_make_mon(&catalog, &input, 0, &state.party[0]) == SC_OK);
    assert(sc_evolve(&state, &catalog, 0, 0, 0, 0, 1) == SC_UNAVAILABLE);
    state.evolution_mask = 1;
    state.party[0].cur_hp -= 3;
    hp = state.party[0].max_hp - state.party[0].cur_hp;
    state.party[0].pp[0] = 2;
    assert(sc_evolve(&state, &catalog, 0, 0, 0, 0, 1) == SC_OK);
    assert(state.party[0].species == 2 && state.party[0].dvs == 0x1248);
    assert(state.party[0].max_hp - state.party[0].cur_hp == hp);
    assert(state.party[0].pp[0] == 2 && !state.evolution_mask);
    input.species = 133;
    input.level = 25;
    assert(sc_make_mon(&catalog, &input, 0, &state.party[0]) == SC_OK);
    assert(sc_evolve(&state, &catalog, 0, 1, 0, 0, 1) == SC_UNAVAILABLE);
    assert(sc_evolve(&state, &catalog, 0, 1, 1U << SC_THUNDER_STONE, 0, 1) == SC_OK);
    assert(state.party[0].species == 135);
    input.species = 64;
    assert(sc_make_mon(&catalog, &input, 0, &state.party[0]) == SC_OK);
    state.party[0].cur_hp = 0;
    assert(sc_evolve(&state, &catalog, 0, 0, 0, 0, 1) == SC_UNAVAILABLE);
    assert(sc_evolve(&state, &catalog, 0, 0, 0, 1, 1) == SC_OK);
    assert(state.party[0].species == 65 && state.party[0].cur_hp == 0);
}

static void test_failed_inputs_do_not_mutate(void) {
    ScCatalog broken;
    unsigned bad;
    setup();
    before = state;
    broken = catalog;
    broken.count = SC_ENCOUNTER_CAPACITY + 1;
    assert(!sc_catalog_valid(&broken));
    assert(!sc_available(&state, &broken, 0));
    for (bad = 0; bad < 256; bad++) {
        ScOptions options = state.options;
        if (bad > 2) {
            options.difficulty = (RlU8)bad;
            assert(sc_new(&state, &catalog, &options, 0) == SC_INVALID);
        }
    }
    assert(memcmp(&state, &before, sizeof(state)) == 0);
    state.party_count = 255;
    assert(sc_begin(&state, &catalog, 0, 2) == SC_INVALID);
    setup();
    assert(sc_begin(&state, &catalog, 0, 2) == SC_OK);
    before = state;
    assert(sc_on_ko(&state, &catalog, 0, 0x80, 2) == SC_INVALID);
    assert(sc_on_ko(&state, &catalog, 6, 1, 2) == SC_INVALID);
    assert(sc_on_ko(&state, &catalog, 0, 1, 0) == SC_INVALID);
    assert(sc_finish(&state, &catalog, SC_WIN, 2) == SC_PENDING);
    assert(memcmp(&state, &before, sizeof(state)) == 0);
    assert(sc_on_ko(&state, &catalog, 0, 1, 2) == SC_OK);
    settle_learning();
    assert(sc_finish(&state, &catalog, SC_WIN, 2) == SC_OK);
    before = state;
    assert(sc_transfer(&state, &catalog, 4, 0, 0, 1) == SC_INVALID);
    assert(sc_transfer(&state, &catalog, 0, 6, 0, 1) == SC_INVALID);
    assert(sc_transfer(&state, &catalog, 0, 0, 4, 1) == SC_INVALID);
    assert(sc_recruit(&state, &catalog, 0, 2) == SC_INVALID);
    assert(memcmp(&state, &before, sizeof(state)) == 0);
}

static void canon_reach(unsigned node, unsigned fossil) {
    const ScNode *entry = &sc_catalog.graph->nodes[node];
    unsigned i, variant;
    if (state.completed[node / 8] & (1U << (node % 8))) return;
    for (i = 0; i < entry->clauses[0].count; i++)
        canon_reach(entry->clauses[0].prior[i], fossil);
    if (node >= sc_catalog.count) {
        assert(sc_action(&state, &sc_catalog, node,
                         node == sc_catalog.graph->fossil_action ? fossil : 0,
                         entry->destination_warp) == SC_OK);
        return;
    }
    variant = sc_encounters[node].kind == SC_RIVAL ? state.rival_branch : 0;
    if (node == sc_catalog.graph->fossil_offer) variant = state.fossil_choice;
    assert(sc_begin(&state, &sc_catalog, node, variant) == SC_OK);
    if (state.phase != SC_REWARD) {
        for (i = 0; i < sc_encounters[node].teams[variant].count; i++) {
            assert(sc_on_ko(&state, &sc_catalog, i, 1, variant) == SC_OK);
            while (state.learn_mask) assert(sc_learn(&state, &sc_catalog, 0, 0, 0) == SC_OK);
        }
        assert(sc_finish(&state, &sc_catalog, SC_WIN, variant) == SC_OK);
    }
    if (state.phase == SC_REWARD) assert(sc_decline(&state, &sc_catalog) == SC_OK);
}

static void test_canonical_activation(void) {
    ScOptions options = {0, SC_STANDARD, SC_STARTER_TRIO, 1};
    ScGraph bad_graph;
    ScCatalog bad;
    unsigned version, starter, fossil, i, departure = 0, vermilion = 0;
    assert(sc_catalog.graph_reviewed && sc_catalog.graph->count == 383);
    for (i = sc_catalog.count; i < sc_catalog.graph->count; i++)
        if (strcmp(sc_catalog.graph->nodes[i].id, "travel:ship-departure") == 0)
            departure = i;
    for (i = 0; i < sc_catalog.graph->healing_count; i++)
        if (strcmp(sc_catalog.graph->healing[i].id, "heal:VermilionPokecenter") == 0)
            vermilion = i;
    for (version = 0; version < 2; version++)
        for (starter = 0; starter < 3; starter++)
            for (fossil = 0; fossil < 2; fossil++) {
                options.version = (RlU8)version;
                options.starter = (RlU8)(starter == 0 ? 1 : starter == 1 ? 4 : 7);
                assert(sc_new(&state, &sc_catalog, &options, 42) == SC_OK);
                assert(sc_available(&state, &sc_catalog, find("OaksLab/rival-lab")));
                assert(!sc_available(&state, &sc_catalog, find("PewterGym/TEXT_PEWTERGYM_BROCK")));
                canon_reach(sc_catalog.graph->champion, fossil);
                assert(state.fossil_choice == fossil);
                assert(sc_location_heal(&state, &sc_catalog, vermilion) == SC_OK);
                assert(sc_location_heal(&state, &sc_catalog, vermilion) == SC_OK);
                assert(sc_available(&state, &sc_catalog,
                                    find("SSAnne1FRooms/TEXT_SSANNE1FROOMS_YOUNGSTER")));
                before = state;
                assert(sc_action(&state, &sc_catalog, departure, 0, 2) == SC_UNAVAILABLE);
                assert(memcmp(&state, &before, sizeof(state)) == 0);
                assert(sc_action(&state, &sc_catalog, departure, 0, 1) == SC_OK);
                assert(!sc_available(&state, &sc_catalog,
                                     find("SSAnne1FRooms/TEXT_SSANNE1FROOMS_YOUNGSTER")));
                assert(sc_action(&state, &sc_catalog, departure, 0, 1) == SC_UNAVAILABLE);
                assert(sc_grant(&state, &sc_catalog, 0xffffffffU) == SC_INVALID);
                assert(sc_state_valid(&state, &sc_catalog));
            }
    bad = sc_catalog;
    bad_graph = *sc_catalog.graph;
    bad.graph = &bad_graph;
    bad_graph.fossil_action = 999;
    assert(!sc_catalog_valid(&bad));
}

static void test_full_installed_graph(void) {
    ScOptions options = {0, SC_STANDARD, SC_STARTER_TRIO, 1};
    unsigned version, starter, fossil, count, i, j, k, chosen, destructive;
    for (version = 0; version < 2; version++)
        for (starter = 0; starter < 3; starter++)
            for (fossil = 0; fossil < 2; fossil++) {
                options.version = (RlU8)version;
                options.starter = (RlU8)(starter == 0 ? 1 : starter == 1 ? 4 : 7);
                assert(sc_new(&state, &sc_catalog, &options, 42) == SC_OK);
                count = 0;
                while (count < sc_catalog.graph->count) {
                    chosen = SC_NO_ENCOUNTER;
                    for (i = 0; i < sc_catalog.graph->count; i++) {
                        ScState probe;
                        int ready;
                        if (i < sc_catalog.count) ready = sc_available(&state, &sc_catalog, i);
                        else {
                            probe = state;
                            ready = sc_action(&probe, &sc_catalog, i,
                                              i == sc_catalog.graph->fossil_action ? fossil : 0,
                                              sc_catalog.graph->nodes[i].destination_warp) == SC_OK;
                        }
                        if (!ready) continue;
                        if (chosen == SC_NO_ENCOUNTER) chosen = i;
                        destructive = 0;
                        for (j = 0; j < sc_catalog.graph->count; j++)
                            for (k = 0; k < sc_catalog.graph->nodes[j].retirement_count; k++)
                                if (sc_catalog.graph->nodes[j].retires_after[k] == i) destructive = 1;
                        if (!destructive) { chosen = i; break; }
                    }
                    assert(chosen != SC_NO_ENCOUNTER);
                    canon_reach(chosen, fossil);
                    count = 0;
                    for (i = 0; i < sc_catalog.graph->count; i++)
                        if (state.completed[i / 8] & (1U << (i % 8))) count++;
                }
                for (i = 0; i < sc_catalog.graph->count; i++)
                    assert(state.completed[i / 8] & (1U << (i % 8)));
                for (i = 0; i < SC_BITS; i++) assert(!state.retired[i]);
                assert(sc_state_valid(&state, &sc_catalog));
            }
}

static RlU8 note[SC_SAVE_NOTE_SIZE], untouched_note[256];
static int note_exists, fail_write, writes;

static int note_open(void *context, unsigned company, RlU32 game,
                     const RlU8 name[16], const RlU8 extension[4], unsigned size, int create) {
    (void)context;
    assert(company == SC_SAVE_COMPANY && game == SC_SAVE_GAME);
    assert(memcmp(name, sc_save_name, 16) == 0);
    assert(memcmp(extension, sc_save_extension, 4) == 0);
    assert(size == sizeof(note));
    if (!note_exists && !create) return SC_SAVE_NOT_FOUND;
    if (!note_exists) { memset(note, 0, sizeof(note)); note_exists = 1; }
    return SC_SAVE_OK;
}

static int note_read(void *context, unsigned offset, RlU8 *data, unsigned size) {
    (void)context;
    assert(offset % 32 == 0 && size % 32 == 0 && offset + size <= sizeof(note));
    memcpy(data, note + offset, size);
    return SC_SAVE_OK;
}

static int note_write(void *context, unsigned offset, const RlU8 *data, unsigned size) {
    (void)context;
    assert(offset % 32 == 0 && size % 32 == 0 && offset + size <= sizeof(note));
    writes++;
    if (fail_write == writes) {
        memcpy(note + offset, data, size / 2);
        return SC_SAVE_IO;
    }
    memcpy(note + offset, data, size);
    return SC_SAVE_OK;
}

static void fix_crc(RlU8 *data) {
    RlU32 crc = sc_save_crc(data, SC_SAVE_SIZE);
    unsigned i;
    for (i = 0; i < 4; i++) data[8 + i] = (RlU8)(crc >> (i * 8));
}

static void test_story_save(void) {
    ScSaveDevice device = {0, note_open, note_read, note_write};
    ScOptions options = {0, SC_STANDARD, SC_STARTER_TRIO, 1};
    ScSnapshot snapshot, loaded, prior;
    RlU8 bytes[SC_SAVE_SIZE], saved[SC_SAVE_SIZE], backup[SC_SAVE_NOTE_SIZE];
    RlU32 generation = 7;
    unsigned i, failure;
    assert(sc_new(&snapshot.state, &sc_catalog, &options, 123) == SC_OK);
    snapshot.interrupted = 1;
    assert(sc_save_encode(&snapshot, &sc_catalog, 0x12345678U, bytes) == SC_SAVE_OK);
    assert(bytes[0] == 'S' && bytes[1] == 'C' && bytes[2] == '0' && bytes[3] == '1');
    assert(bytes[12] == 0x78 && bytes[15] == 0x12);
    memset(&loaded, 0, sizeof(loaded));
    assert(sc_save_decode(&loaded, &sc_catalog, &generation, bytes, sizeof(bytes)) == SC_SAVE_OK);
    assert(generation == 0x12345678U && loaded.interrupted);
    assert(memcmp(&loaded.state, &snapshot.state, sizeof(ScState)) == 0);
    memcpy(saved, bytes, sizeof(bytes));
    prior = loaded;
    snapshot.state.party[5].species = 150;
    snapshot.state.offer.species = 151;
    assert(sc_save_encode(&snapshot, &sc_catalog, 0, bytes) == SC_SAVE_INVALID);
    snapshot = prior;
    snapshot.state.offered_slot = 0;
    assert(sc_save_encode(&snapshot, &sc_catalog, 0, bytes) == SC_SAVE_INVALID);
    snapshot = prior;
    snapshot.state.return_phase = SC_BATTLE;
    assert(sc_save_encode(&snapshot, &sc_catalog, 0, bytes) == SC_SAVE_INVALID);
    snapshot = prior;
    snapshot.state.learn_moves[5] = 1;
    assert(sc_save_encode(&snapshot, &sc_catalog, 0, bytes) == SC_SAVE_INVALID);
    snapshot = prior;
    memcpy(bytes, saved, sizeof(bytes));
    bytes[96 + 166 + 5 * 64] = 150;
    bytes[96 + 166 + 6 * 64] = 151;
    fix_crc(bytes);
    assert(sc_save_decode(&loaded, &sc_catalog, &generation, bytes, sizeof(bytes)) == SC_SAVE_BAD_DATA);
    assert(memcmp(&loaded, &prior, sizeof(loaded)) == 0);
    memcpy(bytes, saved, sizeof(bytes));
    for (i = 0; i < sizeof(bytes); i++) {
        bytes[i] ^= 1;
        assert(sc_save_decode(&loaded, &sc_catalog, &generation, bytes, sizeof(bytes)) != SC_SAVE_OK);
        assert(memcmp(&loaded, &prior, sizeof(loaded)) == 0);
        bytes[i] ^= 1;
    }
    bytes[32] ^= 1; fix_crc(bytes);
    assert(sc_save_decode(&loaded, &sc_catalog, &generation, bytes, sizeof(bytes)) == SC_SAVE_BAD_VERSION);
    memcpy(bytes, saved, sizeof(bytes));
    bytes[96 + 21] = 7; fix_crc(bytes); /* Party count is explicitly serialized. */
    assert(sc_save_decode(&loaded, &sc_catalog, &generation, bytes, sizeof(bytes)) == SC_SAVE_BAD_DATA);
    assert(sc_save_decode(&loaded, &sc_catalog, &generation, bytes, 10) == SC_SAVE_INVALID);
    assert(memcmp(&loaded, &prior, sizeof(loaded)) == 0);
    memset(untouched_note, 0xa5, sizeof(untouched_note));
    note_exists = 0; fail_write = writes = 0;
    assert(sc_storage_load(&device, &sc_catalog, &loaded) == SC_SAVE_NOT_FOUND);
    assert(sc_storage_store(&device, &sc_catalog, &snapshot, 0) == SC_SAVE_NOT_FOUND);
    assert(sc_storage_store(&device, &sc_catalog, &snapshot, 1) == SC_SAVE_OK);
    memcpy(backup, note, sizeof(note));
    for (failure = 1; failure <= 3; failure++) {
        memcpy(note, backup, sizeof(note));
        fail_write = (int)failure; writes = 0;
        snapshot.state.party[0].cur_hp--;
        assert(sc_storage_store(&device, &sc_catalog, &snapshot, 0) == SC_SAVE_IO);
        {
            int status = sc_storage_load(&device, &sc_catalog, &loaded);
            assert(status == SC_SAVE_OK || status == SC_SAVE_RECOVERED);
        }
        assert(memcmp(&loaded.state, &prior.state, sizeof(ScState)) == 0);
    }
    fail_write = writes = 0;
    memcpy(note, backup, sizeof(note));
    snapshot.interrupted = 0;
    assert(sc_storage_store(&device, &sc_catalog, &snapshot, 0) == SC_SAVE_OK);
    assert(sc_storage_load(&device, &sc_catalog, &loaded) == SC_SAVE_OK);
    assert(!loaded.interrupted && loaded.state.party[0].cur_hp == snapshot.state.party[0].cur_hp);
    for (i = 0; i < sizeof(untouched_note); i++) assert(untouched_note[i] == 0xa5);
    assert(sc_begin(&snapshot.state, &sc_catalog, find("OaksLab/rival-lab"), 2) == SC_OK);
    assert(sc_save_encode(&snapshot, &sc_catalog, 0, bytes) == SC_SAVE_INVALID);
    assert(sc_finish(&snapshot.state, &sc_catalog, SC_LOSS, 2) == SC_OK);
    assert(sc_save_encode(&snapshot, &sc_catalog, 0, bytes) == SC_SAVE_OK);
    assert(sc_save_decode(&loaded, &sc_catalog, &generation, bytes, sizeof(bytes)) == SC_SAVE_OK);
    assert(loaded.state.phase == SC_LOST);
}

static void test_ko_bridge_checkpoint_pause(void) {
    ScSaveDevice device = {0, note_open, note_read, note_write};
    ScOptions options = {0, SC_STANDARD, SC_STARTER_TRIO, 1};
    ScSnapshot snapshot, loaded;
    ScBridge bridge;
    ScBattleSnapshot party[6];
    unsigned i, j, encounter = find("OaksLab/rival-lab");
    int storage_status;
    memset(&bridge, 0, sizeof(bridge));
    assert(sc_new(&state, &sc_catalog, &options, 42) == SC_OK);
    state.party[0].level = 6;
    state.party[0].exp = sc_xp(7, state.party[0].growth) - 1;
    sc_recalculate(&state.party[0], 1);
    snapshot.state = state; snapshot.interrupted = 0;
    note_exists = 0; fail_write = writes = 0;
    assert(sc_storage_store(&device, &sc_catalog, &snapshot, 1) == SC_SAVE_OK);
    before = state;
    fail_write = 1; writes = 0;
    assert(sc_bridge_begin(&state, &bridge, &sc_catalog, &device, encounter, 2,
                            &storage_status) == SC_PENDING);
    assert(storage_status == SC_SAVE_IO && memcmp(&state, &before, sizeof(state)) == 0);
    fail_write = writes = 0;
    assert(sc_bridge_begin(&state, &bridge, &sc_catalog, &device, encounter, 2,
                            &storage_status) == SC_OK);
    assert(sc_storage_load(&device, &sc_catalog, &loaded) == SC_SAVE_OK);
    assert(loaded.interrupted && memcmp(&loaded.state, &before, sizeof(state)) == 0);
    memset(party, 0, sizeof(party));
    for (i = 0; i < state.party_count; i++) {
        party[i].hp = state.party[i].cur_hp;
        for (j = 0; j < 4; j++) party[i].pp[j] = state.party[i].pp[j];
    }
    assert(sc_bridge_observe(&state, &bridge, &sc_catalog, party, 0, 0, 20) == SC_OK);
    assert(sc_bridge_observe(&state, &bridge, &sc_catalog, party, 0, 0, 0) == SC_OK);
    assert(sc_bridge_paused(&state) && state.learn_mask == 1);
    before = state;
    assert(sc_bridge_observe(&state, &bridge, &sc_catalog, party, 0, 0, 0) == SC_PENDING);
    assert(memcmp(&state, &before, sizeof(state)) == 0);
    assert(sc_learn(&state, &sc_catalog, 0, 3, 0) == SC_OK);
    assert(!sc_bridge_paused(&state));
    before = state;
    assert(sc_bridge_observe(&state, &bridge, &sc_catalog, party, 0, 0, 0) == SC_DUPLICATE);
    assert(memcmp(&state, &before, sizeof(state)) == 0);
    before = state;
    fail_write = 1; writes = 0;
    assert(sc_bridge_commit(&state, &sc_catalog, &device, SC_WIN, &storage_status) == SC_PENDING);
    assert(storage_status == SC_SAVE_IO && memcmp(&state, &before, sizeof(state)) == 0);
    fail_write = writes = 0;
    assert(sc_bridge_commit(&state, &sc_catalog, &device, SC_WIN, &storage_status) == SC_OK);
    assert(sc_storage_load(&device, &sc_catalog, &loaded) == SC_SAVE_OK);
    assert(!loaded.interrupted && loaded.state.phase == SC_REWARD);
    assert(memcmp(&loaded.state, &state, sizeof(state)) == 0);
}

int main(void) {
    assert(sc_catalog_valid(&sc_catalog));
    test_starters();
    test_stat_source();
    test_all_species_levels();
    test_graph_reward();
    test_transfer();
    test_ko_and_learning();
    test_healing_and_loss();
    test_evolution();
    test_failed_inputs_do_not_mutate();
    test_canonical_activation();
    test_full_installed_graph();
    test_story_save();
    test_ko_bridge_checkpoint_pause();
    puts("Story reviewed graph/progression/save: OK (native/Pak/UI not wired)");
    return 0;
}
