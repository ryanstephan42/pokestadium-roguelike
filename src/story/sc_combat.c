/* PURE. Combat inputs contain no catalog or overlay pointers. */
#include "sc_combat.h"

static int ko_apply_values(ScState *state, const ScSpecies party_species[6],
                           const ScSpecies enemy_species[6], const ScTeam *team,
                           unsigned kind, unsigned enemy_slot, RlU8 participants,
                           RlU8 recipients, int deferred) {
    ScState next;
    const ScSpecies *enemy;
    RlU32 award, value;
    unsigned n, i, j;
    if (!state || !party_species || !enemy_species || !team || state->party_count < 1 ||
        state->party_count > 6 || state->phase != SC_BATTLE ||
        !team->count || team->count > 6 || enemy_slot >= team->count ||
        kind > SC_GHOST || !participants || (participants >> state->party_count) ||
        (recipients & (RlU8)~participants))
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
        if (!(recipients & (1U << i))) continue;
        for (j = 0; j < 5; j++) {
            value = (RlU32)next.party[i].stat_exp[j] + enemy->base[j] / n;
            next.party[i].stat_exp[j] = (RlU16)(value > 65535U ? 65535U : value);
        }
        old_level = next.party[i].level;
        if ((deferred ? sc_add_xp_species_deferred : sc_add_xp_species)(
                &next.party[i], award, &next.learn_moves[i], &party_species[i]) != SC_OK)
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

int sc_ko_values(ScState *state, const ScSpecies party_species[6],
                 const ScSpecies enemy_species[6], const ScTeam *team,
                 unsigned kind, unsigned enemy_slot, RlU8 participants) {
    RlU8 recipients = 0;
    unsigned i;
    if (!state || state->party_count > 6) return SC_INVALID;
    for (i = 0; i < state->party_count; i++)
        if ((participants & (1U << i)) && state->party[i].cur_hp)
            recipients |= (RlU8)(1U << i);
    return ko_apply_values(state, party_species, enemy_species, team, kind,
                           enemy_slot, participants, recipients, 0);
}

void sc_ko_ledger_init(ScKoLedger *ledger) {
    unsigned i;
    if (!ledger) return;
    ledger->count = ledger->next = ledger->enemy_count = ledger->party_count = 0;
    ledger->operation_id = 0;
    ledger->encounter = 0;
    ledger->variant = ledger->bound = 0;
    for (i = 0; i < SC_KO_LEDGER_CAPACITY; i++) {
        ledger->events[i].enemy_slot = 0;
        ledger->events[i].participants = 0;
        ledger->events[i].recipients = 0;
    }
    for (i = 0; i < 6; i++)
        ledger->party_ids[i] = ledger->progression_ids[i] = 0;
}

void sc_ko_ledger_discard(ScKoLedger *ledger) {
    sc_ko_ledger_init(ledger);
}

int sc_ko_ledger_bind(ScKoLedger *ledger, const ScCombat *combat, RlU32 operation_id) {
    unsigned i;
    if (!ledger || !combat || combat->ready != 1 || !operation_id ||
        operation_id != combat->operation_id ||
        ledger->bound || ledger->count || ledger->next || ledger->enemy_count ||
        ledger->party_count || ledger->operation_id || !combat->team.count ||
        combat->team.count > SC_KO_LEDGER_CAPACITY || !combat->party_ids[0])
        return SC_INVALID;
    for (i = 0; i < 6; i++) {
        if (i < 5 && !combat->party_ids[i] && combat->party_ids[i + 1])
            return SC_INVALID;
        if (combat->party_ids[i] > 151) return SC_INVALID;
        ledger->party_ids[i] = ledger->progression_ids[i] = combat->party_ids[i];
    }
    ledger->operation_id = operation_id;
    ledger->encounter = combat->encounter;
    ledger->variant = combat->variant;
    ledger->enemy_count = combat->team.count;
    for (i = 0; i < 6 && combat->party_ids[i]; i++) {}
    if (!i || i > 6) { sc_ko_ledger_init(ledger); return SC_INVALID; }
    ledger->party_count = (RlU8)i;
    ledger->bound = 1;
    return SC_OK;
}

int sc_ko_ledger_follow_evolution(ScKoLedger *ledger, const ScCombat *combat,
                                  const ScState *state, unsigned party_slot,
                                  unsigned prior_species) {
    unsigned i;
    if (!ledger || !combat || !state || !ledger->bound ||
        combat->ready != 1 || !ledger->operation_id ||
        ledger->operation_id != combat->operation_id ||
        ledger->encounter != combat->encounter || ledger->variant != combat->variant ||
        ledger->party_count != state->party_count || party_slot >= ledger->party_count ||
        ledger->progression_ids[party_slot] != prior_species ||
        (state->phase != SC_BATTLE &&
         !(state->phase == SC_LEARNING && state->return_phase == SC_BATTLE)) ||
        !state->party[party_slot].species || state->party[party_slot].species == prior_species)
        return SC_INVALID;
    for (i = 0; i < 6; i++)
        if (ledger->party_ids[i] != combat->party_ids[i]) return SC_INVALID;
    ledger->progression_ids[party_slot] = state->party[party_slot].species;
    return SC_OK;
}

int sc_ko_ledger_record(ScKoLedger *ledger, unsigned enemy_slot,
                        unsigned enemy_count, unsigned party_count,
                        RlU8 participants, RlU8 recipients) {
    unsigned i;
    if (!ledger || !ledger->bound || !ledger->operation_id ||
        !enemy_count || enemy_count > SC_KO_LEDGER_CAPACITY ||
        enemy_slot >= enemy_count || !party_count || party_count > 6 ||
        !participants || (participants >> party_count) ||
        (recipients & (RlU8)~participants) ||
        ledger->count > SC_KO_LEDGER_CAPACITY || ledger->next ||
        ledger->enemy_count != enemy_count || ledger->party_count != party_count)
        return SC_INVALID;
    for (i = 0; i < ledger->count; i++)
        if (ledger->events[i].enemy_slot == enemy_slot) return SC_DUPLICATE;
    if (ledger->count >= enemy_count || ledger->count >= SC_KO_LEDGER_CAPACITY)
        return SC_FULL;
    ledger->events[ledger->count].enemy_slot = (RlU8)enemy_slot;
    ledger->events[ledger->count].participants = participants;
    ledger->events[ledger->count].recipients = recipients;
    ledger->enemy_count = (RlU8)enemy_count;
    ledger->party_count = (RlU8)party_count;
    ledger->count++;
    return SC_OK;
}

int sc_combat_capture_ko(ScBridge *bridge, ScKoLedger *ledger,
                         const ScCombat *combat, const ScBattleSnapshot party[6],
                         unsigned active_slot, unsigned enemy_slot, unsigned enemy_hp) {
    RlU8 participants, recipients = 0;
    ScKoLedger next;
    unsigned i, count = 0;
    int status;
    if (!bridge || !ledger || !combat || combat->ready != 1 || !party ||
        !combat->team.count || combat->team.count > 6 ||
        !combat->party_ids[0] || active_slot >= 6 ||
        !combat->party_ids[active_slot] || enemy_slot >= combat->team.count ||
        bridge->enemy_count != combat->team.count || enemy_hp > 999 ||
        !ledger->bound || !ledger->operation_id ||
        ledger->operation_id != combat->operation_id ||
        ledger->encounter != combat->encounter || ledger->variant != combat->variant ||
        ledger->enemy_count != combat->team.count || ledger->next ||
        ledger->count > ledger->enemy_count)
        return SC_INVALID;
    for (i = 0; i < 6; i++) {
        if (ledger->party_ids[i] != combat->party_ids[i]) return SC_INVALID;
        if (combat->party_ids[i]) count++;
    }
    if (count != ledger->party_count || active_slot >= count)
        return SC_INVALID;
    participants = bridge->participants[enemy_slot] | (RlU8)(1U << active_slot);
    if (!participants || (participants >> count)) return SC_INVALID;
    if (enemy_hp) {
        bridge->participants[enemy_slot] = participants;
        return SC_OK;
    }
    for (i = 0; i < count; i++) {
        if (party[i].hp) recipients |= (RlU8)(1U << i);
    }
    recipients &= participants;
    next = *ledger;
    status = sc_ko_ledger_record(&next, enemy_slot, combat->team.count, count,
                                 participants, recipients);
    if (status == SC_DUPLICATE) return SC_OK;
    if (status != SC_OK) return status;
    *ledger = next;
    bridge->participants[enemy_slot] = participants;
    return SC_OK;
}

int sc_ko_ledger_apply_next(ScKoLedger *ledger, ScState *state,
                            const ScCombat *combat) {
    const ScKoEvent *event;
    int status;
    if (!ledger || !state || !combat || combat->ready != 1 ||
        !ledger->bound || !ledger->operation_id ||
        ledger->operation_id != combat->operation_id ||
        ledger->encounter != combat->encounter || ledger->variant != combat->variant ||
        ledger->count > SC_KO_LEDGER_CAPACITY || ledger->next > ledger->count ||
        state->encounter != combat->encounter ||
        state->encounter_variant != combat->variant ||
        ledger->party_count != state->party_count ||
        ledger->enemy_count != combat->team.count)
        return SC_INVALID;
    {
        unsigned i;
        for (i = 0; i < 6; i++)
            if (ledger->party_ids[i] != combat->party_ids[i] ||
                (i < state->party_count &&
                 state->party[i].species != ledger->progression_ids[i]))
                return SC_INVALID;
    }
    if (!ledger->count)
        return state->phase != SC_BATTLE ? SC_INVALID : SC_OK;
    if (ledger->enemy_count != combat->team.count ||
        ledger->party_count != state->party_count)
        return SC_INVALID;
    if (state->phase == SC_LEARNING) return SC_PENDING;
    if (state->phase != SC_BATTLE) return SC_INVALID;
    if (ledger->next == ledger->count) return SC_OK;
    event = &ledger->events[ledger->next];
    status = ko_apply_values(state, combat->party_species, combat->enemy_species,
                             &combat->team, combat->kind, event->enemy_slot,
                             event->participants, event->recipients, 1);
    if (status != SC_OK) return status;
    ledger->next++;
    return state->phase == SC_LEARNING || ledger->next < ledger->count ?
        SC_PENDING : SC_OK;
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
#ifdef SC_STORY_PACKED_MOVES
        sc_compact_moves(&state->party[party_slot]);
        for (i = 0; i < 4; i++)
            if (!state->party[party_slot].moves[i]) { move_slot = i; break; }
#endif
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
    if (!enemy_hp && !(state->defeated_mask & (1U << enemy_slot))) {
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
