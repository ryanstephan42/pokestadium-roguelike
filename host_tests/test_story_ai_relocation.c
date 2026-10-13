#include "global.h"
#include "src/memmap.h"
#include "src/story/sc_ai_lease.h"
#undef assert
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

extern void *memcpy(void *destination, const void *source, size_t count);
extern void *memset(void *destination, int value, size_t count);
extern int memcmp(const void *left, const void *right, size_t count);

void osInvalICache(void *address, s32 size) { (void)address; (void)size; }
void osInvalDCache(void *address, s32 size) { (void)address; (void)size; }
void osWritebackDCache(void *address, s32 size) { (void)address; (void)size; }
void _bzero(void *address, size_t size) { memset(address, 0, size); }

static u32 be32(const unsigned char *bytes) {
    return ((u32)bytes[0] << 24) | ((u32)bytes[1] << 16) |
        ((u32)bytes[2] << 8) | bytes[3];
}

static u32 relocate(u32 value, unsigned base) {
    if (value >= 0x8F900000 && value < 0x8FA00000)
        return base + (value & 0xFFFFF);
    if (value >= 0x84300000 && value < 0x84400000)
        return 0x80200000 + (value & 0xFFFFF);
    return value;
}

int main(int argc, char **argv) {
    FILE *file;
    unsigned char *raw;
    u32 *original, *loaded, *expected;
    unsigned bytes, ram, count, offset, i, pass, token = 0, prior_token = 0;
    ScAiLease lease;
    memset(&lease, 0, sizeof(lease));
    assert(argc == 2);
    file = fopen(argv[1], "rb");
    assert(file && fseek(file, 0, SEEK_END) == 0);
    bytes = ftell(file);
    assert(bytes >= 32 && !(bytes & 3));
    rewind(file);
    raw = malloc(bytes);
    assert(raw && fread(raw, 1, bytes, file) == bytes && fclose(file) == 0);
    ram = be32(raw + 28);
    offset = be32(raw + 20);
    assert(ram >= offset && offset < bytes && !(ram & 15));
    original = malloc(bytes);
    loaded = malloc(ram > bytes ? ram : bytes);
    expected = malloc(offset);
    assert(original && loaded && expected);
    for (i = 0; i < bytes / 4; i++) original[i] = be32(raw + i * 4);
    count = original[offset / 4];
    assert(offset + 4 + count * 4 <= bytes);
    for (pass = 0; pass < 2; pass++) {
        u32 high_value[32] = {0}, high_offset[32] = {0};
        unsigned base = (unsigned)loaded;
        memcpy(loaded, original, bytes);
        memcpy(expected, original, offset);
        assert(sc_ai_lease_acquire(&lease, SC_AI_LEASE_AI, 0, 0, &token) == SC_AI_LEASE_OK);
        assert(sc_ai_lease_bind(&lease, SC_AI_LEASE_AI, token, base, ram) == SC_AI_LEASE_OK);
        Memmap_SetFragmentMap(0x33, 0x80200000, 0xD0000);
        /* Apply the complete generated table through the actual retail loader. */
        Memmap_RelocateFragment(233, (Fragment *)loaded);
        assert(Memmap_GetFragmentBaseVaddr(233) == base);
        assert(Memmap_GetFragmentSize(233) == ram);
        assert(sc_ai_lease_publish(&lease, SC_AI_LEASE_AI, token, base, ram) == SC_AI_LEASE_OK);
        for (i = 0; i < count; i++) {
            u32 entry = original[offset / 4 + 1 + i], word, value;
            unsigned location = (entry & 0xFFFFFF) / 4, reg;
            assert(location < offset / 4);
            word = original[location];
            switch (entry >> 24) {
                case 2:
                    expected[location] = relocate(word, base);
                    break;
                case 4:
                    value = relocate(0x80000000 | ((word & 0x3FFFFFF) << 2), base);
                    expected[location] = (word & 0xFC000000) | ((value & 0xFFFFFFF) >> 2);
                    break;
                case 5:
                    reg = (word >> 16) & 31;
                    high_value[reg] = word;
                    high_offset[reg] = location;
                    break;
                case 6:
                    reg = (word >> 21) & 31;
                    assert(high_offset[reg]);
                    value = relocate((high_value[reg] << 16) + (s16)word, base);
                    expected[high_offset[reg]] = (high_value[reg] & 0xFFFF0000) |
                        ((value >> 16) + !!(value & 0x8000));
                    expected[location] = (word & 0xFFFF0000) | (value & 0xFFFF);
                    break;
                default:
                    assert(0);
            }
        }
        assert(memcmp(expected, loaded, offset) == 0);
        for (i = offset; i < ram; i++) assert(((unsigned char *)loaded)[i] == 0);
        assert(sc_ai_lease_revoke(&lease, SC_AI_LEASE_AI, token) == SC_AI_LEASE_OK);
        if (prior_token) {
            assert(sc_ai_lease_free_admit(&lease, SC_AI_LEASE_AI, prior_token,
                                         base, base, ram) == SC_AI_LEASE_STALE);
            assert(Memmap_GetFragmentBaseVaddr(233) == base);
        }
        assert(sc_ai_lease_free_admit(&lease, SC_AI_LEASE_AI, token,
                                     base, base, ram) == SC_AI_LEASE_OK);
        Memmap_ClearFragmentMemmap(233);
        assert(sc_ai_lease_release(&lease, SC_AI_LEASE_AI, token, 0, 0) == SC_AI_LEASE_OK);
        prior_token = token;
    }
    free(expected); free(loaded); free(original); free(raw);
    printf("actual retail loader: %u ID233 relocations, imported BATL rebasing, "
           "BSS clearing, repeated base/mapclear and stale-token refusal passed\n", count);
    return 0;
}
