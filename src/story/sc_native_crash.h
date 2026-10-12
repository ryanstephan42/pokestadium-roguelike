#ifndef SC_NATIVE_CRASH_H
#define SC_NATIVE_CRASH_H
#ifdef STORY_NATIVE_UI
#include "src/dp_intro.h"
#include "sc_core.h"

typedef struct ScNativeCrash {
    ScDpSnapshot snapshot;
} ScNativeCrash;

/* External retained control. validate must authenticate admitted fallback,
   retired descriptor/work references, native mode and live owner pins.
   It runs with interrupts disabled: no allocation, blocking or submission.
   This restores only the startup-reserved crash buffer, not VI or lease rights. */
int sc_native_crash_restore(ScNativeCrash *, const ScDpToken *, void *,
                            int (*validate)(void *, const ScDpSnapshot *));
#endif
#endif
