/* PURE */
#include "sc_save.h"
#include "sc_graph.h"

#define SC_SAVE_MAGIC 0x31304353U
typedef struct ScWire {
    RlU8 *write;
    const RlU8 *read;
    unsigned pos;
} ScWire;

static RlU32 field(ScWire *wire, RlU32 value, unsigned width) {
    RlU32 result = 0;
    unsigned i;
    for (i = 0; i < width; i++) {
        if (wire->write) wire->write[wire->pos] = (RlU8)(value >> (8 * i));
        else result |= (RlU32)wire->read[wire->pos] << (8 * i);
        wire->pos++;
    }
    return wire->write ? value : result;
}

#define F(member, width) member = field(wire, member, width)

static void mon_wire(ScWire *wire, RlMon *mon) {
    unsigned i;
    F(mon->species, 1); F(mon->level, 1); F(mon->growth, 1); F(mon->fainted, 1);
    F(mon->exp, 4); F(mon->dvs, 2);
    for (i = 0; i < 5; i++) { F(mon->stat_exp[i], 2); }
    for (i = 0; i < 5; i++) { F(mon->base_stats[i], 1); }
    for (i = 0; i < 5; i++) { F(mon->current_stats[i], 2); }
    for (i = 0; i < 4; i++) { F(mon->moves[i], 1); F(mon->pp[i], 1); F(mon->max_pp[i], 1); }
    F(mon->cur_hp, 2); F(mon->max_hp, 2); F(mon->status, 1); F(mon->held_relic, 1);
    for (i = 0; i < 11; i++) { F(mon->nickname[i], 1); }
}

static void state_wire(ScWire *wire, ScState *state) {
    unsigned i;
    F(state->seed, 4); F(state->rng, 4); F(state->milestones, 4); F(state->healing_used, 4);
    F(state->options.version, 1); F(state->options.difficulty, 1);
    F(state->options.starter_pool, 1); F(state->options.starter, 1);
    F(state->phase, 1); F(state->party_count, 1); F(state->rival_branch, 1);
    F(state->defeated_mask, 1); F(state->evolution_mask, 1); F(state->encounter_variant, 1);
    F(state->learn_mask, 1);
    for (i = 0; i < 6; i++) { F(state->learn_moves[i], 1); }
    F(state->return_phase, 1); F(state->offered_slot, 1); F(state->fossil_choice, 1);
    F(state->encounter, 2);
    for (i = 0; i < SC_BITS; i++) { F(state->completed[i], 1); }
    for (i = 0; i < SC_BITS; i++) { F(state->retired[i], 1); }
    for (i = 0; i < 6; i++) mon_wire(wire, &state->party[i]);
    mon_wire(wire, &state->offer);
}
#undef F

static int mon_valid(const RlMon *mon, const ScCatalog *catalog) {
    const ScSpecies *species;
    RlMon minimum;
    unsigned i;
#ifdef SC_STORY_PACKED_MOVES
    if (!sc_moves_packed(mon)) return 0;
#endif
    if (!mon->species || mon->species > 151 || !mon->level || mon->level > 100 ||
        mon->fainted > 1 || mon->fainted != (mon->cur_hp == 0) ||
        !mon->max_hp || mon->cur_hp > mon->max_hp || mon->held_relic != RL_NO_SLOT ||
        mon->current_stats[0] != mon->max_hp || !mon->moves[0]) return 0;
    species = &catalog->species[mon->species];
    if (mon->growth != species->growth || mon->exp < sc_xp(mon->level, mon->growth) ||
        (mon->level < 100 && mon->exp >= sc_xp(mon->level + 1, mon->growth))) return 0;
    minimum = *mon;
    for (i = 0; i < 5; i++) minimum.stat_exp[i] = 0;
    for (i = 0; i < 5; i++)
        if (mon->base_stats[i] != species->base[i] ||
            mon->current_stats[i] < sc_stat(&minimum, i) ||
            mon->current_stats[i] > sc_stat(mon, i)) return 0;
    for (i = 0; i < 4; i++)
        if (mon->moves[i] > 165 || mon->max_pp[i] != catalog->pp[mon->moves[i]] ||
            mon->pp[i] > mon->max_pp[i]) return 0;
    for (i = 0; i < 11; i++) {
        if (!mon->nickname[i]) return 1;
        if ((unsigned char)mon->nickname[i] > 127) return 0;
    }
    return 0;
}

static int done(const ScState *state, unsigned i) {
    return (state->completed[i / 8] >> (i % 8)) & 1;
}

static int empty_mon(const RlMon *mon) {
    RlMon copy = *mon;
    RlU8 bytes[64];
    ScWire wire;
    unsigned i;
    wire.write = bytes; wire.read = 0; wire.pos = 0;
    mon_wire(&wire, &copy);
    for (i = 0; i < sizeof(bytes); i++) if (bytes[i]) return 0;
    return 1;
}

