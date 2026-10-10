#ifndef RL_SAVE_FORMAT_H
#define RL_SAVE_FORMAT_H

#include "rl_core.h"

#define RL_META_MAX_SIZE 512
#define RL_SAVE_MAX_SIZE 1024
#define RL_SAVE_VERSION 1

typedef struct RlMeta {
    RlU32 unlocked_pools, unlocked_relics, unlocked_events;
    RlU32 runs, wins, best_floor;
    RlU8 ascension, hall_count;
    RlMon hall[RL_PARTY];
} RlMeta;

/* Writes a versioned little-endian record; run may be NULL to omit suspension. */
int rl_save_encode(const RlMeta *meta, const RunState *run, RlU8 *buffer,
                  unsigned capacity, unsigned *written);
int rl_save_decode(RlMeta *meta, RunState *run, RlU8 *has_run,
                  const RlU8 *buffer, unsigned length);
RlU32 rl_save_crc32(const RlU8 *bytes, unsigned length);

#endif
