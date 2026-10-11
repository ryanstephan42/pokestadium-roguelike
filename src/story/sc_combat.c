/* PURE. Combat inputs contain no catalog or overlay pointers. */
#include "sc_combat.h"

int sc_ko_values(ScState *state, const ScSpecies party_species[6],
                 const ScSpecies enemy_species[6], const ScTeam *team,
                 unsigned kind, unsigned enemy_slot, RlU8 participants) {
    ScState next;
    const ScSpecies *enemy;
    RlU32 award, value;
    unsigned n, i, j;
    if (!state || !party_species || !enemy_species || !team || state->party_count < 1 ||
        state->party_count > 6 || state->phase != SC_BATTLE ||
        !team->count || team->count > 6 || enemy_slot >= team->count ||
        kind > SC_GHOST || !participants || (participants >> state->party_count))
        return SC_INVALID;
    if (state->defeated_mask & (1U << enemy_slot)) return SC_DUPLICATE;
    i = team->mons[enemy_slot].species;
    if (!i || i > 151 || !team->mons[enemy_slot].level ||
        team->mons[enemy_slot].level > 100) return SC_INVALID;
    enemy = &enemy_species[enemy_slot];
    n = 0;
    for (i = 0; i < state->party_count; i++) n += (participants >> i) & 1;
    award = (RlU32)(enemy->yield / n) * team->mons[enemy_slot].level / 7;
    if (kind == SC_TRAINER || kind == SC_RIVAL) award += award / 2;
    next = *state;
    for (i = 0; i < state->party_count; i++) {
        unsigned old_level;
        if (!(participants & (1U << i)) || !next.party[i].cur_hp) continue;
        for (j = 0; j < 5; j++) {
            value = (RlU32)next.party[i].stat_exp[j] + enemy->base[j] / n;
            next.party[i].stat_exp[j] = (RlU16)(value > 65535U ? 65535U : value);
        }
        old_level = next.party[i].level;
        if (sc_add_xp_species(&next.party[i], award, &next.learn_moves[i], &party_species[i]) != SC_OK)
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

int sc_learn_pp(ScState *state, const RlU8 pp[166], unsigned party_slot,
                unsigned move_slot, int accept) {
    unsigned move, i;
    if (!state || !pp || state->phase != SC_LEARNING ||
        state->party_count > 6 || party_slot >= state->party_count ||
        move_slot >= 4 || !(state->learn_mask & (1U << party_slot)) ||
        (accept != 0 && accept != 1)) return SC_INVALID;
    move = state->learn_moves[party_slot];
    if (!move || move > 165 || !pp[move]) return SC_INVALID;
    if (accept) {
        for (i = 0; i < 4; i++) if (state->party[party_slot].moves[i] == move) return SC_DUPLICATE;
        state->party[party_slot].moves[move_slot] = (RlU8)move;
        state->party[party_slot].pp[move_slot] = state->party[party_slot].max_pp[move_slot] = pp[move];
    }
    state->learn_mask &= (RlU8)~(1U << party_slot);
    state->learn_moves[party_slot] = 0;
    if (!state->learn_mask) {
        state->phase = state->return_phase;
        state->return_phase = SC_ROUTE;
    }
    return SC_OK;
}

int sc_observe_values(ScState *state, ScBridge *bridge, const ScSpecies party_species[6],
                      const ScSpecies enemy_species[6], const ScTeam *team, unsigned kind,
                      const ScBattleSnapshot party[6], unsigned active_slot,
                      unsigned enemy_slot, unsigned enemy_hp) {
    ScState next;
    RlU8 participants;
    unsigned i, j;
    int status;
    if (!state || !bridge || !party_species || !enemy_species || !team || !party ||
        state->party_count > 6 ||
        active_slot >= state->party_count || enemy_slot >= bridge->enemy_count ||
        bridge->enemy_count > 6 || bridge->enemy_count != team->count || enemy_hp > 999)
        return SC_INVALID;
    if (state->phase == SC_LEARNING) return SC_PENDING;
    if (state->phase != SC_BATTLE) return SC_INVALID;
    next = *state;
    for (i = 0; i < state->party_count; i++) {
        if (party[i].hp > next.party[i].max_hp) return SC_INVALID;
        for (j = 0; j < 4; j++) if (party[i].pp[j] > next.party[i].max_pp[j]) return SC_INVALID;
        next.party[i].cur_hp = party[i].hp;
        next.party[i].fainted = party[i].hp == 0;
        next.party[i].status = party[i].status;
        for (j = 0; j < 4; j++) next.party[i].pp[j] = party[i].pp[j];
    }
    participants = bridge->participants[enemy_slot] | (RlU8)(1U << active_slot);
    if (!enemy_hp) {
        status = sc_ko_values(&next, party_species, enemy_species, team, kind,
                              enemy_slot, participants);
        if (status != SC_OK) return status;
    }
    bridge->participants[enemy_slot] = participants;
    *state = next;
    return SC_OK;
}

int sc_combat_observe(ScState *state, ScBridge *bridge, const ScCombat *combat,
                      const ScBattleSnapshot party[6], unsigned active_slot,
                      unsigned enemy_slot, unsigned enemy_hp) {
    unsigned i;
    if (!state || !combat || combat->ready != 1 || state->party_count > 6 ||
        state->encounter != combat->encounter ||
        state->encounter_variant != combat->variant) return SC_INVALID;
    for (i = 0; i < state->party_count; i++)
        if (state->party[i].species != combat->party_ids[i]) return SC_INVALID;
    return sc_observe_values(state, bridge, combat->party_species, combat->enemy_species,
                             &combat->team, combat->kind,
                             party, active_slot, enemy_slot, enemy_hp);
}
