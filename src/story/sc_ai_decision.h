#ifndef SC_AI_DECISION_H
#define SC_AI_DECISION_H

#include "sc_ai_select.h"

enum ScAiDomain { SC_AI_DF60 = 1, SC_AI_D9EC };
enum ScAiRoute { SC_AI_DE20 = 1, SC_AI_DCD4, SC_AI_D9EC_STORE, SC_AI_EB1C };
enum ScAiPendingPhase { SC_AI_DECISION_IDLE, SC_AI_DECISION_PENDING,
                         SC_AI_DECISION_STRUGGLE, SC_AI_DECISION_FAILED };

typedef struct ScAiDecisionToken {
    u32 battle, operation, owner, domain, party_count;
    u8 mapping[6];
} ScAiDecisionToken;

typedef struct ScAiDecisionView {
    unk_D_84390010 *side;
    unk_D_800AE540_1194 *registration;
    unk_D_800AE540_0004 *group;
    void *metadata;
    u32 active, count, state;
    u8 species[6];
    u16 hp[6];
    unk_D_800FCB18 live;
} ScAiDecisionView;

typedef struct ScAiDecision {
    ScAiCapsule committed, staged;
    ScAiBank original_enemy;
    ScAiBank observed_enemy;
    ScAiDecisionToken token;
    ScAiSelection choice;
    ScAiDecisionView views[2];
    const f32 *clock;
    u32 operation, phase;
} ScAiDecision;

int sc_ai_decision_init(ScAiDecision *decision, u32 battle, const f32 *clock);
int sc_ai_decision_prepare(ScAiDecision *decision, unsigned owner,
                          unsigned domain, const u8 mapping[6], unsigned count,
                          ScAiDecisionToken *token, ScAiSelection *choice);
int sc_ai_decision_accept(ScAiDecision *decision, const ScAiDecisionToken *token,
                         unsigned route);
int sc_ai_decision_reject(ScAiDecision *decision, const ScAiDecisionToken *token,
                         unsigned route);
int sc_ai_decision_struggle(ScAiDecision *decision, const ScAiDecisionToken *token);
int sc_ai_decision_shutdown(ScAiDecision *decision, u32 battle);

#endif
