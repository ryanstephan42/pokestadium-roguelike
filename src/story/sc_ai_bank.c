#include "sc_ai_bank.h"

extern unk_D_843C5568 D_843C5568[2];
extern unk_D_843C60A4 *D_843C60A4, *D_843C60A8;
extern unk_D_843C60AC *D_843C60AC, *D_843C60B0;
extern unk_D_800FCB18 *D_843C60B4, *D_843C60B8;
extern unk_D_843C60F0_00C *D_843C5564;
extern unk_D_843C60F0 D_843C60F0;
extern s32 D_843C60BC, D_8438AFB0;
extern unk_D_843C60C0 D_843C60C0[2];
extern u8 D_843C60E8, D_843C6144;
extern u16 D_843C613A, D_843C613C, D_843C613E, D_843C6140, D_843C6142;
extern unk_D_843C6148 D_843C6148;

typedef char ScAiBankSize[(sizeof(ScAiBank) == 0xBA0) ? 1 : -1];
typedef char ScAiCapsuleSize[(sizeof(ScAiCapsule) == 0x1760) ? 1 : -1];
#define SC_AI_OFFSET(type, field, offset) \
    typedef char ScAiOffset_##field[((u32)&((type *)0)->field == offset) ? 1 : -1]
SC_AI_OFFSET(ScAiBank, owner, 0xB38);
SC_AI_OFFSET(ScAiBank, prediction, 0xB3C);
SC_AI_OFFSET(ScAiBank, repetition, 0xB40);
SC_AI_OFFSET(ScAiBank, refs, 0xB44);
SC_AI_OFFSET(ScAiBank, choices, 0xB60);
SC_AI_OFFSET(ScAiBank, damage_half, 0xB88);
SC_AI_OFFSET(ScAiBank, damage_byte, 0xB92);
SC_AI_OFFSET(ScAiBank, choice_generation, 0xB93);
SC_AI_OFFSET(ScAiBank, damage_scratch, 0xB94);
SC_AI_OFFSET(ScAiBank, valid, 0xB9C);
SC_AI_OFFSET(ScAiCapsule, player, 0x10);
SC_AI_OFFSET(ScAiCapsule, saved_enemy, 0xBB0);
SC_AI_OFFSET(ScAiCapsule, saved_aliases, 0x1750);
SC_AI_OFFSET(ScAiCapsule, policy_crc, 0x1758);
SC_AI_OFFSET(ScAiCapsule, generation, 0x175C);
#undef SC_AI_OFFSET

static void copy_bytes(void *dst, const void *src, unsigned count) {
    u8 *d = dst;
    const u8 *s = src;
    while (count--) *d++ = *s++;
}

static u32 policy_hash(void) {
    const u8 *bytes = (const void *)&D_843C60F0;
    unsigned i;
    u32 hash = 2166136261U;
    for (i = 0; i < sizeof(D_843C60F0); i++)
        hash = (hash ^ bytes[i]) * 16777619U;
    return hash;
}

static int immutable_context(const ScAiCapsule *capsule) {
    return capsule->saved_aliases[0] == (u32)D_84384588[0] &&
        capsule->saved_aliases[1] == (u32)D_84384588[1] &&
        capsule->policy_crc == policy_hash();
}

int sc_ai_bank_context_matches(const ScAiCapsule *capsule) {
    return capsule && immutable_context(capsule);
}

static u32 ref_offset(const void *pointer, const void *base, unsigned size) {
    u32 offset;
    if (!pointer) return ~0U;
    offset = (u32)pointer - (u32)base;
    return offset < size ? offset : ~0U - 1;
}

static int valid_offsets(unsigned owner, const u32 refs[7],
                         const unk_D_843C5568 contexts[2]) {
    unsigned i;
    u32 offset, record;
    if (owner > 1) return 0;
    for (i = 0; i < 2; i++) {
        if (refs[i] != ((i ? 1 - owner : owner) * 0x59C + 2)) return 0;
    }
    for (i = 2; i < 6; i++) {
        offset = refs[i];
        if (offset == ~0U) continue;
        record = offset - ((i & 1) ? 1 - owner : owner) * 0x59C;
        if (i >= 4) record -= 0x12;
        if (record < 0x16 || (record - 0x16) % 0x76 ||
            (record - 0x16) / 0x76 >= contexts[(i & 1) ? 1 - owner : owner].unk_005)
            return 0;
        if (i >= 4 && (refs[i - 2] == ~0U || refs[i - 2] + 0x12 != offset))
            return 0;
    }
    if ((refs[2] == ~0U) != (refs[4] == ~0U) ||
        (refs[3] == ~0U) != (refs[5] == ~0U)) return 0;
    offset = refs[6];
    return offset == 0xC || offset == 0x20 || offset == 0x34;
}

