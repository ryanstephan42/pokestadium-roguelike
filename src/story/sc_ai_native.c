/* Included only in the prototype's native battle fragment. */
#ifdef STORY_AI_PROTOTYPE
#include "sc_ai_runtime.h"
#include "sc_ai_lease.h"
#include "sc_runtime_probe.h"

int sc_ai_native_choose(unk_D_84390010 *side, int forced) {
    ScAiRequest request;
    unsigned i, token = sc_ai_battle_token();
    extern RlU8 gScProbeSixMapping[6];
    bzero(&request, sizeof(request));
    request.generation = token;
    if (side == D_84390010[0]) {
        request.owner = 0;
        request.party_count = 6;
        for (i = 0; i < 6; i++) request.mapping[i] = gScProbeSixMapping[i];
    } else if (side == D_84390010[1]) {
        request.owner = 1;
        request.party_count = 2;
        for (i = 0; i < 6; i++) request.mapping[i] = i;
    } else sc_ai_fatal(16);
    gScAiFatal.side = request.owner;
    gScAiFatal.forced = forced;
    if (sc_ai_dispatch(SC_AI_LEASE_AI, token, SC_AI_SELECT, &request))
        sc_ai_fatal(17);
    if (request.selection.kind == SC_AI_SWITCH)
        return request.selection.native_slot + 4;
    if (!forced && request.selection.kind == SC_AI_MOVE)
        return request.selection.move_slot;
    if (!forced && request.selection.kind == SC_AI_STRUGGLE)
        return 0; /* Native D8C4/EB1C retain slot4 and the Struggle special path. */
    sc_ai_fatal(18);
    return -1;
}

void sc_ai_native_validate(unk_D_84390010 *side, int choice, int forced) {
    unk_D_800AE540_0004 *group;
    if (!side || !side->unk_720 || side->unk_720->unk_01 != 1 ||
        side->unk_654.unk_2B || side->unk_654.unk_2C)
        sc_ai_fatal(19);
    group = side->unk_720->unk_08[0];
    gScAiFatal.choice = choice;
    if (!group || side->unk_724 != group || !group->unk_214 ||
        !group->unk_002 || group->unk_002 > 6 ||
        group->unk_214->unk_002 != group->unk_002 ||
        side->unk_654.unk_08 >= group->unk_002) sc_ai_fatal(20);
    if (choice >= 4 && choice < group->unk_002 + 4 &&
        choice - 4 != side->unk_654.unk_08 && group->unk_01C[choice - 4].unk_02)
        return;
    if (!forced && choice >= 0 && choice < 4 && side->unk_654.unk_38.unk_0C)
        return;
    sc_ai_fatal(21);
}
#endif
