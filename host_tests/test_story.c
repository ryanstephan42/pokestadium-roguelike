#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/story/sc_core.h"

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
    puts("Story data and isolated progression: OK (canon graph/native/save/UI not wired)");
    return 0;
}
