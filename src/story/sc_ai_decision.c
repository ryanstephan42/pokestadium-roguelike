/* Noninstalled two-phase owner. No retail hook or exported entry pointer. */
#include "sc_ai_decision.h"

static ScAiDecision *workspace_owner;

static void copy_bytes(void *destination, const void *source, unsigned size) {
    u8 *d = destination;
    const u8 *s = source;
    while (size--) *d++ = *s++;
}

static void clear_bytes(void *destination, unsigned size) {
    u8 *d = destination;
    while (size--) *d++ = 0;
}

static int same_bytes(const void *left, const void *right, unsigned size) {
    const u8 *a = left, *b = right;
    while (size--) if (*a++ != *b++) return 0;
    return 1;
}

static int fail_stop(ScAiDecision *d) {
    d->phase = SC_AI_DECISION_FAILED;
    d->committed.error = SC_AI_RESTORE_FAILED;
    return SC_AI_RESTORE_FAILED;
}

static int expected(ScAiDecision *d, const ScAiDecisionToken *t) {
    return d && t && d->committed.generation &&
        workspace_owner == d &&
        t->battle == d->committed.generation && t->battle == d->token.battle &&
        t->operation && t->operation == d->token.operation &&
        t->owner == d->token.owner && t->domain == d->token.domain &&
        t->party_count == d->token.party_count &&
        same_bytes(t->mapping, d->token.mapping, 6);
}

static int capture_view(ScAiDecisionView *v, unsigned owner) {
    unk_D_84390010 *side = D_84390010[owner];
    unk_D_800AE540_0004 *group;
    unsigned i;
    if (!side || !side->unk_720 || side->unk_720->unk_01 != 1 ||
        side->unk_654.unk_2B || side->unk_654.unk_2C) return 0;
    group = side->unk_720->unk_08[0];
    if (!group || group != side->unk_724 || !group->unk_214 ||
        !group->unk_002 || group->unk_002 > 6 ||
        group->unk_214->unk_002 != group->unk_002 ||
        side->unk_654.unk_08 >= group->unk_002) return 0;
    v->side = side;
    v->registration = side->unk_720;
    v->group = group;
    v->metadata = group->unk_214;
    v->active = side->unk_654.unk_08;
    v->state = side->unk_654.unk_2D;
    v->count = group->unk_002;
    for (i = 0; i < v->count; i++) {
        v->species[i] = group->unk_01C[i].unk_00.unk_00;
        v->hp[i] = group->unk_01C[i].unk_02;
        if (!v->species[i] || v->species[i] > 151) return 0;
    }
    copy_bytes(&v->live, &side->unk_654.unk_38, sizeof(v->live));
    return v->live.unk_0B == v->species[v->active];
}

static int view_matches(const ScAiDecisionView *v, unsigned owner, int consumer) {
    ScAiDecisionView now;
    clear_bytes(&now, sizeof(now));
    if (!capture_view(&now, owner) || now.side != v->side ||
        now.registration != v->registration || now.group != v->group ||
        now.metadata != v->metadata || now.active != v->active ||
        now.state != v->state || now.count != v->count ||
        !same_bytes(now.species, v->species, 6) ||
        !same_bytes(now.hp, v->hp, sizeof(now.hp))) return 0;
    /* DFCC's bit8 write is not a choice commit. EB1C permits flag20 transition. */
    now.live.unk_4C &= ~(consumer ? 0x28 : 8);
    {
        unk_D_800FCB18 original;
        copy_bytes(&original, &v->live, sizeof(original));
        original.unk_4C &= ~(consumer ? 0x28 : 8);
        return same_bytes(&now.live, &original, sizeof(original));
    }
}

static unsigned usable(const unk_D_800FCB18 *live) {
    unsigned i, n = 0;
    for (i = 0; i < 4; i++)
        if (live->unk_1F[i] && (live->unk_32[i] & 0x3F) &&
            (!live->unk_52 || (live->unk_52 >> 4) != i)) n++;
    return n;
}

