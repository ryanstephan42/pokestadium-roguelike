#ifndef HOST_STORY_REAL_AI_IMAGE_H
#define HOST_STORY_REAL_AI_IMAGE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct { uint32_t generation; } AiOwner;
typedef struct { uint32_t generation, image_base, native_base, entry; } AiPublished;
typedef uint8_t *(*AiMap)(void *, uint32_t, uint32_t);
typedef bool (*AiSync)(void *, uint8_t *, uint32_t, uint32_t);
/* Trusted bootstrap captures canonical cart once. No arbitrary format admission. */
bool ai_authority_init(const void *canonical_cart, uint32_t installed_bytes,
                       AiMap trusted_map, void *map_context);
bool ai_owner_allocate(uint32_t image_base, uint32_t native_base, AiOwner *owner);
bool ai_owner_bind(AiOwner owner, const void *cart, uint32_t rom_start,
                   uint32_t rom_end, uint32_t battle_generation);
bool ai_owner_revoke(AiOwner owner);
bool ai_owner_retire(AiOwner owner, const AiPublished *publication);
bool ai_owner_admit(AiOwner owner, const void *cart, uint32_t battle_generation,
                    uint32_t rom_start, uint32_t rom_end);
bool ai_owner_span(AiOwner owner, uint32_t physical_base, uint32_t owner_end,
                   uint32_t bytes, uint32_t operation);
bool ai_image_validate(AiOwner owner, const void *cart, uint32_t battle_generation,
                       const uint8_t *image, size_t bytes);
bool ai_image_relocate(AiOwner owner, const void *cart, uint32_t battle_generation,
                       const uint8_t *image, size_t bytes, uint8_t *private_copy,
                       size_t capacity);
/* Cache callback synchronizes only; registry publishes after success/recheck. */
bool ai_image_publish(AiOwner owner, const void *cart, uint32_t battle_generation,
                      AiSync sync, void *context,
                      AiPublished *published);
#endif