/* Private scratch keeps the Game_Thread stack bounded; nested use is refused. */
static ScState valid_expected;
static int valid_busy;

static int state_valid_body(const ScState *state, const ScCatalog *catalog) {
    unsigned i, j, valid_clause;
    ScState *expected = &valid_expected;
    if (!state || !sc_catalog_valid(catalog) || !catalog->graph || !catalog->graph_reviewed ||
        !state->rng || state->milestones || state->options.version > 1 ||
        state->options.difficulty > SC_BRUTAL || state->options.starter_pool > SC_ALL_SPECIES ||
        (state->options.starter && state->options.starter != 1 &&
         state->options.starter != 4 && state->options.starter != 7) ||
        !state->party_count || state->party_count > 6 || state->rival_branch > 2 ||
        state->phase > SC_FINISHED || state->phase == SC_BATTLE || state->phase == SC_LEARNING ||
        state->learn_mask || state->return_phase != SC_ROUTE ||
        state->evolution_mask >> state->party_count ||
        state->defeated_mask > 63 || state->healing_used >> catalog->graph->healing_count ||
        (state->encounter != SC_NO_ENCOUNTER && state->encounter >= catalog->count) ||
        (state->fossil_choice != RL_NO_SLOT && state->fossil_choice > 1)) return 0;
    if (done(state, catalog->graph->fossil_action) != (state->fossil_choice < 2)) return 0;
    if (!sc_stone_inventory_valid(state, catalog)) return 0;
    if (sc_new(expected, catalog, &state->options, state->seed) != SC_OK ||
        expected->rival_branch != state->rival_branch ||
        (state->phase == SC_FINISHED && !done(state, catalog->graph->champion))) return 0;
    for (i = 0; i < state->party_count; i++)
        if (!mon_valid(&state->party[i], catalog)) return 0;
    for (i = state->party_count; i < 6; i++)
        if (!empty_mon(&state->party[i])) return 0;
    if (state->phase != SC_REWARD &&
        (!empty_mon(&state->offer) || state->offered_slot != RL_NO_SLOT)) return 0;
    if (state->phase == SC_REWARD) {
        const ScEncounter *entry;
        RlMon team[6], offer;
        RlU8 actual[64], wanted[64];
        ScWire wire;
        unsigned count;
        if (state->encounter == SC_NO_ENCOUNTER || !mon_valid(&state->offer, catalog)) return 0;
        entry = &catalog->encounters[state->encounter];
        if (state->encounter_variant >= entry->variants ||
            state->offered_slot >= entry->teams[state->encounter_variant].count ||
            (entry->kind == SC_RIVAL && state->encounter_variant != state->rival_branch) ||
            (state->encounter == catalog->graph->fossil_offer &&
             state->encounter_variant != state->fossil_choice)) return 0;
        if (sc_team(state, catalog, state->encounter, state->encounter_variant,
                    team, &count) != SC_OK || state->offered_slot >= count) return 0;
        offer = state->offer;
        wire.write = actual; wire.read = 0; wire.pos = 0;
        mon_wire(&wire, &offer);
        wire.write = wanted; wire.pos = 0;
        mon_wire(&wire, &team[state->offered_slot]);
        for (i = 0; i < 64; i++) if (actual[i] != wanted[i]) return 0;
    }
    for (i = 0; i < 6; i++) if (state->learn_moves[i]) return 0;
    for (i = 0; i < SC_ENCOUNTER_CAPACITY; i++) {
        if (done(state, i) && ((state->retired[i / 8] >> (i % 8)) & 1)) return 0;
        if (i >= catalog->graph->count) {
            if (done(state, i) || ((state->retired[i / 8] >> (i % 8)) & 1)) return 0;
        } else if (done(state, i)) {
            const ScNode *node = &catalog->graph->nodes[i];
            valid_clause = 0;
            for (j = 0; j < node->clause_count; j++) {
                unsigned k;
                for (k = 0; k < node->clauses[j].count; k++)
                    if (!done(state, node->clauses[j].prior[k])) break;
                if (k == node->clauses[j].count) valid_clause = 1;
            }
            if (!valid_clause) return 0;
        }
    }
    *expected = *state;
    for (i = 0; i < SC_BITS; i++) expected->retired[i] = 0;
    sc_graph_retire(expected, catalog);
    for (i = 0; i < SC_BITS; i++) if (expected->retired[i] != state->retired[i]) return 0;
    return 1;
}

int sc_state_valid(const ScState *state, const ScCatalog *catalog) {
    int valid;
    if (valid_busy) return 0;
    valid_busy = 1;
    valid = state_valid_body(state, catalog);
    valid_busy = 0;
    return valid;
}

