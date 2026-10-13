#ifndef STORY_CAMPAIGN_STUBS_H
#define STORY_CAMPAIGN_STUBS_H
#include "story_native_stubs.h"
#include "src/story/sc_native_memory.h"
#define SC_NATIVE_LIFETIME_H
/* Injected lifetime view for installer/adapter ordering tests (every lifetime
   call is a recorded stub), not a target ABI fixture. */
typedef struct ScNativeLifetime {
    ScNativeMemory memory;
    ScNativeLoop loop;
    ScNativeDisplay display;
    struct { int backing_retired; } retirement;
    int live;
} ScNativeLifetime;
int sc_native_lifetime_acquire(ScNativeLifetime *, ScMaArena *, const ScCatalog *,
                               RlU32, ScNativeCampaignBacking *);
int sc_native_lifetime_validate(void *, const ScNativeCampaignBacking *);
int sc_native_lifetime_retire(void *, const ScNativeCampaignBacking *,
                              const unk_D_80068BB0 *);
int sc_native_lifetime_release(void *, const ScNativeCampaignBacking *);
#endif
