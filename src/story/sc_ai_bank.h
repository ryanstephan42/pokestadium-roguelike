#ifndef SC_AI_BANK_H
#define SC_AI_BANK_H

#include "src/fragments/62/fragment62.h"

enum ScAiStatus { SC_AI_OK, SC_AI_INVALID, SC_AI_NESTED, SC_AI_RESTORE_FAILED };

typedef struct ScAiBank {
    unk_D_843C5568 contexts[2];
    u32 owner;
    s32 prediction, repetition;
    u32 refs[7];
    unk_D_843C60C0 choices[2];
    u16 damage_half[5];
    u8 damage_byte, choice_generation;
    unk_D_843C6148 damage_scratch;
    u32 valid;
} ScAiBank;

typedef struct ScAiCapsule {
    u32 version, size, lock_owner, error;
    ScAiBank player, saved_enemy;
    u32 saved_aliases[2];
    u32 policy_crc, generation;
} ScAiCapsule;

int sc_ai_bank_capture(ScAiBank *bank, unsigned owner);
int sc_ai_bank_restore(const ScAiBank *bank);
int sc_ai_transaction_begin(ScAiCapsule *capsule, unsigned owner);
int sc_ai_transaction_end(ScAiCapsule *capsule, unsigned owner, int accepted);
int sc_ai_player_init(ScAiCapsule *capsule);
int sc_ai_bank_context_matches(const ScAiCapsule *capsule);

#endif
