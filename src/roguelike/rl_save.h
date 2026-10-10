#ifndef RL_SAVE_H
#define RL_SAVE_H

#include "rl_save_format.h"

#define RL_STORAGE_SLOT_SIZE 1280
#define RL_STORAGE_NOTE_SIZE (2 * RL_STORAGE_SLOT_SIZE)
#define RL_STORAGE_HEADER_SIZE 32

typedef enum RlSaveStatus {
    RL_SAVE_OK = 0,
    RL_SAVE_NOT_FOUND,
    RL_SAVE_NO_DEVICE,
    RL_SAVE_UNSUPPORTED,
    RL_SAVE_DEVICE_CHANGED,
    RL_SAVE_STORAGE_FULL,
    RL_SAVE_IO,
    RL_SAVE_BAD_CRC,
    RL_SAVE_BAD_VERSION,
    RL_SAVE_BAD_DATA,
    RL_SAVE_INVALID,
    RL_SAVE_RECOVERED
} RlSaveStatus;

typedef struct RlSaveDevice {
    void *context;
    RlSaveStatus (*open)(void *context, int create);
    RlSaveStatus (*read)(void *context, unsigned offset, RlU8 *data, unsigned size);
    RlSaveStatus (*write)(void *context, unsigned offset, const RlU8 *data, unsigned size);
} RlSaveDevice;

/* Outputs are unchanged on errors. RECOVERED publishes the surviving record. */
RlSaveStatus rl_storage_load(const RlSaveDevice *device, RlMeta *meta,
                             RunState *run, RlU8 *has_run);
/* create must be explicitly requested; never replaces an invalid committed record. */
RlSaveStatus rl_storage_save(const RlSaveDevice *device, const RlMeta *meta,
                             const RunState *run, int create);
const char *rl_save_status_text(RlSaveStatus status);

/* Native synchronous API: controller is zero-based; call between input reads. */
RlSaveStatus rl_save_load(unsigned controller, RlMeta *meta,
                          RunState *run, RlU8 *has_run);
RlSaveStatus rl_save_store(unsigned controller, const RlMeta *meta,
                           const RunState *run, int create);

#endif
