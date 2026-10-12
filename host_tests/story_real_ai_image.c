#include "story_real_ai_image.h"

#include "ai_sizes.h"
#ifdef AI_AUTHORITY_PACKED
typedef struct { uint32_t descriptor, word; } AiReloc;
#else
typedef struct { uint32_t kind, offset, word, target, internal; } AiReloc;
#endif
#include "ai_authority.h"

static uint32_t reloc_kind(const AiReloc *r) {
#ifdef AI_AUTHORITY_PACKED
    return r->descriptor >> 24 & 0x7f;
#else
    return r->kind;
#endif
}

static uint32_t reloc_offset(const AiReloc *r) {
#ifdef AI_AUTHORITY_PACKED
    return r->descriptor & 0xffffff;
#else
    return r->offset;
#endif
}

static uint32_t reloc_internal(const AiReloc *r) {
#ifdef AI_AUTHORITY_PACKED
    return r->descriptor >> 31;
#else
    return r->internal;
#endif
}

static struct {
    const void *cart;
    const uint8_t *private_copy;
    uint8_t *backing;
    AiMap map;
    void *map_context;
    uint32_t generation, base, native, battle, relocated_crc;
    bool initialized, live, bound, revoked, relocated, published;
} authority;

static uint32_t word(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
        (uint32_t)p[2] << 8 | p[3];
}
static void put(uint8_t *p, uint32_t w) {
    p[0] = (uint8_t)(w >> 24); p[1] = (uint8_t)(w >> 16);
    p[2] = (uint8_t)(w >> 8); p[3] = (uint8_t)w;
}
static uint32_t crc(const uint8_t *p, size_t n) {
    uint32_t result = UINT32_MAX;
    size_t i;
    unsigned bit;
    for (i = 0; i < n; ++i) {
        result ^= p[i];
        for (bit = 0; bit < 8; ++bit)
            result = result >> 1 ^ (UINT32_C(0xedb88320) & (0u - (result & 1)));
    }
    return ~result;
}
static bool live(AiOwner o, const void *cart, uint32_t battle) {
    return authority.live && authority.bound && !authority.revoked &&
        cart == authority.cart && o.generation == authority.generation &&
        battle && battle == authority.battle;
}

bool ai_authority_init(const void *cart, uint32_t mem, AiMap map, void *ctx) {
    if (authority.initialized || !cart || !map || mem != 0x800000) return false;
    authority.cart = cart;
    authority.map = map;
    authority.map_context = ctx;
    authority.initialized = true;
    return true;
}
bool ai_owner_allocate(uint32_t base, uint32_t native, AiOwner *o) {
    uint8_t *backing;
    if (!o || !authority.initialized || authority.live ||
        authority.generation == UINT32_MAX || (base & 15) ||
        base < 0x80400000 || base >= 0x807da800 ||
        AI_RAM >= 0x807da800 - base || (native & 15) ||
        native < 0x80000000 || native >= 0x80400000 ||
        AI_NATIVE_END - 0x84300000 > 0x80400000 - native) return false;
    backing = authority.map(authority.map_context, base, AI_RAM);
    if (!backing || (uintptr_t)backing > UINTPTR_MAX - AI_RAM) return false;
    authority.backing = backing;
    ++authority.generation;
    authority.live = true;
    authority.base = base;
    authority.native = native;
    o->generation = authority.generation;
    return true;
}
static bool mapped_backing(void) {
    return authority.backing && authority.map(authority.map_context,
        authority.base, AI_RAM) == authority.backing;
}
bool ai_owner_bind(AiOwner o, const void *cart, uint32_t start, uint32_t end,
                   uint32_t battle) {
    if (!authority.live || authority.revoked || authority.bound ||
        o.generation != authority.generation || cart != authority.cart ||
        !battle || start != AI_ROM_START || end != AI_ROM_END) return false;
    authority.bound = true;
    authority.battle = battle;
    return true;
}
bool ai_owner_revoke(AiOwner o) {
    if (!authority.live || o.generation != authority.generation) return false;
    authority.revoked = true; /* No release/reuse with an outstanding loader. */
    return true;
}
bool ai_owner_admit(AiOwner o, const void *cart, uint32_t battle,
                    uint32_t start, uint32_t end) {
    return live(o, cart, battle) && start == AI_ROM_START && end == AI_ROM_END;
}