static int initial_route(ScAiDecision *d, unsigned owner, unsigned domain) {
    unk_D_84390010_654 *actor = &D_84390010[owner]->unk_654;
    unk_D_800FCB18 *live = &actor->unk_38;
    if (domain == SC_AI_D9EC)
        return actor->unk_2D == 0x10 && actor->unk_10 != 3 && *d->clock == 20.0f;
    return domain == SC_AI_DF60 && actor->unk_2D != 0x10 &&
        actor->unk_10 != 3 && !(live->unk_4D & 0x60) &&
        !(live->unk_4C & 0x12) && *d->clock == 1.0f;
}

static int acceptance_route(ScAiDecision *d, unsigned route) {
    unsigned owner = d->token.owner, slot = d->choice.native_slot;
    unk_D_84390010_654 *actor = &D_84390010[owner]->unk_654;
    unk_D_84390010_654 *opponent = &D_84390010[1 - owner]->unk_654;
    unk_D_800FCB18 *live = &actor->unk_38;
    unk_D_800AE540_0004 *group = d->views[owner].group;
    if (d->choice.kind == SC_AI_SWITCH) {
        if (slot >= group->unk_002 || slot == actor->unk_08 ||
            !group->unk_01C[slot].unk_02 ||
            d->choice.compact_slot != d->token.mapping[slot]) return 0;
        if (route == SC_AI_D9EC_STORE)
            return d->token.domain == SC_AI_D9EC && initial_route(d, owner, SC_AI_D9EC);
        if (route != SC_AI_DE20 || d->token.domain != SC_AI_DF60) return 0;
    } else {
        unsigned move = d->choice.move_slot;
        if (route != SC_AI_DCD4 || d->token.domain != SC_AI_DF60 ||
            !live->unk_0C || (live->unk_15 & 0x27) || (live->unk_4C & 0x21) ||
            (opponent->unk_38.unk_4C & 0x20)) return 0;
        if (d->choice.kind == SC_AI_STRUGGLE) {
            if (move != 4 || usable(live)) return 0;
        } else if (d->choice.kind != SC_AI_MOVE || move >= 4 ||
            !live->unk_1F[move] || !(live->unk_32[move] & 0x3F) ||
            (live->unk_52 && (live->unk_52 >> 4) == move)) return 0;
    }
    return actor->unk_2D != 0x10 && actor->unk_10 != 3 &&
        !(live->unk_4D & 0x60) && !(live->unk_4C & 0x12) &&
        ((*d->clock >= 90.0f) ||
         (opponent->unk_10 == 3 && *d->clock >= 10.0f));
}

int sc_ai_decision_init(ScAiDecision *d, u32 battle, const f32 *clock) {
    if (workspace_owner) return SC_AI_NESTED;
    if (!d || !battle || !clock || d->committed.generation || d->committed.lock_owner ||
        d->phase != SC_AI_DECISION_IDLE || d->committed.error) return SC_AI_INVALID;
    d->committed.version = 1;
    d->committed.size = sizeof(d->committed);
    d->committed.generation = battle;
    d->clock = clock;
    return SC_AI_OK;
}

