#ifndef SC_AI_OVERLAY_H
#define SC_AI_OVERLAY_H

#include "sc_ai_decision.h"

enum ScAiCommand { SC_AI_INIT = 1, SC_AI_SELECT, SC_AI_SHUTDOWN,
                   SC_AI_PREPARE, SC_AI_ACCEPT, SC_AI_REJECT, SC_AI_CONSUME_STRUGGLE };

typedef struct ScAiRequest {
    u32 generation;
    unsigned owner, party_count;
    u8 mapping[6];
    ScAiSelection selection;
    unsigned domain, route;
    ScAiDecisionToken token;
    const f32 *clock;
} ScAiRequest;

int sc_ai_entry(int command, ScAiRequest *request);

#endif