bool ai_owner_retire(AiOwner o, const AiPublished *p) {
    if (!p || !authority.live || !authority.revoked || !authority.published ||
        o.generation != authority.generation || p->generation != o.generation ||
        p->image_base != authority.base || p->native_base != authority.native ||
        p->entry != authority.base + AI_ENTRY_OFFSET) return false;
    authority.live = authority.bound = authority.revoked = false;
    authority.published = authority.relocated = false;
    authority.backing = 0;
    authority.private_copy = 0;
    authority.base = authority.native = authority.battle = authority.relocated_crc = 0;
    return true;
}
bool ai_owner_span(AiOwner o, uint32_t base, uint32_t end, uint32_t bytes,
                   uint32_t operation) {
    uint32_t physical = authority.base & 0x1fffffff;
    return authority.live && authority.bound && !authority.revoked &&
        !authority.published && mapped_backing() &&
        o.generation == authority.generation &&
        base == physical && end == physical + AI_RAM &&
        bytes == AI_ROM_BYTES && operation == o.generation;
}

bool ai_image_validate(AiOwner o, const void *cart, uint32_t battle,
                       const uint8_t *image, size_t bytes) {
    uint32_t high[32] = {0};
    bool valid[32] = {false};
    uint32_t i;
    if (!live(o, cart, battle) || !image || bytes != AI_ROM_BYTES ||
        word(image) != ai_table[0].word || word(image + 4) != 0 ||
        word(image + 8) != 0x46524147 || word(image + 12) != 0x4d454e54 ||
        word(image + 16) != 32 ||
        word(image + 20) != AI_PAYLOAD || word(image + 24) != AI_ROM_BYTES ||
        word(image + 28) != AI_RAM || crc(image, bytes) != AI_CRC ||
        word(image + AI_PAYLOAD) != AI_COUNT) return false;
    for (i = 0; i < AI_COUNT; ++i) {
        const AiReloc *r = &ai_table[i];
        uint32_t kind = reloc_kind(r), offset = reloc_offset(r);
        uint32_t value;
        if ((offset & 3) || offset > AI_PAYLOAD - 4 ||
            (offset < 32 && offset != 0) ||
            word(image + AI_PAYLOAD + 4 + 4 * i) != (kind << 24 | offset) ||
            word(image + offset) != r->word) return false;
        if (kind == 5) {
            unsigned reg = r->word >> 16 & 31;
            if (r->word >> 26 != 15) return false;
            valid[reg] = true;
            high[reg] = r->word;
            continue;
        }
        if (kind == 2) value = r->word;
        else if (kind == 4) {
            if (r->word >> 26 != 2 && r->word >> 26 != 3) return false;
            value = ((r->word * 4) & 0x0ffffffc) + 0x80000000;
        } else if (kind == 6) {
            unsigned reg = r->word >> 21 & 31;
            uint32_t low = r->word & 65535;
            if (!valid[reg]) return false;
            value = (high[reg] << 16) + low - ((low & 32768) ? 65536u : 0u);
        } else return false;
#ifndef AI_AUTHORITY_PACKED
        if (value != r->target) return false;
#endif
        if (reloc_internal(r) && (value < 0x8f900000 || value - 0x8f900000 >= AI_RAM))
            return false;
        /* Exact generated target/addend allowlist, not arbitrary symbol ranges. */
    }
    for (i = AI_PAYLOAD + 4 + 4 * AI_COUNT; i < AI_ROM_BYTES; ++i)
        if (image[i]) return false;
    return true;
}

