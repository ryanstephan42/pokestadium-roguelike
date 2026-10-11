/* PURE */
#include "sc_graph.h"

static int bit(const RlU8 *bits, unsigned index) {
    return (bits[index / 8] >> (index % 8)) & 1;
}

static void set_bit(RlU8 *bits, unsigned index) {
    bits[index / 8] |= (RlU8)(1U << (index % 8));
}

static int gate_valid(const ScGate *gate, unsigned count) {
    unsigned i;
    if (!gate || count > SC_ENCOUNTER_CAPACITY ||
        gate->prior_count > 6 || (gate->required & gate->forbidden))
        return 0;
    for (i = 0; i < gate->prior_count; i++)
        if (gate->prior[i] >= count) return 0;
    return 1;
}

static int gate_open(const ScState *state, const ScGate *gate) {
    unsigned i;
    if ((state->milestones & gate->required) != gate->required ||
        (state->milestones & gate->forbidden) ||
        (gate->any && !(state->milestones & gate->any))) return 0;
    for (i = 0; i < gate->prior_count; i++)
        if (!bit(state->completed, gate->prior[i])) return 0;
    return 1;
}

static int unevolved(const ScCatalog *catalog, unsigned species) {
    unsigned j, k;
    if (species == 144 || species == 145 || species == 146 || species == 150 || species == 151)
        return 0;
    for (j = 1; j <= 151; j++)
        for (k = 0; k < catalog->species[j].evolution_count; k++)
            if (catalog->species[j].evolution[k].species == species) return 0;
    return 1;
}

RlU32 sc_random(RlU32 *state) {
    RlU32 value = *state ? *state : 0x73633031U;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    *state = value;
    return value;
}

RlU32 sc_random_bounded(RlU32 *state, RlU32 bound) {
    RlU32 value, threshold;
    if (!state || !bound) return 0;
    threshold = (0U - bound) % bound;
    do { value = sc_random(state); } while (value < threshold);
    return value % bound;
}

int sc_catalog_valid(const ScCatalog *catalog) {
    unsigned i, j, k, n;
    const ScSpecies *species;
    const ScEncounter *encounter;
    const ScTemplate *mon;
    if (!catalog || !catalog->species || !catalog->pp || !catalog->encounters ||
        !catalog->count || catalog->count > SC_ENCOUNTER_CAPACITY ||
        catalog->graph_reviewed > 1 || catalog->pp[0]) return 0;
    for (i = 1; i <= 165; i++)
        if (!catalog->pp[i] || catalog->pp[i] > 40) return 0;
    for (i = 1; i <= 151; i++) {
        species = &catalog->species[i];
        if (species->growth > RL_MEDIUM_SLOW || !species->yield ||
            !species->initial[0] || species->learn_count > SC_LEARN_CAPACITY ||
            species->evolution_count > SC_EVOLUTION_CAPACITY) return 0;
        for (j = 0; j < 5; j++) if (!species->base[j]) return 0;
        for (j = 0; j < 4; j++) if (species->initial[j] > 165) return 0;
        for (j = 0; j < species->learn_count; j++) {
            if (!species->learn[j].level || species->learn[j].level > 100 ||
                !species->learn[j].move || species->learn[j].move > 165 ||
                (j && species->learn[j].level < species->learn[j - 1].level)) return 0;
        }
        for (j = 0; j < species->evolution_count; j++) {
            const ScEvolution *evo = &species->evolution[j];
            if (evo->method < 1 || evo->method > 3 || !evo->level || evo->level > 100 ||
                !evo->species || evo->species > 151 || evo->species == i ||
                (evo->method == SC_STONE_EVOLUTION && (!evo->stone || evo->stone > 5)) ||
                (evo->method != SC_STONE_EVOLUTION && evo->stone)) return 0;
        }
    }
    for (i = 0; i < catalog->count; i++) {
        unsigned previous;
        encounter = &catalog->encounters[i];
        if (!encounter->id || !encounter->id[0] || encounter->kind > SC_GHOST ||
            !encounter->variants || encounter->variants > 3 ||
            (encounter->kind == SC_RIVAL && encounter->variants != 3) ||
            !gate_valid(&encounter->gate, catalog->count)) return 0;
        for (previous = 0; previous < i; previous++) {
            const char *a = encounter->id;
            const char *b = catalog->encounters[previous].id;
            while (*a && *a == *b) { a++; b++; }
            if (*a == *b) return 0;
        }
        for (j = 0; j < encounter->variants; j++) {
            n = encounter->teams[j].count;
            if (!n || n > 6 || (encounter->kind >= SC_STATIC && n != 1)) return 0;
            for (k = 0; k < n; k++) {
                unsigned slot;
                mon = &encounter->teams[j].mons[k];
                if (!mon->species || mon->species > 151 || !mon->level || mon->level > 100 ||
                    !mon->moves[0] || mon->fixed_dvs > 1) return 0;
                for (slot = 0; slot < 4; slot++) if (mon->moves[slot] > 165) return 0;
            }
        }
    }
    return sc_graph_valid(catalog);
}

