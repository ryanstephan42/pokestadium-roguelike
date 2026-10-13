#ifndef SC_NATIVE_LIFETIME_H
#define SC_NATIVE_LIFETIME_H
#ifdef STORY_NATIVE_UI
#include "sc_native_memory.h"
#include "sc_native_retirement.h"
#include "sc_native_crash.h"

typedef struct ScNativeLifetime {
    ScNativeMemory memory;
    ScNativeLoop loop;
    ScNativeDisplay display;
    ScNativeRetirement retirement;
    ScNativeCrash crash;
    ScNativeCampaignBacking binding;
    const unk_D_80068BB0 *fallback;
    unk_D_80068BB0 fallback_value;
    int live;
} ScNativeLifetime;

/* Zero once, outside the data lease; preserve arena/DP generations on reentry.
   Battle submit/poll/resume remain separate callbacks and confer no rights here. */
int sc_native_lifetime_acquire(ScNativeLifetime *, ScMaArena *, const ScCatalog *,
                               RlU32, ScNativeCampaignBacking *);
int sc_native_lifetime_validate(void *, const ScNativeCampaignBacking *);
int sc_native_lifetime_retire(void *, const ScNativeCampaignBacking *,
                              const unk_D_80068BB0 *);
int sc_native_lifetime_release(void *, const ScNativeCampaignBacking *);
#endif
#endif
