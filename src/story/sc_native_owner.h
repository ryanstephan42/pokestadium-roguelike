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

/* All callbacks validate the exact live lease/backing tuple. Submission copies
   values only. Activation, exit and release happen outside the Pak input session.
   DRAINED means workers/service/permit retired and BATL scope already popped.
   A learning event is held before presentation; resume copies settled values.
   Return SC_OK, SC_PENDING or an explicit ScStatus refusal, never a Boolean.
   poll writes only caller-admitted event backing; NONE has no operation identity. */
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