int sc_new(ScState *state, const ScCatalog *catalog, const ScOptions *options,
           RlU32 seed) {
    static const RlU8 starters[3] = {1, 4, 7};
    ScState next;
    ScTemplate mon;
    unsigned char *bytes;
    unsigned i, species, choices, selected;
    if (!state || !options || options->version > 1 || options->difficulty > SC_BRUTAL ||
        options->starter_pool > SC_ALL_SPECIES ||
        (options->starter && options->starter != 1 && options->starter != 4 && options->starter != 7) ||
        !sc_catalog_valid(catalog)) return SC_INVALID;
    if (!catalog->graph_reviewed) return SC_UNREVIEWED;
    bytes = (unsigned char *)&next;
    for (i = 0; i < sizeof(next); i++) bytes[i] = 0;
    bytes = (unsigned char *)&mon;
    for (i = 0; i < sizeof(mon); i++) bytes[i] = 0;
    next.options = *options;
    next.seed = seed;
    next.rng = seed ? seed : 0x73633031U;
    next.encounter = SC_NO_ENCOUNTER;
    next.offered_slot = RL_NO_SLOT;
    next.fossil_choice = RL_NO_SLOT;
    species = options->starter;
    if (!species && options->starter_pool == SC_STARTER_TRIO)
        species = starters[sc_random_bounded(&next.rng, 3)];
    else if (!species && options->starter_pool == SC_ALL_SPECIES)
        species = 1 + sc_random_bounded(&next.rng, 151);
    else if (!species) {
        choices = 0;
        for (i = 1; i <= 151; i++) if (unevolved(catalog, i)) choices++;
        if (!choices) return SC_INVALID;
        selected = sc_random_bounded(&next.rng, choices);
        for (i = 1; i <= 151; i++) {
            if (unevolved(catalog, i)) {
                if (!selected) { species = i; break; }
                selected--;
            }
        }
    }
    if (options->starter || options->starter_pool == SC_STARTER_TRIO) {
        if (species == 1) next.rival_branch = 2; /* Charmander branch. */
        else if (species == 4) next.rival_branch = 0; /* Squirtle branch. */
        else next.rival_branch = 1; /* Bulbasaur branch. */
    } else next.rival_branch = (RlU8)sc_random_bounded(&next.rng, 3);
    mon.species = (RlU8)species;
    mon.level = 5;
    if (sc_make_mon(catalog, &mon, (RlU16)sc_random(&next.rng), &next.party[0]) != SC_OK)
        return SC_INVALID;
    next.party_count = 1;
    next.phase = SC_ROUTE;
    *state = next;
    return SC_OK;
}

