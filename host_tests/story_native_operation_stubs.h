#ifndef STORY_NATIVE_OPERATION_STUBS_H
#define STORY_NATIVE_OPERATION_STUBS_H
#include "story_native_stubs.h"
#include "src/story/sc_native_memory.h"
#define SC_NATIVE_LIFETIME_H
/* Injected lifetime view for operation policy tests, not a target ABI fixture. */
typedef struct ScNativeLifetime {
    ScNativeMemory memory;
    ScNativeLoop loop;
    struct { int backing_retired; } retirement;
    int live;
} ScNativeLifetime;
int sc_native_lifetime_validate(void *, const ScNativeCampaignBacking *);
#endif
