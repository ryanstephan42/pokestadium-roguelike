#include "sc_ai_select.h"

extern s32 sc_ai_owned_select(s32 owner);

static int valid_side(unsigned owner) {
    unk_D_84390010 *side = D_84390010[owner];
    unk_D_800AE540_0004 *group;
    unsigned slot;
    if (!side || !side->unk_720 || side->unk_720->unk_01 != 1 ||
        side->unk_654.unk_2B || side->unk_654.unk_2C) return 0;
    group = side->unk_720->unk_08[0];
    if (!group || group != side->unk_724 || !group->unk_214 ||
        !group->unk_002 || group->unk_002 > 6 ||
        group->unk_214->unk_002 != group->unk_002 ||
        side->unk_654.unk_08 >= group->unk_002) return 0;
    for (slot = 0; slot < group->unk_002; slot++)
        if (!group->unk_01C[slot].unk_00.unk_00 ||
            group->unk_01C[slot].unk_00.unk_00 > 151) return 0;
    return side->unk_654.unk_38.unk_0B ==
        group->unk_01C[side->unk_654.unk_08].unk_00.unk_00;
}

static int refuse(ScAiCapsule *capsule, ScAiSelection *selection, int status) {
    if (capsule && !capsule->error) capsule->error = status;
    if (selection) {
        selection->kind = SC_AI_REFUSED;
        selection->status = status;
    }
    return status;
}

int sc_ai_select(ScAiCapsule *capsule, unsigned owner,
                 const u8 mapping[6], unsigned party_count, ScAiSelection *selection) {
    unk_D_84390010 *side;
    unk_D_800AE540_0004 *group;
    unk_D_800FCB18 *live;
    unsigned i, count, active, living = 0, seen = 0, usable = 0;
    int status, choice;
    if (!capsule || !selection || owner > 1 || !mapping)
        return refuse(capsule, selection, SC_AI_INVALID);
    if (capsule->error == SC_AI_RESTORE_FAILED)
        return refuse(capsule, selection, SC_AI_RESTORE_FAILED);
    if (capsule->lock_owner)
        return refuse(capsule, selection, SC_AI_NESTED);
    if (capsule->error || capsule->version != 1 ||
        capsule->size != sizeof(*capsule) || !capsule->generation ||
        !valid_side(owner) || !valid_side(1 - owner))
        return refuse(capsule, selection, SC_AI_INVALID);
    selection->native_slot = selection->compact_slot = selection->move_slot = ~0U;
    side = D_84390010[owner];
    group = side->unk_720->unk_08[0];
    if (!party_count || party_count > 6)
        return refuse(capsule, selection, SC_AI_INVALID);
    count = group->unk_002;
    active = side->unk_654.unk_08;
    if (active >= count) return refuse(capsule, selection, SC_AI_INVALID);
    for (i = 0; i < count; i++) {
        if (mapping[i] >= party_count || (seen & (1U << mapping[i])))
            return refuse(capsule, selection, SC_AI_INVALID);
        seen |= 1U << mapping[i];
        if (i != active && group->unk_01C[i].unk_02) living++;
    }
    live = &side->unk_654.unk_38;
    if (live->unk_0B != group->unk_01C[active].unk_00.unk_00)
        return refuse(capsule, selection, SC_AI_INVALID);
    if (!live->unk_0C && !living) {
        if (side->unk_654.unk_2D != 0x12 && side->unk_654.unk_2D != 0x13)
            return refuse(capsule, selection, SC_AI_INVALID);
        selection->kind = SC_AI_NATIVE_FINISH;
        selection->status = SC_AI_OK;
        return SC_AI_OK;
    }
    for (i = 0; i < 4; i++)
        if (live->unk_1F[i] && (live->unk_32[i] & 0x3F) &&
            (!live->unk_52 || (live->unk_52 >> 4) != i))
            usable++;
    if (live->unk_0C && !usable) {
        selection->kind = SC_AI_STRUGGLE;
        selection->move_slot = 4;
        selection->status = SC_AI_OK;
        return SC_AI_OK;
    }
    status = sc_ai_transaction_begin(capsule, owner);
    if (status != SC_AI_OK) return refuse(capsule, selection, status);
    if (owner == 0 && !capsule->player.valid) {
        status = sc_ai_player_init(capsule);
        if (status != SC_AI_OK) {
            int ended = sc_ai_transaction_end(capsule, owner, 0);
            if (ended != SC_AI_OK) status = ended;
            return refuse(capsule, selection, status);
        }
    }
    choice = owner ? func_84383B5C(1) : sc_ai_owned_select(0);
    if (choice >= 4 && choice < (int)count + 4 && (unsigned)(choice - 4) != active &&
        group->unk_01C[choice - 4].unk_02) {
        selection->kind = SC_AI_SWITCH;
        selection->native_slot = choice - 4;
        selection->compact_slot = mapping[choice - 4];
    } else if (live->unk_0C && choice >= 0 && choice < 4 &&
               live->unk_1F[choice] && (live->unk_32[choice] & 0x3F) &&
               (!live->unk_52 || (live->unk_52 >> 4) != (unsigned)choice)) {
        selection->kind = SC_AI_MOVE;
        selection->move_slot = choice;
    } else {
        status = sc_ai_transaction_end(capsule, owner, 0);
        return refuse(capsule, selection,
                      status == SC_AI_OK ? SC_AI_INVALID : status);
    }
    status = sc_ai_transaction_end(capsule, owner, 1);
    if (status != SC_AI_OK) return refuse(capsule, selection, status);
    selection->status = SC_AI_OK;
    return SC_AI_OK;
}