int sc_available(const ScState *state, const ScCatalog *catalog, unsigned encounter) {
    const ScEncounter *entry;
    if (!state || !catalog || !catalog->encounters || encounter >= catalog->count ||
        catalog->count > SC_ENCOUNTER_CAPACITY || state->phase != SC_ROUTE ||
        !catalog->graph_reviewed) return 0;
    entry = &catalog->encounters[encounter];
    return gate_valid(&entry->gate, catalog->count) &&
        !bit(state->completed, encounter) && !bit(state->retired, encounter) &&
        !(entry->retires_at & state->milestones) &&
        gate_open(state, &entry->gate) &&
        (!catalog->graph || sc_graph_open(state, catalog, encounter));
}

static int variant_valid(const ScState *state, const ScEncounter *entry, unsigned variant) {
    return variant < entry->variants &&
        (entry->kind != SC_RIVAL || variant == state->rival_branch);
}

int sc_team(const ScState *state, const ScCatalog *catalog, unsigned encounter,
            unsigned variant, RlMon output[6], unsigned *count) {
    const ScEncounter *entry;
    RlMon mons[6];
    RlU32 rng;
    unsigned i;
    if (!state || !catalog || !catalog->encounters || !output || !count ||
        catalog->count > SC_ENCOUNTER_CAPACITY || encounter >= catalog->count) return SC_INVALID;
    entry = &catalog->encounters[encounter];
    if (catalog->graph && encounter == catalog->graph->fossil_offer &&
        variant != state->fossil_choice) return SC_INVALID;
    if (!variant_valid(state, entry, variant) || !entry->teams[variant].count ||
        entry->teams[variant].count > 6) return SC_INVALID;
    rng = state->seed ^ ((RlU32)(encounter + 1) * 0x9e3779b9U) ^ variant;
    for (i = 0; i < entry->teams[variant].count; i++)
        if (sc_make_mon(catalog, &entry->teams[variant].mons[i],
                        (RlU16)sc_random(&rng), &mons[i]) != SC_OK) return SC_INVALID;
    for (i = 0; i < entry->teams[variant].count; i++) output[i] = mons[i];
    *count = entry->teams[variant].count;
    return SC_OK;
}

int sc_begin(ScState *state, const ScCatalog *catalog, unsigned encounter,
             unsigned variant) {
    const ScEncounter *entry;
    RlMon mons[6];
    unsigned count, i;
    if (!state || !catalog || !catalog->graph_reviewed) return SC_UNREVIEWED;
    if (!sc_available(state, catalog, encounter)) return SC_UNAVAILABLE;
    if (!state->party_count || state->party_count > 6) return SC_INVALID;
    entry = &catalog->encounters[encounter];
    if (sc_team(state, catalog, encounter, variant, mons, &count) != SC_OK) return SC_INVALID;
    for (i = 0; i < state->party_count; i++) if (state->party[i].cur_hp) break;
    if (i == state->party_count)
        return SC_UNAVAILABLE;
    state->encounter = (RlU16)encounter;
    state->encounter_variant = (RlU8)variant;
    state->defeated_mask = 0;
    state->evolution_mask = 0;
    state->learn_mask = 0;
    state->offered_slot = RL_NO_SLOT;
    if (entry->kind == SC_GIFT) {
        state->offer = mons[0];
        state->offered_slot = 0;
        state->phase = SC_REWARD;
    } else state->phase = SC_BATTLE;
    return SC_OK;
}

