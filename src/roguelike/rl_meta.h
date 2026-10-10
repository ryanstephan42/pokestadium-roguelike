#ifndef RL_META_H
#define RL_META_H

#include "rl_save_format.h"

enum RlMetaPool {
    RL_META_POOL_STARTERS = 0,
    RL_META_POOL_PRIME_CUP = 1,
    RL_META_POOL_LITTLE_CUP = 2,
    RL_META_POOL_COUNT = 3
};

enum RlMetaUnlock {
    RL_META_INITIAL_RELICS = 8,
    RL_META_INITIAL_EVENTS = 8
};

/* Initializes a new profile with the base starter pool and launch content. */
void rl_meta_init(RlMeta *meta);

/* Valid catalog bits only; suitable for intersecting with content offers. */
RlU32 rl_meta_permitted_pools(const RlMeta *meta);
RlU32 rl_meta_permitted_relics(const RlMeta *meta);
RlU32 rl_meta_permitted_events(const RlMeta *meta);

/* Unlock queries return zero for invalid arguments or a locked item. */
int rl_meta_starter_unlocked(const RlMeta *meta, unsigned pool);
int rl_meta_relic_unlocked(const RlMeta *meta, unsigned relic);
int rl_meta_event_unlocked(const RlMeta *meta, unsigned event);

/*
 * Uniformly selects from the unlocked event set using only the caller's RNG.
 * Returns RL_EMPTY when no event is unlocked; output/state stay unchanged then.
 */
int rl_meta_choose_event(const RlMeta *meta, RlU32 *rng, RlU8 *event);

/*
 * Selects an unlocked ascension without changing meta->ascension, which stores
 * the highest unlocked level. The output is unchanged on failure.
 */
int rl_meta_select_ascension(const RlMeta *meta, unsigned requested,
                             RlU8 *selected);

/*
 * Records only terminal runs. The caller must invoke this exactly once per
 * completed run; the v3 meta layout has no run identifier for deduplication.
 */
int rl_meta_record_run(RlMeta *meta, const RunState *run);

#endif
