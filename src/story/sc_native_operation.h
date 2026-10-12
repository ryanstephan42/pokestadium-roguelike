#ifndef SC_NATIVE_OPERATION_H
#define SC_NATIVE_OPERATION_H
#ifdef STORY_NATIVE_UI
#include "sc_native_lifetime.h"

enum ScNativeOperationPhase {
    SC_NATIVE_OPERATION_IDLE, SC_NATIVE_OPERATION_RUNNING,
    SC_NATIVE_OPERATION_HELD, SC_NATIVE_OPERATION_FAILED,
    SC_NATIVE_OPERATION_DRAINING
};

typedef struct ScNativeOperation {
    ScNativeLifetime *lifetime;
    ScBridge bridge;
    ScNativeCampaignEvent pending;
    RlU32 operation, last_operation, serial, held_serial;
    unsigned phase, queued;
} ScNativeOperation;

/* Caller-retained control; values live in the admitted DATA storage.
   These functions do not launch a battle. drain queues the single terminal
   result; its caller must already hold the retirement authority (workers,
   service and permit retired, BATL scope popped). */
int sc_native_operation_bind(ScNativeOperation *, ScNativeLifetime *);
int sc_native_operation_submit(ScNativeOperation *, const ScNativeCampaignBacking *,
                                const ScUiBattleRequest *);
int sc_native_operation_observe(ScNativeOperation *, const ScNativeCampaignBacking *,
                                 const ScBattleSnapshot [6], unsigned, unsigned, unsigned);
int sc_native_operation_poll(ScNativeOperation *, const ScNativeCampaignBacking *,
                              ScNativeCampaignEvent *);
int sc_native_operation_resume(ScNativeOperation *, const ScNativeCampaignBacking *,
                                RlU32, RlU32, const ScState *, const ScCombat *);
int sc_native_operation_drain(ScNativeOperation *, const ScNativeCampaignBacking *, unsigned);
#endif
#endif
