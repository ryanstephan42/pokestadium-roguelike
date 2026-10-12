#ifndef SC_SERVICE_NATIVE_CALLS_H
#define SC_SERVICE_NATIVE_CALLS_H
#include "sc_battle_service.h"
#include "sc_runtime_probe.h"

static int sc_service_native_prepare(unk_D_84390010 *side, unsigned domain) {
    ScAiSelection choice;
    u8 mapping[6];
    unsigned i, count;
    extern RlU8 gScProbeSixMapping[6];
    sc_service_require(side == D_84390010[0] || side == D_84390010[1]);
    count = side->unk_724->unk_002;
    for (i = 0; i < 6; ++i)
        mapping[i] = side == D_84390010[0] ? gScProbeSixMapping[i] : i;
    sc_service_require(sc_service_prepare(side, domain, mapping, count, &choice));
    if (choice.kind == SC_AI_SWITCH) return choice.native_slot + 4;
    if (choice.kind == SC_AI_MOVE) return choice.move_slot;
    if (choice.kind == SC_AI_STRUGGLE) return -1;
    sc_service_require(0);
    return -1;
}

static void sc_service_native_accept(unk_D_84390010 *side, unsigned route) {
    if (sc_service_accept(side, route)) return;
    /* Safe reject is attempted exactly once; uncertain rollback retains. */
    sc_service_require(sc_service_reject(side, route));
    sc_service_require(0);
}
#endif