static int valid_contexts(const unk_D_843C5568 contexts[2]) {
    unsigned side;
    for (side = 0; side < 2; side++) {
        const unk_D_843C5568 *context = &contexts[side];
        if (!context->unk_005 || context->unk_005 > 6 ||
            context->unk_003 + context->unk_004 != context->unk_005 ||
            !context->unk_008 || context->unk_008 > context->unk_005 ||
            context->unk_006 + context->unk_007 != context->unk_008 ||
            context->unk_012 >= context->unk_005) return 0;
    }
    return 1;
}

static int valid_refs(const ScAiBank *bank) {
    return bank && bank->valid && bank->contexts[0].unk_000 == bank->owner &&
        valid_offsets(bank->owner, bank->refs, bank->contexts) &&
        valid_contexts(bank->contexts);
}

int sc_ai_bank_capture(ScAiBank *bank, unsigned owner) {
    const void *refs[7];
    u32 offsets[7];
    unsigned i;
    if (!bank || owner > 1) return SC_AI_INVALID;
    refs[0] = D_843C60A4; refs[1] = D_843C60A8;
    refs[2] = D_843C60AC; refs[3] = D_843C60B0;
    refs[4] = D_843C60B4; refs[5] = D_843C60B8;
    refs[6] = D_843C5564;
    for (i = 0; i < 6; i++)
        offsets[i] = ref_offset(refs[i], D_843C5568, sizeof(D_843C5568));
    offsets[6] = ref_offset(refs[6], &D_843C60F0, 0x48);
    if (D_843C5568[0].unk_000 != owner ||
        !valid_offsets(owner, offsets, D_843C5568) ||
        !valid_contexts(D_843C5568))
        return SC_AI_INVALID;
    copy_bytes(bank->contexts, D_843C5568, sizeof(bank->contexts));
    bank->owner = owner;
    copy_bytes(bank->refs, offsets, sizeof(offsets));
    bank->prediction = D_843C60BC; bank->repetition = D_8438AFB0;
    copy_bytes(bank->choices, D_843C60C0, sizeof(bank->choices));
    bank->damage_half[0] = D_843C613A; bank->damage_half[1] = D_843C613C;
    bank->damage_half[2] = D_843C613E; bank->damage_half[3] = D_843C6140;
    bank->damage_half[4] = D_843C6142;
    bank->damage_byte = D_843C6144;
    bank->choice_generation = D_843C60E8;
    bank->damage_scratch = D_843C6148;
    bank->valid = 1;
    return SC_AI_OK;
}

static void *rebase(u32 offset) {
    return offset == ~0U ? NULL : (u8 *)D_843C5568 + offset;
}

int sc_ai_bank_restore(const ScAiBank *bank) {
    if (!valid_refs(bank)) return SC_AI_INVALID;
    copy_bytes(D_843C5568, bank->contexts, sizeof(bank->contexts));
    D_843C60A4 = rebase(bank->refs[0]); D_843C60A8 = rebase(bank->refs[1]);
    D_843C60AC = rebase(bank->refs[2]); D_843C60B0 = rebase(bank->refs[3]);
    D_843C60B4 = rebase(bank->refs[4]); D_843C60B8 = rebase(bank->refs[5]);
    D_843C5564 = (void *)((u8 *)&D_843C60F0 + bank->refs[6]);
    D_843C60BC = bank->prediction; D_8438AFB0 = bank->repetition;
    copy_bytes(D_843C60C0, bank->choices, sizeof(bank->choices));
    D_843C613A = bank->damage_half[0]; D_843C613C = bank->damage_half[1];
    D_843C613E = bank->damage_half[2]; D_843C6140 = bank->damage_half[3];
    D_843C6142 = bank->damage_half[4]; D_843C6144 = bank->damage_byte;
    D_843C60E8 = bank->choice_generation;
    D_843C6148 = bank->damage_scratch;
    return SC_AI_OK;
}

int sc_ai_transaction_begin(ScAiCapsule *capsule, unsigned owner) {
    int status, restored;
    if (!capsule || owner > 1 || !capsule->generation ||
        capsule->version != 1 || capsule->size != sizeof(*capsule))
        return SC_AI_INVALID;
    if (capsule->error == SC_AI_RESTORE_FAILED) return SC_AI_RESTORE_FAILED;
    if (capsule->lock_owner) {
        if (!capsule->error) capsule->error = SC_AI_NESTED;
        return SC_AI_NESTED;
    }
    if (capsule->error) return SC_AI_INVALID;
    if (!capsule->saved_aliases[0] && !capsule->saved_aliases[1]) {
        if (!D_84384588[0] || !D_84384588[1]) {
            capsule->error = SC_AI_INVALID;
            return SC_AI_INVALID;
        }
        capsule->saved_aliases[0] = (u32)D_84384588[0];
        capsule->saved_aliases[1] = (u32)D_84384588[1];
        capsule->policy_crc = policy_hash();
    }
    if (!immutable_context(capsule)) {
        capsule->error = SC_AI_INVALID;
        return SC_AI_INVALID;
    }
    status = sc_ai_bank_capture(&capsule->saved_enemy, 1);
    if (status != SC_AI_OK) {
        capsule->error = status;
        return status;
    }
    capsule->lock_owner = owner + 1;
    if (owner == 0 && capsule->player.valid) {
        status = sc_ai_bank_restore(&capsule->player);
        if (status != SC_AI_OK) {
            capsule->error = status;
            restored = sc_ai_bank_restore(&capsule->saved_enemy);
            if (restored != SC_AI_OK) {
                capsule->error = SC_AI_RESTORE_FAILED;
                return SC_AI_RESTORE_FAILED;
            }
            capsule->lock_owner = 0;
        }
    }
    return status;
}

