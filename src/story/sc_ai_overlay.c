#include "sc_ai_overlay.h"

static ScAiDecision decision;

int sc_ai_entry(int command, ScAiRequest *request) {
    if (!request || !request->generation) return SC_AI_INVALID;
    if (command == SC_AI_INIT)
        return sc_ai_decision_init(&decision, request->generation, request->clock);
    if (decision.committed.generation != request->generation) return SC_AI_INVALID;
    if (command == SC_AI_PREPARE)
        return sc_ai_decision_prepare(&decision, request->owner, request->domain,
            request->mapping, request->party_count, &request->token, &request->selection);
    if (command == SC_AI_ACCEPT)
        return sc_ai_decision_accept(&decision, &request->token, request->route);
    if (command == SC_AI_REJECT)
        return sc_ai_decision_reject(&decision, &request->token, request->route);
    if (command == SC_AI_CONSUME_STRUGGLE)
        return sc_ai_decision_struggle(&decision, &request->token);
    if (command == SC_AI_SHUTDOWN)
        return sc_ai_decision_shutdown(&decision, request->generation);
    return SC_AI_INVALID;
}
