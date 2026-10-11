#ifndef RL_META_CONTENT_H
#define RL_META_CONTENT_H

#include "rl_meta.h"

enum RlMetaRentalSource {
    RL_META_RENTAL_POKE_CUP,
    RL_META_RENTAL_PRIME_CUP,
    RL_META_RENTAL_PETIT_CUP,
    RL_META_RENTAL_PRIME_CUP_R2,
    RL_META_RENTAL_SOURCE_COUNT
};

enum RlMetaTheme {
    RL_META_THEME_FOREST,
    RL_META_THEME_COAST,
    RL_META_THEME_CAVERN,
    RL_META_THEME_SKY,
    RL_META_THEME_COUNT
};

enum RlMetaKit {
    RL_META_KIT_DEFAULT = RL_NO_SLOT,
    RL_META_KIT_POTION = RL_META_KIT_1
};

#define RL_META_STARTING_BUDGET 300
#define RL_META_MAX_CATALOG_RECORDS 4096

typedef struct RlMetaRentalLoadout {
    RlU8 species;
    RlU8 source;
    RlU8 moves[4];
} RlMetaRentalLoadout;

typedef struct RlMetaMoveVariant {
    RlU8 moves[4];
} RlMetaMoveVariant;

typedef struct RlMetaKitDef {
    const char *name;
    RlU8 option;
    RlU16 starting_money;
    RlU16 budget;
    RlU16 bag[RL_ITEM_COUNT];
} RlMetaKitDef;

/*
 * Rental rows must come from the named canonical set. Results contain only
 * moves, never the source rental's level, DVs, stat experience, PP Ups, or item.
 * Returns a count, RL_EMPTY when no distinct variant exists, or an RL_* error.
 */
int rl_meta_list_move_variants(const RlMeta *meta, unsigned option,
                               unsigned species, const RlU8 current_moves[4],
                               const RlMetaRentalLoadout *catalog,
                               unsigned catalog_count,
                               RlMetaMoveVariant *output,
                               unsigned capacity);

/*
 * Intersects a caller-supplied canonical rental species list with the themed
 * set and currently unlocked profile species. Results are unique National Dex
 * IDs and require purchase of the corresponding theme option.
 */
int rl_meta_theme_species(const RlMeta *meta, unsigned theme,
                          const RlU8 *canonical_species, unsigned count,
                          RlU8 *output, unsigned capacity);
const char *rl_meta_theme_name(unsigned theme);
unsigned rl_meta_theme_option(unsigned theme);
const char *rl_meta_option_name(unsigned option);

/* Read-only kit catalog; the default kit is the existing 300-coin start. */
const RlMetaKitDef *rl_meta_kit(unsigned option);

/*
 * Applies a purchased kit only to a fresh, empty draft state. The optional kit
 * replaces part of the existing starting budget; it never adds run value.
 */
int rl_meta_apply_kit(const RlMeta *meta, RunState *run, unsigned option);

#endif
