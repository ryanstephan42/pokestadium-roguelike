#include "story_real_ai_image.h"
#include "story_private_rom_loader.h"
#include "ai_sizes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned assertions;
#define CHECK(x) do { ++assertions; if (!(x)) { \
    fprintf(stderr, "%d: %s\n", __LINE__, #x); exit(1); \
} } while (0)
static uint8_t *image, *copy;
static AiOwner owner;
static int cart, foreign_cart;
static unsigned syncs;
static bool revoked, cache_ok = true;
static uint8_t *registered_backing, *alternate_backing;
static AiPublished published;
static uint8_t *host_map(void *ctx, uint32_t base, uint32_t size) {
    (void)ctx;
    return base == 0x80400000 && size == AI_RAM ? registered_backing : NULL;
}
static bool sync_cache(void *ctx, uint8_t *backing, uint32_t base, uint32_t size) {
    (void)ctx;
    CHECK(base == 0x80400000 && size == AI_RAM);
    CHECK(backing == registered_backing && backing == copy);
    CHECK(published.entry == 0);
    ++syncs;
    if (revoked) CHECK(ai_owner_revoke(owner));
    if (getenv("AI_TEST_MAP_SWAP")) registered_backing = alternate_backing;
    return cache_ok;
}
static uint8_t *read_file(const char *path, size_t size) {
    FILE *f = fopen(path, "rb");
    CHECK(f != NULL);
    uint8_t *p = malloc(size > AI_RAM ? size : AI_RAM);
    CHECK(p != NULL && fread(p, 1, size, f) == size);
    CHECK(fgetc(f) == EOF && fclose(f) == 0);
    return p;
}
static bool validate(void *ctx, const SprRequest *r) {
    (void)ctx;
    return ai_image_validate(owner, &cart, r->generation, image, r->bytes);
}
static bool relocate(void *ctx, const SprRequest *r) {
    (void)ctx;
    return ai_image_relocate(owner, &cart, r->generation, image, r->bytes, copy, AI_RAM);
}
static bool cache(void *ctx, const SprRequest *r) {
    (void)ctx;
    return r->ram == 0x400000 && r->bytes == AI_ROM_BYTES; /* HOST sync simulation. */
}
static bool publish(void *ctx, const SprRequest *r) {
    (void)ctx;
    CHECK(published.entry == 0);
    copy[33] ^= 1;
    CHECK(!ai_image_publish(owner, &cart, r->generation, sync_cache, NULL, &published));
    CHECK(syncs == 0 && published.entry == 0);
    copy[33] ^= 1;
    registered_backing = alternate_backing;
    CHECK(!ai_image_publish(owner, &cart, r->generation, sync_cache, NULL, &published));
    registered_backing = copy;
    return ai_image_publish(owner, &cart, r->generation, sync_cache, NULL, &published);
}
static int message;
static int enqueue(void *ctx, const SprRequest *r, const void **expected) {
    (void)ctx;
    if (!ai_image_validate(owner, &cart, r->generation, image, r->bytes)) return -1;
    *expected = &message;
    return 0; /* Injected device outcome; bytes were read from sealed file. */
}
static int receive(void *ctx, const void **expected) {
    (void)ctx;
    *expected = &message;
    return 1;
}
static void refused(void *ctx, SprError error, bool retained) {
    (void)ctx;
    CHECK(error == SPR_CACHE && retained && published.entry == 0);
}