int sc_save_encode(const ScSnapshot *snapshot, const ScCatalog *catalog,
                   RlU32 generation, RlU8 data[SC_SAVE_SIZE]) {
    ScState state;
    ScWire wire;
    unsigned i;
    if (!snapshot || !data || !sc_state_valid(&snapshot->state, catalog) ||
        snapshot->interrupted > 1 ||
        (snapshot->interrupted && snapshot->state.phase != SC_ROUTE)) return SC_SAVE_INVALID;
    for (i = 0; i < SC_SAVE_SIZE; i++) data[i] = 0;
    wire.write = data; wire.read = 0; wire.pos = 0;
    field(&wire, SC_SAVE_MAGIC, 4); field(&wire, SC_SAVE_VERSION, 2); field(&wire, SC_SAVE_SIZE, 2);
    field(&wire, 0, 4); field(&wire, generation, 4);
    field(&wire, snapshot->interrupted, 1);
    for (i = 0; i < 64; i++) data[32 + i] = (RlU8)catalog->graph->review_identity[i];
    wire.pos = 96;
    state = snapshot->state;
    state_wire(&wire, &state);
    if (wire.pos != SC_SAVE_INVENTORY_OFFSET) return SC_SAVE_INVALID;
    field(&wire, state.moon_stones, 1);
    field(&wire, state.moon_stones_spent, 1);
    wire.pos = 8; field(&wire, sc_save_crc(data, SC_SAVE_SIZE), 4);
    return SC_SAVE_OK;
}

/* Private scratch keeps the Game_Thread stack bounded; nested use is refused. */
static ScSnapshot decode_next;
static int decode_busy;

static int decode_body(ScSnapshot *snapshot, const ScCatalog *catalog,
                       RlU32 *generation, const RlU8 *data, unsigned size) {
    ScSnapshot *next = &decode_next;
    ScWire wire;
    RlU32 crc, serial;
    unsigned i, version;
    const char *identity;
    if (!snapshot || !generation || !data || size != SC_SAVE_SIZE ||
        !sc_catalog_valid(catalog) || !catalog->graph || !catalog->graph_reviewed)
        return SC_SAVE_INVALID;
    wire.read = data; wire.write = 0; wire.pos = 0;
    if (field(&wire, 0, 4) != SC_SAVE_MAGIC) return SC_SAVE_BAD_DATA;
    version = field(&wire, 0, 2);
    if (version != 1 && version != SC_SAVE_VERSION) return SC_SAVE_BAD_VERSION;
    if (field(&wire, 0, 2) != SC_SAVE_SIZE) return SC_SAVE_BAD_DATA;
    crc = field(&wire, 0, 4);
    if (crc != sc_save_crc(data, size)) return SC_SAVE_BAD_CRC;
    serial = field(&wire, 0, 4);
    next->interrupted = (RlU8)field(&wire, 0, 1);
    if (next->interrupted > 1) return SC_SAVE_BAD_DATA;
    for (i = 17; i < 32; i++) if (data[i]) return SC_SAVE_BAD_DATA;
    identity = version == 1 && catalog->graph->legacy_review_identity ?
        catalog->graph->legacy_review_identity : catalog->graph->review_identity;
    for (i = 0; i < 64; i++)
        if (data[32 + i] != (RlU8)identity[i]) return SC_SAVE_BAD_VERSION;
    for (i = 0; i < sizeof(next->state); i++) ((RlU8 *)&next->state)[i] = 0;
    wire.pos = 96;
    state_wire(&wire, &next->state);
    if (wire.pos != SC_SAVE_INVENTORY_OFFSET) return SC_SAVE_BAD_DATA;
    if (version == SC_SAVE_VERSION) {
        next->state.moon_stones = (RlU8)field(&wire, 0, 1);
        next->state.moon_stones_spent = (RlU8)field(&wire, 0, 1);
    } else {
        /* V1 never had pickup actions; do not manufacture or accept their history. */
        for (i = 0; i < catalog->graph->pickup_count; i++)
            if (done(&next->state, catalog->graph->pickups[i].node)) return SC_SAVE_BAD_DATA;
    }
    for (i = wire.pos; i < size; i++) if (data[i]) return SC_SAVE_BAD_DATA;
    if (!sc_state_valid(&next->state, catalog) ||
        (next->interrupted && next->state.phase != SC_ROUTE)) return SC_SAVE_BAD_DATA;
    next->wire_version = (RlU8)version;
    *snapshot = *next;
    *generation = serial;
    return SC_SAVE_OK;
}

int sc_save_decode(ScSnapshot *snapshot, const ScCatalog *catalog,
                   RlU32 *generation, const RlU8 *data, unsigned size) {
    int status;
    if (decode_busy) return SC_SAVE_INVALID;
    decode_busy = 1;
    status = decode_body(snapshot, catalog, generation, data, size);
    decode_busy = 0;
    return status;
}

#ifdef SC_SCRATCH_TEST
void sc_scratch_test_hold_valid(int held) { valid_busy = held; }
int sc_scratch_test_busy_valid(void) { return valid_busy; }
#endif

#ifdef SC_SCRATCH_TEST
void sc_scratch_test_hold_decode(int held) { decode_busy = held; }
int sc_scratch_test_busy_decode(void) { return decode_busy; }
#endif
