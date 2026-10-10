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

enum RlMetaOption {
    RL_META_MOVESET_1 = 0, RL_META_MOVESET_2, RL_META_MOVESET_3,
    RL_META_MOVESET_4,
    RL_META_THEME_1, RL_META_THEME_2, RL_META_THEME_3, RL_META_THEME_4,
    RL_META_KIT_1, RL_META_KIT_2, RL_META_KIT_3,
    RL_META_EVENT_PACK_1, RL_META_EVENT_PACK_2, RL_META_EVENT_PACK_3,
    RL_META_CONTRACT_1, RL_META_CONTRACT_2, RL_META_CONTRACT_3,
    RL_META_CONTRACT_4,
    RL_META_COSMETIC_1, RL_META_COSMETIC_2, RL_META_COSMETIC_3,
    RL_META_COSMETIC_4,
    RL_META_OPTION_COUNT
};

enum RlMetaEconomy {
    RL_META_WIN_ENCOUNTER_COINS = 10,
    RL_META_LOSS_ENCOUNTER_COINS = 5,
    RL_META_CLEAR_BONUS_COINS = 25
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
int rl_meta_species_unlocked(const RlMeta *meta, unsigned species);
int rl_meta_option_unlocked(const RlMeta *meta, unsigned option);
unsigned rl_meta_option_cost(unsigned option);
int rl_meta_purchase_option(RlMeta *meta, unsigned option);

/*
 * Selects one species from a caller-supplied legal-loadout catalog. Higher BST
 * species receive lower integer weight. Repeated entries of the same species
 * are counted once; inconsistent BST values for one species are invalid.
 */
int rl_meta_choose_species(const RlMeta *meta, RlU32 *rng,
                          const RlU8 *species, const RlU16 *bst,
                          unsigned count, RlU8 *selected);

/*
 * Registers or skips one species preserved from the latest terminal team.
 * A repeated same-run/same-species registration is idempotent.
 */
int rl_meta_register_species(RlMeta *meta, unsigned species);
int rl_meta_skip_species_registration(RlMeta *meta);

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