int sc_on_ko(ScState *state, const ScCatalog *catalog, unsigned enemy_slot,
             RlU8 participants, unsigned variant) {
    ScState next;
    const ScEncounter *entry;
    const ScSpecies *enemy;
    RlU32 award, value;
    unsigned n, i, j;
    if (!state || !catalog || !catalog->species || !catalog->encounters ||
        catalog->count > SC_ENCOUNTER_CAPACITY ||
        state->encounter >= catalog->count || state->party_count < 1 ||
        state->party_count > 6 || state->phase != SC_BATTLE ||
        variant != state->encounter_variant) return SC_INVALID;
    entry = &catalog->encounters[state->encounter];
    if (!variant_valid(state, entry, variant) || enemy_slot >= entry->teams[variant].count ||
        enemy_slot >= 6 || !participants || (participants >> state->party_count))
        return SC_INVALID;
    if (state->defeated_mask & (1U << enemy_slot)) return SC_DUPLICATE;
    i = entry->teams[variant].mons[enemy_slot].species;
    if (!i || i > 151) return SC_INVALID;
    enemy = &catalog->species[i];
    n = 0;
    for (i = 0; i < state->party_count; i++) n += (participants >> i) & 1;
    award = (RlU32)(enemy->yield / n) * entry->teams[variant].mons[enemy_slot].level / 7;
    if (entry->kind == SC_TRAINER || entry->kind == SC_RIVAL) award += award / 2;
    next = *state;
    for (i = 0; i < state->party_count; i++) {
        unsigned old_level;
        if (!(participants & (1U << i)) || !next.party[i].cur_hp) continue;
        for (j = 0; j < 5; j++) {
            value = (RlU32)next.party[i].stat_exp[j] + enemy->base[j] / n;
            next.party[i].stat_exp[j] = (RlU16)(value > 65535U ? 65535U : value);
        }
        old_level = next.party[i].level;
        if (sc_add_xp(&next.party[i], award, &next.learn_moves[i], catalog) != SC_OK)
            return SC_INVALID;
        if (next.party[i].level > old_level) next.evolution_mask |= (RlU8)(1U << i);
        if (next.learn_moves[i]) next.learn_mask |= (RlU8)(1U << i);
    }
    next.defeated_mask |= (RlU8)(1U << enemy_slot);
    if (next.learn_mask) {
        next.phase = SC_LEARNING;
        next.return_phase = SC_BATTLE;
    }
    *state = next;
    return SC_OK;
}

static void retire(ScState *state, const ScCatalog *catalog) {
    unsigned i;
    for (i = 0; i < catalog->count; i++)
        if (catalog->encounters[i].retires_at & state->milestones) set_bit(state->retired, i);
    sc_graph_retire(state, catalog);
}

int sc_grant(ScState *state, const ScCatalog *catalog, RlU32 milestones) {
    if (!state || !catalog || !catalog->encounters || !catalog->graph_reviewed ||
        catalog->count > SC_ENCOUNTER_CAPACITY || state->phase != SC_ROUTE || catalog->graph)
        return SC_INVALID;
    state->milestones |= milestones;
    retire(state, catalog);
    return SC_OK;
}

int sc_finish(ScState *state, const ScCatalog *catalog, unsigned outcome,
              unsigned variant) {
    const ScEncounter *entry;
    RlMon mons[6];
    unsigned count, selected;
    if (!state || !catalog || !catalog->encounters || catalog->count > SC_ENCOUNTER_CAPACITY ||
        state->encounter >= catalog->count ||
        state->phase != SC_BATTLE || outcome > SC_DRAW ||
        variant != state->encounter_variant) return SC_INVALID;
    entry = &catalog->encounters[state->encounter];
    if (!variant_valid(state, entry, variant)) return SC_INVALID;
    if (outcome != SC_WIN) {
        state->phase = SC_LOST;
        return SC_OK;
    }
    count = entry->teams[variant].count;
    if (!count || count > 6 || state->defeated_mask != (1U << count) - 1U)
        return SC_PENDING;
    if (sc_team(state, catalog, state->encounter, variant, mons, &count) != SC_OK)
        return SC_INVALID;
    set_bit(state->completed, state->encounter);
    state->milestones |= entry->grants;
    retire(state, catalog);
    if (entry->kind == SC_GHOST) {
        state->phase = SC_ROUTE;
        return SC_OK;
    }
    selected = sc_random_bounded(&state->rng, count);
    state->offer = mons[selected];
    state->offered_slot = (RlU8)selected;
    state->phase = SC_REWARD;
    return SC_OK;
}