static uint32_t resolve(uint32_t value) {
    if (value >= 0x8f900000 && value - 0x8f900000 < AI_RAM)
        return authority.base + value - 0x8f900000;
    if (value >= 0x84300000 && value < AI_NATIVE_END)
        return authority.native + value - 0x84300000;
    return value;
}

bool ai_image_relocate(AiOwner o, const void *cart, uint32_t battle,
                       const uint8_t *image, size_t bytes, uint8_t *copy,
                       size_t capacity) {
    uintptr_t src = (uintptr_t)image, dst = (uintptr_t)copy;
    uint32_t refs[32] = {0}, vals[32] = {0};
    uint32_t i;
    if (!copy || !image || copy != authority.backing || !mapped_backing() ||
        capacity != AI_RAM ||
        src > UINTPTR_MAX - bytes || dst > UINTPTR_MAX - AI_RAM ||
        (src != dst && src < dst + AI_RAM && dst < src + bytes) ||
        authority.relocated ||
        !ai_image_validate(o, cart, battle, image, bytes)) return false;
    /* Exact in-place unpublished staging is allowed only after full preflight. */
    if (copy != image)
        for (i = 0; i < AI_PAYLOAD; ++i) copy[i] = image[i];
    for (i = AI_PAYLOAD; i < AI_RAM; ++i) copy[i] = 0;
    for (i = 0; i < AI_COUNT; ++i) {
        const AiReloc *r = &ai_table[i];
        uint32_t kind = reloc_kind(r), offset = reloc_offset(r);
        uint32_t w = word(copy + offset), resolved;
#ifdef AI_AUTHORITY_PACKED
        uint32_t value = 0;
        if (kind == 2) value = r->word;
        else if (kind == 4) value = ((r->word * 4) & 0x0ffffffc) + 0x80000000;
        else if (kind == 6) {
            uint32_t low = r->word & 65535;
            if (low & 32768) low |= 0xffff0000;
            value = (vals[r->word >> 21 & 31] << 16) + low;
        }
        resolved = resolve(value);
#else
        resolved = resolve(r->target);
#endif
        if (kind == 2) put(copy + offset, resolved);
        else if (kind == 4)
            put(copy + offset, (w & 0xfc000000) | ((resolved & 0x0fffffff) >> 2));
        else if (kind == 5) {
            unsigned reg = w >> 16 & 31;
            refs[reg] = offset;
            vals[reg] = w;
        } else {
            unsigned reg = w >> 21 & 31;
            uint32_t lo = w & 65535;
            resolved = resolve((vals[reg] << 16) + lo - ((lo & 32768) ? 65536u : 0u));
            put(copy + refs[reg], (word(copy + refs[reg]) & 0xffff0000) |
                (((resolved >> 16) + !!(resolved & 32768)) & 65535));
            put(copy + offset, (w & 0xffff0000) | (resolved & 65535));
        }
    }
    if (!live(o, cart, battle) || !mapped_backing()) return false;
    authority.private_copy = copy;
    authority.relocated_crc = crc(copy, AI_RAM);
    authority.relocated = true;
    return true;
}
bool ai_image_publish(AiOwner o, const void *cart, uint32_t battle,
                      AiSync sync, void *ctx,
                      AiPublished *published) {
    if (!published || !sync || !live(o, cart, battle) ||
        !authority.relocated || authority.published || !mapped_backing() ||
        authority.private_copy != authority.backing ||
        AI_ENTRY_OFFSET < 32 || AI_ENTRY_OFFSET >= AI_PAYLOAD ||
        crc(authority.private_copy, AI_RAM) != authority.relocated_crc) return false;
    if (!sync(ctx, authority.backing, authority.base, AI_RAM) ||
        !live(o, cart, battle) || !mapped_backing() ||
        crc(authority.private_copy, AI_RAM) != authority.relocated_crc) return false;
    authority.published = true;
    published->generation = authority.generation;
    published->image_base = authority.base;
    published->native_base = authority.native;
    published->entry = authority.base + AI_ENTRY_OFFSET;
    return true;
}
