#ifndef SC_NATIVE_OWNER_H
#define SC_NATIVE_OWNER_H

#ifdef STORY_NATIVE_UI
#include "sc_native_ui.h"

enum ScNativeEventKind {
    SC_NATIVE_EVENT_NONE, SC_NATIVE_EVENT_OBSERVE, SC_NATIVE_EVENT_LEARNING,
    SC_NATIVE_EVENT_DRAINED, SC_NATIVE_EVENT_FAILED
};
typedef struct ScNativeCampaignEvent {
    ScBattleSnapshot party[6];
    /* generation is the nonzero, strictly increasing event serial within this
       operation, NOT a BATL scope/module/allocator generation. */
    RlU32 operation_id, generation;
    unsigned kind, active_slot, enemy_slot, enemy_hp, outcome;
    int error;
} ScNativeCampaignEvent;

/* Private native control only; never serialize this lease into campaign values. */
typedef struct ScNativeCampaignBacking {
    ScUi *ui;
    ScUiBattleRequest *request;
    ScUiDriver *driver;
    ScNativeUi *native_ui;
    ScNativePak *pak;
    ScNativeCampaignEvent *event;
    const ScCatalog *catalog;
    RlU32 seed, lease_id;
} ScNativeCampaignBacking;

/* All callbacks validate the exact live lease/backing tuple and return SC_OK,
   SC_PENDING or an explicit ScStatus refusal, never a Boolean.
   acquire publishes a complete usable tuple only on SC_OK; otherwise clears it.
   submit copies values only on SC_OK; PENDING/refusal owns NO request/work.
   Activation, exit and release happen outside the Pak input session.
   poll requires event == backing->event. SC_OK delivers each event once, in
   strictly increasing serial order; NONE/PENDING/refusal zero the full payload.
   LEARNING contains a held KO observation, already paused before presentation.
   After observe it must actually require learning; resume is allowed only after
   presentation/settlement to SC_BATTLE with no learn_mask. Resume validates the
   same operation/held serial, party order and progression, copies values (never
   retains pointers), and consumes the hold once on SC_OK. PENDING retains it.
   DRAINED follows all final observations and settled learning, exactly once,
   after workers/service/permit retire and BATL scope pops. Dispatch finish once;
   save-error retries must not replay that event, battle or reward.
   release PENDING retains the entire lease/backing; SC_OK retires it once, only
   after graphics resources and submitted tasks have already retired. */
typedef struct ScNativeCampaignOwner {
    void *context;
    int (*acquire)(void *context, ScNativeCampaignBacking *backing);
    int (*validate)(void *context, const ScNativeCampaignBacking *backing);
    int (*submit)(void *context, const ScNativeCampaignBacking *backing,
                  const ScUiBattleRequest *request);
    int (*poll)(void *context, const ScNativeCampaignBacking *backing,
                ScNativeCampaignEvent *event);
    int (*resume)(void *context, const ScNativeCampaignBacking *backing,
                  RlU32 operation_id, RlU32 generation, const ScState *settled,
                  const ScCombat *progression);
    int (*release)(void *context, const ScNativeCampaignBacking *backing);
} ScNativeCampaignOwner;
#endif

#endif