int sc_ai_decision_prepare(ScAiDecision *d, unsigned owner, unsigned domain,
                          const u8 mapping[6], unsigned count,
                          ScAiDecisionToken *token, ScAiSelection *choice) {
    int status, restored;
    unsigned i, seen = 0;
    if (!d || !token || !choice) return SC_AI_INVALID;
    clear_bytes(token, sizeof(*token));
    clear_bytes(choice, sizeof(*choice));
    choice->kind = SC_AI_REFUSED;
    choice->status = SC_AI_INVALID;
    if (d->phase == SC_AI_DECISION_FAILED) return SC_AI_RESTORE_FAILED;
    if (d->phase != SC_AI_DECISION_IDLE || d->committed.lock_owner || workspace_owner)
        return SC_AI_NESTED;
    if (!d->committed.generation || d->committed.error || owner > 1 ||
        !mapping || !count || count > 6 || d->operation == ~0U) return SC_AI_INVALID;
    clear_bytes(d->views, sizeof(d->views));
    if (!capture_view(&d->views[0], 0) || !capture_view(&d->views[1], 1) ||
        !initial_route(d, owner, domain)) return SC_AI_INVALID;
    for (i = 0; i < d->views[owner].count; i++) {
        if (mapping[i] >= count || (seen & (1U << mapping[i]))) return SC_AI_INVALID;
        seen |= 1U << mapping[i];
    }
    clear_bytes(&d->original_enemy, sizeof(d->original_enemy));
    if (sc_ai_bank_capture(&d->original_enemy, 1) != SC_AI_OK) return SC_AI_INVALID;
    workspace_owner = d;
    d->committed.lock_owner = owner + 1;
    copy_bytes(&d->staged, &d->committed, sizeof(d->staged));
    d->staged.lock_owner = 0;
    status = sc_ai_select(&d->staged, owner, mapping, count, &d->choice);
    if (d->staged.error == SC_AI_RESTORE_FAILED || d->staged.lock_owner)
        return fail_stop(d);
    restored = sc_ai_bank_restore(&d->original_enemy);
    if (restored != SC_AI_OK) return fail_stop(d);
    if (status != SC_AI_OK || d->choice.kind == SC_AI_NATIVE_FINISH ||
        (domain == SC_AI_D9EC && d->choice.kind != SC_AI_SWITCH)) {
        d->committed.lock_owner = 0;
        workspace_owner = NULL;
        return status == SC_AI_OK ? SC_AI_INVALID : status;
    }
    d->token.battle = d->committed.generation;
    d->token.operation = ++d->operation;
    d->token.owner = owner;
    d->token.domain = domain;
    d->token.party_count = count;
    copy_bytes(d->token.mapping, mapping, 6);
    d->phase = SC_AI_DECISION_PENDING;
    copy_bytes(token, &d->token, sizeof(*token));
    copy_bytes(choice, &d->choice, sizeof(*choice));
    return SC_AI_OK;
}

static void retire(ScAiDecision *d) {
    d->committed.lock_owner = 0;
    d->phase = SC_AI_DECISION_IDLE;
    clear_bytes(&d->token, sizeof(d->token));
    clear_bytes(&d->choice, sizeof(d->choice));
    clear_bytes(&d->staged, sizeof(d->staged));
    workspace_owner = NULL;
}

static int workspace_matches(ScAiDecision *d) {
    clear_bytes(&d->observed_enemy, sizeof(d->observed_enemy));
    return workspace_owner == d &&
        d->committed.lock_owner == d->token.owner + 1 &&
        sc_ai_bank_capture(&d->observed_enemy, 1) == SC_AI_OK &&
        same_bytes(&d->observed_enemy, &d->original_enemy, sizeof(d->original_enemy));
}

int sc_ai_decision_accept(ScAiDecision *d, const ScAiDecisionToken *token, unsigned route) {
    unk_D_84390010_654 *actor;
    unsigned owner;
    if (!expected(d, token)) return SC_AI_INVALID;
    if (d->phase == SC_AI_DECISION_FAILED) return SC_AI_RESTORE_FAILED;
    if (d->phase != SC_AI_DECISION_PENDING || !view_matches(&d->views[0], 0, 0) ||
        !view_matches(&d->views[1], 1, 0) ||
        !workspace_matches(d) ||
        (d->choice.kind != SC_AI_STRUGGLE && !sc_ai_bank_context_matches(&d->staged)) ||
        !acceptance_route(d, route))
        return SC_AI_INVALID;
    owner = token->owner;
    actor = &D_84390010[owner]->unk_654;
    if (owner == 1 && d->choice.kind != SC_AI_STRUGGLE &&
        sc_ai_bank_restore(&d->staged.saved_enemy) != SC_AI_OK) return fail_stop(d);
    if (owner == 0 && d->choice.kind != SC_AI_STRUGGLE)
        copy_bytes(&d->committed.player, &d->staged.player, sizeof(d->committed.player));
    if (d->choice.kind != SC_AI_STRUGGLE) {
        copy_bytes(d->committed.saved_aliases, d->staged.saved_aliases,
                   sizeof(d->committed.saved_aliases));
        d->committed.policy_crc = d->staged.policy_crc;
    }
    /* No callback/wait between promotion and these choice-bearing stores. */
    if (d->choice.kind == SC_AI_SWITCH) {
        actor->unk_1C = 0;
        actor->unk_18 = d->choice.native_slot;
        actor->unk_1E = 0;
        actor->unk_10 = 3;
        actor->unk_04 = 2;
    } else {
        actor->unk_14 = d->choice.move_slot;
        actor->unk_0C = d->choice.move_slot;
        actor->unk_10 = 3;
        actor->unk_04 = 1;
    }
    if (d->choice.kind == SC_AI_STRUGGLE) d->phase = SC_AI_DECISION_STRUGGLE;
    else retire(d);
    return SC_AI_OK;
}