static int resolve_offer(ScState *state, const ScCatalog *catalog) {
    if (!state || !catalog || !catalog->encounters || catalog->count > SC_ENCOUNTER_CAPACITY ||
        state->phase != SC_REWARD || state->encounter >= catalog->count ||
        state->offered_slot == RL_NO_SLOT) return SC_INVALID;
    set_bit(state->completed, state->encounter);
    state->milestones |= catalog->encounters[state->encounter].grants;
    retire(state, catalog);
    state->offered_slot = RL_NO_SLOT;
    state->phase = SC_ROUTE;
    return SC_OK;
}

int sc_recruit(ScState *state, const ScCatalog *catalog, unsigned outgoing_slot, int confirmed) {
    unsigned slot;
    if (!state || !catalog || !catalog->encounters || catalog->count > SC_ENCOUNTER_CAPACITY ||
        state->phase != SC_REWARD || !state->offer.species ||
        state->party_count < 1 || state->party_count > 6 ||
        state->offered_slot == RL_NO_SLOT || state->encounter >= catalog->count ||
        (confirmed != 0 && confirmed != 1)) return SC_INVALID;
    slot = state->party_count;
    if (slot == 6) {
        if (outgoing_slot >= 6) return SC_FULL;
        slot = outgoing_slot;
    }
    if (!confirmed) return SC_PENDING;
    state->party[slot] = state->offer;
    state->evolution_mask &= (RlU8)~(1U << slot);
    if (state->party_count < 6) state->party_count++;
    return resolve_offer(state, catalog);
}

int sc_transfer(ScState *state, const ScCatalog *catalog, unsigned donor_move,
                unsigned party_slot, unsigned move_slot, int confirmed) {
    unsigned move, i;
    if (!state || !catalog || !catalog->pp || !catalog->encounters ||
        catalog->count > SC_ENCOUNTER_CAPACITY || state->phase != SC_REWARD ||
        donor_move >= 4 || party_slot >= state->party_count || state->party_count > 6 ||
        move_slot >= 4 || (confirmed != 0 && confirmed != 1) ||
        state->encounter >= catalog->count || state->offered_slot == RL_NO_SLOT)
        return SC_INVALID;
    move = state->offer.moves[donor_move];
    if (!move || move > 165 || !catalog->pp[move]) return SC_INVALID;
    for (i = 0; i < 4; i++) if (state->party[party_slot].moves[i] == move) return SC_DUPLICATE;
    if (!confirmed) return SC_PENDING;
    state->party[party_slot].moves[move_slot] = (RlU8)move;
    state->party[party_slot].pp[move_slot] = catalog->pp[move];
    state->party[party_slot].max_pp[move_slot] = catalog->pp[move];
    return resolve_offer(state, catalog);
}

int sc_decline(ScState *state, const ScCatalog *catalog) {
    return resolve_offer(state, catalog);
}

int sc_apply_heal(ScState *state, unsigned location) {
    unsigned i, j;
    if (!state || location >= 32 ||
        state->party_count < 1 || state->party_count > 6 || state->phase != SC_ROUTE ||
        state->options.difficulty > SC_BRUTAL) return SC_INVALID;
    if (state->options.difficulty == SC_BRUTAL ||
        (state->options.difficulty == SC_LIMITED && (state->healing_used & (1U << location))))
        return SC_UNAVAILABLE;
    for (i = 0; i < state->party_count; i++) {
        state->party[i].cur_hp = state->party[i].max_hp;
        state->party[i].status = state->party[i].fainted = 0;
        for (j = 0; j < 4; j++) state->party[i].pp[j] = state->party[i].max_pp[j];
    }
    state->healing_used |= 1U << location;
    return SC_OK;
}

