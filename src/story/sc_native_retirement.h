#ifndef SC_NATIVE_RETIREMENT_H
#define SC_NATIVE_RETIREMENT_H
#ifdef STORY_NATIVE_UI
#include "sc_native_owner.h"
#include "src/dp_intro.h"
typedef struct ScNativeRetirement {
    ScDpToken token;
    ScNativeFramebufferSpan spans[2];
    const unk_D_80068BB0 *images;
    const unk_D_80068BB0 *fallback;
    void *context;
    int (*validate)(void *context, const struct ScNativeRetirement *retirement);
    ScDpSnapshot snapshot;
    int acquired, pending, backing_retired, snapshot_valid, fallback_completed;
    int (*retire_backing)(void *, const struct ScNativeRetirement *);
} ScNativeRetirement;
/* Control must remain outside the freed allocation. Owner supplies zeroed
   retained control and validates admitted identities,
   fallback mapping/mode and pins on every step. snapshot_valid indicates the
   atomic scheduler view is available for native presentation-mode validation.
   No allocator authority here.
   SC_OK step keeps the token held; finalize after all lower cleanup. */
int sc_native_retirement_acquire(ScNativeRetirement *retirement,
    const ScNativeFramebufferSpan spans[2], const unk_D_80068BB0 images[2],
    const unk_D_80068BB0 *fallback, void *context,
    int (*validate)(void *, const ScNativeRetirement *));
int sc_native_retirement_step(ScNativeRetirement *retirement);
/* Refreshes retained inspection only: no enqueue, DONE consume or free. */
int sc_native_retirement_check(ScNativeRetirement *retirement);
int sc_native_retirement_release(ScNativeRetirement *retirement);
/* Owner frees exact pinned backing once while exclusion is still held. A
   post-free token-release refusal retains exclusion; retry never reads backing
   or repeats the callback. release above is for abort without freeing only. */
int sc_native_retirement_finalize(ScNativeRetirement *retirement,
    int (*retire_backing)(void *, const ScNativeRetirement *));
#endif
#endif