int sc_ai_decision_reject(ScAiDecision *d, const ScAiDecisionToken *token, unsigned route) {
    if (!expected(d, token)) return SC_AI_INVALID;
    if (d->phase == SC_AI_DECISION_FAILED) return SC_AI_RESTORE_FAILED;
    if (d->phase != SC_AI_DECISION_PENDING ||
        (route != SC_AI_DE20 && route != SC_AI_DCD4 && route != SC_AI_D9EC_STORE) ||
        ((route == SC_AI_D9EC_STORE) != (token->domain == SC_AI_D9EC)))
        return SC_AI_INVALID;
    if (token->domain == SC_AI_DF60 &&
        ((route == SC_AI_DE20) != (d->choice.kind == SC_AI_SWITCH))) return SC_AI_INVALID;
    if (!view_matches(&d->views[0], 0, 0) || !view_matches(&d->views[1], 1, 0) ||
        !workspace_matches(d)) return fail_stop(d);
    if (d->choice.kind != SC_AI_STRUGGLE && !sc_ai_bank_context_matches(&d->staged))
        return fail_stop(d);
    if (sc_ai_bank_restore(&d->original_enemy) != SC_AI_OK) return fail_stop(d);
    retire(d);
    return SC_AI_OK;
}

int sc_ai_decision_struggle(ScAiDecision *d, const ScAiDecisionToken *token) {
    unk_D_84390010_654 *actor;
    if (!expected(d, token)) return SC_AI_INVALID;
    if (d->phase == SC_AI_DECISION_FAILED) return SC_AI_RESTORE_FAILED;
    if (d->phase != SC_AI_DECISION_STRUGGLE || d->choice.kind != SC_AI_STRUGGLE ||
        !view_matches(&d->views[0], 0, 1) || !view_matches(&d->views[1], 1, 1))
        return SC_AI_INVALID;
    if (!workspace_matches(d)) return fail_stop(d);
    actor = &D_84390010[token->owner]->unk_654;
    if (actor->unk_14 != 4 || actor->unk_0C != 4 || actor->unk_10 != 3 ||
        usable(&actor->unk_38)) return SC_AI_INVALID;
    if (actor->unk_38.unk_4C & 0x20) actor->unk_0C = 4;
    else {
        actor->unk_14 = 0;
        actor->unk_0C = 0;
        actor->unk_38.unk_5A = 0xA5;
    }
    actor->unk_1C = 0;
    retire(d);
    return SC_AI_OK;
}

int sc_ai_decision_shutdown(ScAiDecision *d, u32 battle) {
    if (!d || !battle || d->committed.generation != battle ||
        workspace_owner || d->committed.error || d->committed.lock_owner ||
        d->phase != SC_AI_DECISION_IDLE)
        return SC_AI_INVALID;
    clear_bytes(&d->committed, sizeof(d->committed));
    d->clock = NULL;
    return SC_AI_OK;
}