int sc_heal(ScState *state, const ScCatalog *catalog, unsigned location,
            const ScGate *gate) {
    if (!state || !catalog) return SC_INVALID;
    if (catalog->graph) return sc_location_heal(state, catalog, location);
    if (!gate_valid(gate, catalog->count)) return SC_INVALID;
    if (!gate_open(state, gate)) return SC_UNAVAILABLE;
    return sc_apply_heal(state, location);
}

int sc_learn(ScState *state, const ScCatalog *catalog, unsigned party_slot,
             unsigned move_slot, int accept) {
    unsigned move, i;
    if (!state || !catalog || !catalog->pp || state->phase != SC_LEARNING ||
        state->party_count > 6 || party_slot >= state->party_count ||
        move_slot >= 4 || !(state->learn_mask & (1U << party_slot)) ||
        (accept != 0 && accept != 1)) return SC_INVALID;
    move = state->learn_moves[party_slot];
    if (!move || move > 165 || !catalog->pp[move]) return SC_INVALID;
    if (accept) {
        for (i = 0; i < 4; i++) if (state->party[party_slot].moves[i] == move) return SC_DUPLICATE;
        state->party[party_slot].moves[move_slot] = (RlU8)move;
        state->party[party_slot].pp[move_slot] = state->party[party_slot].max_pp[move_slot] = catalog->pp[move];
    }
    state->learn_mask &= (RlU8)~(1U << party_slot);
    state->learn_moves[party_slot] = 0;
    if (!state->learn_mask) state->phase = state->return_phase;
    return SC_OK;
}

int sc_evolve(ScState *state, const ScCatalog *catalog, unsigned slot,
              unsigned evolution, unsigned stone_mask, int trade_unlocked,
              int confirmed) {
    const ScSpecies *old, *species;
    const ScEvolution *evo;
    RlMon mon;
    unsigned i, j;
    if (!state || !catalog || !catalog->species || !catalog->pp ||
        state->party_count > 6 || slot >= state->party_count || state->phase != SC_ROUTE ||
        (confirmed != 0 && confirmed != 1) || (trade_unlocked != 0 && trade_unlocked != 1))
        return SC_INVALID;
    mon = state->party[slot];
    if (!mon.species || mon.species > 151) return SC_INVALID;
    old = &catalog->species[mon.species];
    if (evolution >= old->evolution_count || old->evolution_count > SC_EVOLUTION_CAPACITY)
        return SC_INVALID;
    evo = &old->evolution[evolution];
    if (evo->method < 1 || evo->method > 3) return SC_INVALID;
    if (!evo->species || evo->species > 151 || mon.level < evo->level ||
        (evo->method == SC_LEVEL_EVOLUTION && !(state->evolution_mask & (1U << slot))) ||
        (evo->method == SC_STONE_EVOLUTION && (!evo->stone || evo->stone > 5 ||
                                              !(stone_mask & (1U << evo->stone)))) ||
        (evo->method == SC_TRADE_EVOLUTION && !trade_unlocked)) return SC_UNAVAILABLE;
    if (!confirmed) {
        if (evo->method == SC_LEVEL_EVOLUTION) state->evolution_mask &= (RlU8)~(1U << slot);
        return SC_PENDING;
    }
    species = &catalog->species[evo->species];
    if (species->growth != mon.growth || species->learn_count > SC_LEARN_CAPACITY)
        return SC_INVALID;
    mon.species = evo->species;
    for (i = 0; i < 5; i++) mon.base_stats[i] = species->base[i];
    sc_recalculate(&mon, 1);
    state->party[slot] = mon;
    state->evolution_mask &= (RlU8)~(1U << slot);
    for (i = 0; i < species->learn_count; i++) {
        if (species->learn[i].level != mon.level) continue;
        for (j = 0; j < 4; j++) if (mon.moves[j] == species->learn[i].move) return SC_OK;
        state->learn_moves[slot] = species->learn[i].move;
        state->learn_mask = (RlU8)(1U << slot);
        state->return_phase = SC_ROUTE;
        state->phase = SC_LEARNING;
        break;
    }
    return SC_OK;
}
