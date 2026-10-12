#ifndef SC_AI_SELECT_H
#define SC_AI_SELECT_H

#include "sc_ai_bank.h"

enum ScAiChoiceKind {
    SC_AI_REFUSED, SC_AI_MOVE, SC_AI_SWITCH, SC_AI_STRUGGLE, SC_AI_NATIVE_FINISH
};

typedef struct ScAiSelection {
    unsigned kind;
    unsigned native_slot; /* Registered group-local slot for SWITCH only. */
    unsigned compact_slot; /* Canonical party slot for SWITCH only. */
    unsigned move_slot; /* MOVE 0..3 or native STRUGGLE sentinel 4. */
    int status;
} ScAiSelection;

int sc_ai_select(ScAiCapsule *capsule, unsigned owner,
                 const u8 mapping[6], unsigned party_count, ScAiSelection *selection);

#endif
