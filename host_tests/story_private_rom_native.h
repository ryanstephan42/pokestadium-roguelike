#ifndef HOST_STORY_PRIVATE_ROM_NATIVE_H
#define HOST_STORY_PRIVATE_ROM_NATIVE_H

#include <ultra64.h>
#include "story_private_rom_loader.h"

/* Single synchronous caller; context/request/queue cannot be reset or freed. */
void *spr_native_transport(OSPiHandle *cart);
int spr_native_enqueue(void *ctx, const SprRequest *request, const void **expected);
int spr_native_receive(void *ctx, const void **message);
bool spr_native_prepare_cache(void *ctx, const SprRequest *request);
bool spr_native_retire(void *ctx, const SprRequest *request);

#endif
