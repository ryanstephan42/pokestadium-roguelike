#ifndef HOST_STORY_PRIVATE_ROM_LOADER_H
#define HOST_STORY_PRIVATE_ROM_LOADER_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    SPR_IDLE, SPR_PREPARING, SPR_REJECTED, SPR_QUEUED, SPR_VALIDATING,
    SPR_FAIL_STOP, SPR_PUBLISHED
} SprState;
typedef enum {
    SPR_NONE, SPR_INPUT, SPR_ENQUEUE, SPR_COMPLETION, SPR_TIMEOUT,
    SPR_DIGEST, SPR_CONTAINER, SPR_IMPORTS, SPR_RELOCATION, SPR_CACHE
} SprError;
typedef struct {
    uint32_t ram, bytes, owner_base, owner_end;
    uint32_t rom_offset, rom_low, rom_end, installed_bytes;
    uint32_t generation, operation;
    bool cold_route, exclusive_staging, unlocked, cart_handle;
} SprRequest;
typedef struct {
    /* -1 means definitive not-enqueued; 0 means queued, not completed. */
    int (*enqueue)(void *ctx, const SprRequest *request, const void **expected);
    /* 0 empty, 1 message, -1 unknown/error. Only manager posts. */
    int (*receive)(void *ctx, const void **message);
    bool (*prepare_cache)(void *ctx, const SprRequest *request);
    bool (*digest)(void *ctx, const SprRequest *request);
    bool (*container)(void *ctx, const SprRequest *request);
    bool (*imports)(void *ctx, const SprRequest *request);
    bool (*relocate)(void *ctx, const SprRequest *request);
    bool (*publish_cache)(void *ctx, const SprRequest *request);
    void (*refusal)(void *ctx, SprError error, bool retained);
} SprOps;
typedef struct {
    SprState state;
    SprError error;
    SprRequest request;
    SprOps ops;
    void *ctx;
    const void *expected;
    bool retained;
} SprLoader;

/* Caller-owned zero-initialized persistent object; no retry/reset interface. */
bool spr_preflight(const SprRequest *request);
bool spr_start(SprLoader *loader, const SprRequest *request,
               const SprOps *ops, void *ctx);
SprState spr_poll(SprLoader *loader, bool deadline_expired);
bool spr_retire(SprLoader *loader, uint32_t generation, uint32_t operation);

#endif