int main(int argc, char **argv) {
    CHECK(argc == 3);
    image = read_file(argv[1], AI_ROM_BYTES);
    uint8_t *reference = read_file(argv[2], AI_RAM);
    copy = malloc(AI_RAM);
    CHECK(copy != NULL);
    alternate_backing = malloc(AI_RAM);
    CHECK(alternate_backing != NULL);
    if (getenv("AI_TEST_INPLACE")) {
        free(copy);
        copy = image;
    }
    registered_backing = copy;
    CHECK(!ai_authority_init(&cart, 0x400000, host_map, NULL));
    CHECK(ai_authority_init(&cart, 0x800000, host_map, NULL));
    CHECK(!ai_authority_init(&foreign_cart, 0x800000, host_map, NULL));
    CHECK(!ai_owner_allocate(0x80300000, 0x80200000, &owner));
    CHECK(!ai_owner_allocate(0x80400001, 0x80200000, &owner));
    CHECK(!ai_owner_allocate(0xa0400000, 0x80200000, &owner));
    CHECK(!ai_owner_allocate(0x400000, 0x80200000, &owner));
    CHECK(!ai_owner_allocate(0x807da7f0, 0x80200000, &owner));
    CHECK(!ai_owner_allocate(0x80400000, 0x803ffff0, &owner));
    CHECK(ai_owner_allocate(0x80400000, 0x80200000, &owner));
    CHECK(!ai_owner_allocate(0x80400000, 0x80200000, &owner));
    CHECK(!ai_owner_bind(owner, &foreign_cart, AI_ROM_START, AI_ROM_END, 7));
    CHECK(!ai_owner_bind((AiOwner){owner.generation + 1}, &cart, AI_ROM_START, AI_ROM_END, 7));
    CHECK(!ai_owner_bind(owner, &cart, (AI_ROM_START + 2), AI_ROM_END, 7));
    CHECK(!ai_owner_bind(owner, &cart, AI_ROM_START, AI_ROM_END, 0));
    CHECK(ai_owner_bind(owner, &cart, AI_ROM_START, AI_ROM_END, 7));
    CHECK(ai_owner_admit(owner, &cart, 7, AI_ROM_START, AI_ROM_END));
    CHECK(ai_owner_span(owner, 0x400000, 0x400000 + AI_RAM, AI_ROM_BYTES, owner.generation));
    CHECK(!ai_owner_span(owner, 0x400008, 0x400000 + AI_RAM, AI_ROM_BYTES, owner.generation));
    CHECK(!ai_owner_span(owner, 0x400000, 0x400000 + (AI_RAM - 1), AI_ROM_BYTES, owner.generation));
    CHECK(!ai_owner_span(owner, 0x400000, 0x400000 + AI_RAM, AI_ROM_BYTES, owner.generation + 1));
    CHECK(!ai_owner_bind(owner, &cart, AI_ROM_START, AI_ROM_END, 7));
    CHECK(ai_image_validate(owner, &cart, 7, image, AI_ROM_BYTES));
    CHECK(!ai_image_validate(owner, &foreign_cart, 7, image, AI_ROM_BYTES));
    CHECK(!ai_image_validate(owner, &cart, 8, image, AI_ROM_BYTES));
    CHECK(!ai_image_validate((AiOwner){0}, &cart, 7, image, AI_ROM_BYTES));
    for (unsigned n = 0; n < AI_ROM_BYTES; n += 31)
        CHECK(!ai_image_validate(owner, &cart, 7, image, n));
    for (unsigned n = 0; n < 32; ++n) {
        image[n] ^= 1;
        CHECK(!ai_image_validate(owner, &cart, 7, image, AI_ROM_BYTES));
        image[n] ^= 1;
    }
    const unsigned corrupt[] = {6324, 13748, AI_PAYLOAD, (AI_PAYLOAD + 4), (AI_PAYLOAD + 7), (AI_ROM_BYTES - 13), (AI_ROM_BYTES - 1)};
    for (unsigned i = 0; i < sizeof(corrupt) / sizeof(corrupt[0]); ++i) {
        image[corrupt[i]] ^= 128;
        if (copy != image) memset(copy, 0xcc, AI_RAM);
        CHECK(!ai_image_relocate(owner, &cart, 7, image, AI_ROM_BYTES, copy, AI_RAM));
        if (copy != image) CHECK(copy[0] == 0xcc && copy[(AI_RAM - 1)] == 0xcc);
        image[corrupt[i]] ^= 128;
    }
    CHECK(!ai_image_relocate(owner, &cart, 7, image, AI_ROM_BYTES, image + 4, AI_RAM));
    CHECK(!ai_image_relocate(owner, &cart, 7, image, AI_ROM_BYTES, copy, (AI_RAM - 1)));
    CHECK(!ai_image_relocate(owner, &cart, 7, image, AI_ROM_BYTES, alternate_backing, AI_RAM));
    CHECK(!ai_image_relocate((AiOwner){owner.generation + 1}, &cart, 7,
                            image, AI_ROM_BYTES, copy, AI_RAM));
    CHECK(!ai_image_relocate(owner, &cart, 7, image, AI_ROM_BYTES, copy + 16, AI_RAM));
    registered_backing = alternate_backing;
    CHECK(!ai_image_relocate(owner, &cart, 7, image, AI_ROM_BYTES, copy, AI_RAM));
    registered_backing = copy;
    CHECK(!ai_image_publish(owner, &cart, 7, sync_cache, NULL, &published));
    if (getenv("AI_TEST_EARLY_REVOKE")) {
        CHECK(ai_owner_revoke(owner));
        CHECK(!ai_image_relocate(owner, &cart, 7, image, AI_ROM_BYTES, copy, AI_RAM));
        CHECK(!ai_image_publish(owner, &cart, 7, sync_cache, NULL, &published));
        CHECK(published.entry == 0 && syncs == 0);
        if (copy != image) free(copy);
        free(alternate_backing); free(image); free(reference);
        printf("HOST REAL AI PASSED: %u assertions\n", assertions);
        return 0;
    }
    SprRequest request = {.ram = 0x400000, .bytes = AI_ROM_BYTES,
        .owner_base = 0x400000, .owner_end = 0x400000 + AI_RAM,
        .rom_offset = AI_ROM_START, .rom_low = AI_ROM_START, .rom_end = AI_ROM_END,
        .installed_bytes = 0x800000, .generation = 7, .operation = 1,
        .cold_route = true, .exclusive_staging = true, .unlocked = true,
        .cart_handle = true};
    SprOps ops = {enqueue, receive, cache, validate, validate, validate,
                  relocate, publish, refused};
    SprLoader loader = {0};
    CHECK(spr_start(&loader, &request, &ops, NULL));
    bool failure = getenv("AI_TEST_REVOKE") || getenv("AI_TEST_MAP_SWAP") ||
        getenv("AI_TEST_SYNC_FAIL");
    revoked = getenv("AI_TEST_REVOKE") != NULL;
    cache_ok = getenv("AI_TEST_SYNC_FAIL") == NULL;
    CHECK(spr_poll(&loader, false) == (failure ? SPR_FAIL_STOP : SPR_PUBLISHED));
    CHECK(memcmp(copy, reference, AI_RAM) == 0); /* Every relocated word/BSS byte. */
    CHECK(syncs == 1);
    if (failure) {
        CHECK(published.entry == 0 && loader.retained);
        CHECK(spr_poll(&loader, false) == SPR_FAIL_STOP);
    } else {
        CHECK(published.entry == (0x80400000 + AI_ENTRY_OFFSET) && published.generation == owner.generation);
        CHECK(!ai_image_publish(owner, &cart, 7, sync_cache, NULL, &published));
        CHECK(ai_owner_revoke(owner));
        CHECK(!ai_image_validate(owner, &cart, 7, image, AI_ROM_BYTES));
    }
    if (copy != image) free(copy);
    free(alternate_backing);
    free(image); free(reference);
    printf("HOST REAL AI PASSED: %u assertions\n", assertions);
    return 0;
}