int sc_ai_transaction_end(ScAiCapsule *capsule, unsigned owner, int accepted) {
    int status = SC_AI_OK, restored;
    if (!capsule || owner > 1 || capsule->lock_owner != owner + 1)
        return SC_AI_INVALID;
    if (capsule->error == SC_AI_RESTORE_FAILED) return SC_AI_RESTORE_FAILED;
    if (capsule->error || !accepted || !immutable_context(capsule))
        status = SC_AI_INVALID;
    if (owner == 0 && status == SC_AI_OK)
        status = sc_ai_bank_capture(&capsule->player, owner);
    if (owner == 1 && status == SC_AI_OK)
        status = sc_ai_bank_capture(&capsule->saved_enemy, owner);
    restored = sc_ai_bank_restore(&capsule->saved_enemy);
    if (restored != SC_AI_OK) {
        capsule->error = SC_AI_RESTORE_FAILED;
        return SC_AI_RESTORE_FAILED;
    }
    capsule->lock_owner = 0;
    if (status != SC_AI_OK && !capsule->error) capsule->error = status;
    return status;
}

int sc_ai_player_init(ScAiCapsule *capsule) {
    unsigned side, slot;
    extern u8 D_843C5550[0x14];
    if (!capsule || capsule->lock_owner != 1 || capsule->error ||
        !capsule->saved_enemy.valid || capsule->player.valid)
        return SC_AI_INVALID;
    for (side = 0; side < 2; side++) {
        unk_D_800AE540_0004 *group;
        if (!D_84390010[side] || !D_84390010[side]->unk_720 ||
            D_84390010[side]->unk_720->unk_01 != 1)
            return SC_AI_INVALID;
        group = D_84390010[side]->unk_720->unk_08[0];
        if (!group || !group->unk_214 || !group->unk_002 || group->unk_002 > 6 ||
            group->unk_214->unk_002 != group->unk_002)
            return SC_AI_INVALID;
        for (slot = 0; slot < group->unk_002; slot++) {
            unsigned index;
            for (index = 0; index < 18; index++)
                if (D_843C5550[index] == group->unk_01C[slot].unk_00.unk_00) break;
            if (!group->unk_01C[slot].unk_00.unk_00 || index == 18)
                return SC_AI_INVALID;
        }
    }
    {
        u8 *bytes = (void *)D_843C5568;
        for (slot = 0; slot < sizeof(D_843C5568); slot++) bytes[slot] = 0;
    }
    func_84378474(D_84390010[0]->unk_720->unk_08[0], NULL,
                  D_84390010[1]->unk_720->unk_08[0], D_843C5568, 0);
    D_843C60A4 = (void *)&D_843C5568[0].unk_002;
    D_843C60A8 = (void *)&D_843C5568[1].unk_002;
    D_843C5568[0].unk_000 = 0;
    D_843C5568[0].unk_001 = 2;
    D_843C60A4->unk_13 = D_843C60A8->unk_13 = 1;
    D_843C5564 = (void *)((u8 *)&D_843C60F0 + 0x20);
    func_8437E974(D_84390010[0]->unk_720->unk_08[0], NULL, D_843C60A4, 0);
    func_8437E974(D_84390010[1]->unk_720->unk_08[0], NULL, D_843C60A8, 1);
    for (side = 0; side < 2; side++) {
        unk_D_843C60A4 *team = side ? D_843C60A8 : D_843C60A4;
        team->unk_09 = team->unk_06;
        team->unk_07[0] = team->unk_06;
        for (slot = 0; slot < team->unk_06; slot++)
            ((u8 *)team)[0xA + slot] = slot;
    }
    D_843C60AC = D_843C60B0 = NULL;
    D_843C60B4 = D_843C60B8 = NULL;
    D_843C60BC = D_8438AFB0 = 0;
    D_843C60E8 = D_843C6144 = 0;
    D_843C613A = D_843C613C = D_843C613E = D_843C6140 = D_843C6142 = 0;
    {
        u8 *bytes = (void *)D_843C60C0;
        for (slot = 0; slot < sizeof(D_843C60C0); slot++) bytes[slot] = 0;
        bytes = (void *)&D_843C6148;
        for (slot = 0; slot < sizeof(D_843C6148); slot++) bytes[slot] = 0;
    }
    func_84382ACC();
    return SC_AI_OK;
}
