#ifdef STORY_NATIVE_PROBE
#include "sc_native_probe.h"

int sc_native_record(const RlMon *story, unk_func_80026268_arg0 *native) {
    unsigned i;
    if (!story || !native || story->species < 1 || story->species > 151 ||
        story->level < 1 || story->level > 100 ||
        native->unk_00.unk_00 != story->species ||
        !story->max_hp || story->cur_hp > story->max_hp ||
        story->current_stats[0] != story->max_hp) return SC_INVALID;
    for (i = 0; i < 5; i++)
        if (!story->current_stats[i] || story->current_stats[i] > 999) return SC_INVALID;
    for (i = 0; i < 4; i++)
        if (story->moves[i] > 165 || story->pp[i] > story->max_pp[i] ||
            story->max_pp[i] > 40) return SC_INVALID;
    native->unk_10 = story->exp;
    native->unk_24 = story->level;
    native->unk_1E = story->dvs;
    native->unk_14 = story->stat_exp[0];
    native->unk_16 = story->stat_exp[1];
    native->unk_18 = story->stat_exp[2];
    native->unk_1A = story->stat_exp[3];
    native->unk_1C = story->stat_exp[4];
    native->unk_26 = story->current_stats[0];
    native->unk_28 = story->current_stats[1];
    native->unk_2A = story->current_stats[2];
    native->unk_2C = story->current_stats[3];
    native->unk_2E = story->current_stats[4];
    native->unk_02 = story->cur_hp;
    native->unk_05 = story->status;
    for (i = 0; i < 4; i++) {
        native->unk_09[i] = story->moves[i];
        native->unk_20[i] = story->pp[i];
    }
    return SC_OK;
}

int sc_native_snapshot(const unk_func_80026268_arg0 *native,
                       const RlMon *story, ScBattleSnapshot *snapshot) {
    ScBattleSnapshot next;
    unsigned i;
    if (!native || !story || !snapshot ||
        native->unk_00.unk_00 != story->species ||
        native->unk_02 > story->max_hp) return SC_INVALID;
    next.hp = native->unk_02;
    next.status = native->unk_05;
    for (i = 0; i < 4; i++) {
        if (native->unk_09[i] != story->moves[i] ||
            (native->unk_20[i] & 63) > story->max_pp[i]) return SC_INVALID;
        next.pp[i] = native->unk_20[i] & 63;
    }
    *snapshot = next;
    return SC_OK;
}

static int mapping_valid(const ScState *state, unsigned count, const RlU8 mapping[6]) {
    unsigned i, j;
    if (!state || !mapping || !count || count > 6 || state->party_count > 6) return 0;
    for (i = 0; i < count; i++) {
        if (mapping[i] >= state->party_count) return 0;
        for (j = 0; j < i; j++) if (mapping[j] == mapping[i]) return 0;
    }
    for (i = 0; i < state->party_count; i++) {
        if (!state->party[i].cur_hp) continue;
        for (j = 0; j < count; j++) if (mapping[j] == i) break;
        if (j == count) return 0;
    }
    return 1;
}

#ifdef STORY_SIX_PROBE
int sc_native_mapping(const ScState *state, unsigned count, const RlU8 indices[6],
                       RlU8 output[6]) {
    unsigned i;
    if (!output || !mapping_valid(state, count, indices)) return SC_INVALID;
    for (i = 0; i < count; i++)
        if (!state->party[indices[i]].cur_hp || state->party[indices[i]].fainted)
            return SC_INVALID;
    for (i = 0; i < 6; i++) output[i] = i < count ? indices[i] : RL_NO_SLOT;
    return SC_OK;
}
#endif
int sc_native_capture(const ScState *state, const unk_func_80026268_arg0 *party,
                    unsigned count, const RlU8 mapping[6], unsigned active,
                    ScBattleSnapshot snapshots[6]) {
    unsigned i, j;
    if (!party || active >= count || !mapping_valid(state, count, mapping)) return SC_INVALID;
    for (i = 0; i < state->party_count; i++) {
        snapshots[i].hp = state->party[i].cur_hp;
        snapshots[i].status = state->party[i].status;
        for (j = 0; j < 4; j++) snapshots[i].pp[j] = state->party[i].pp[j];
    }
    for (i = 0; i < count; i++)
        if (sc_native_snapshot(&party[i], &state->party[mapping[i]],
                               &snapshots[mapping[i]]) != SC_OK) return SC_INVALID;
    return SC_OK;
}

int sc_native_observe_combat(ScState *state, ScBridge *bridge, const ScCombat *combat,
                             const unk_func_80026268_arg0 *party, unsigned count,
                             const RlU8 mapping[6], unsigned active, unsigned enemy,
                             unsigned live_enemy_hp) {
    ScBattleSnapshot snapshots[6];
    if (sc_native_capture(state, party, count, mapping, active, snapshots) != SC_OK) return SC_INVALID;
    return sc_combat_observe(state, bridge, combat, snapshots, mapping[active],
                             enemy, live_enemy_hp);
}
int sc_native_publish(const ScState *state, unk_func_80026268_arg0 *party,
                      unsigned count, const RlU8 mapping[6]) {
    unk_func_80026268_arg0 next[6];
    unsigned i;
    if (!party || !mapping_valid(state, count, mapping)) return SC_INVALID;
    for (i = 0; i < count; i++) {
        next[i] = party[i];
        if (sc_native_record(&state->party[mapping[i]], &next[i]) != SC_OK) return SC_INVALID;
    }
    for (i = 0; i < count; i++) party[i] = next[i];
    return SC_OK;
}
#endif
