/* PURE */
#include "rl_save.h"

#define STORAGE_MAGIC 0x4b504c52U
#define STORAGE_VERSION 1

typedef struct RlSlot {
    RlU8 header[RL_STORAGE_HEADER_SIZE];
    RlU8 payload[RL_SAVE_MAX_SIZE];
    RlU32 generation;
    unsigned length;
    RlSaveStatus status;
} RlSlot;

static RlU32 get32(const RlU8 *p) {
    return (RlU32)p[0] | ((RlU32)p[1] << 8) |
        ((RlU32)p[2] << 16) | ((RlU32)p[3] << 24);
}

static void put32(RlU8 *p, RlU32 n) {
    unsigned i;
    for (i = 0; i < 4; i++) p[i] = (RlU8)(n >> (8 * i));
}

static int valid_device(const RlSaveDevice *d) {
    return d && d->open && d->read && d->write;
}

static RlSaveStatus read_slot(const RlSaveDevice *d, unsigned index, RlSlot *s) {
    unsigned i;
    RlSaveStatus result;
    RlMeta meta;
    RunState run;
    RlU8 suspended;
    result = d->read(d->context, index * RL_STORAGE_SLOT_SIZE,
                     s->header, RL_STORAGE_HEADER_SIZE);
    if (result != RL_SAVE_OK) return result;
    for (i = 0; i < RL_STORAGE_HEADER_SIZE; i++)
        if (s->header[i]) break;
    if (i == RL_STORAGE_HEADER_SIZE) return RL_SAVE_NOT_FOUND;
    if (get32(s->header) != STORAGE_MAGIC) return RL_SAVE_BAD_DATA;
    if (get32(s->header + 28) != rl_save_crc32(s->header, 28))
        return RL_SAVE_BAD_CRC;
    if (get32(s->header + 4) != STORAGE_VERSION) return RL_SAVE_BAD_VERSION;
    for (i = 20; i < 28; i++)
        if (s->header[i]) return RL_SAVE_BAD_DATA;
    s->generation = get32(s->header + 8);
    s->length = get32(s->header + 12);
    if (s->length < 32 || s->length > RL_SAVE_MAX_SIZE) return RL_SAVE_BAD_DATA;
    result = d->read(d->context, index * RL_STORAGE_SLOT_SIZE + 32,
                     s->payload, (s->length + 31) & ~31U);
    if (result != RL_SAVE_OK) return result;
    if (get32(s->header + 16) != rl_save_crc32(s->payload, s->length))
        return RL_SAVE_BAD_CRC;
    if ((s->payload[4] != RL_SAVE_VERSION &&
         s->payload[4] != RL_SAVE_LEGACY_VERSION) || s->payload[5])
        return RL_SAVE_BAD_VERSION;
    if (rl_save_decode(&meta, &run, &suspended, s->payload, s->length) != RL_OK)
        return RL_SAVE_BAD_DATA;
    return RL_SAVE_OK;
}

static int corruption(RlSaveStatus status) {
    return status == RL_SAVE_BAD_CRC || status == RL_SAVE_BAD_DATA;
}

static RlSaveStatus scan(const RlSaveDevice *d, RlSlot slots[2], int *latest) {
    unsigned i;
    *latest = -1;
    for (i = 0; i < 2; i++) {
        slots[i].status = read_slot(d, i, &slots[i]);
        if (slots[i].status != RL_SAVE_OK && slots[i].status != RL_SAVE_NOT_FOUND &&
            !corruption(slots[i].status)) return slots[i].status;
        if (slots[i].status == RL_SAVE_OK) {
            if (*latest < 0) *latest = (int)i;
            else {
                RlU32 delta;
                delta = slots[i].generation - slots[*latest].generation;
                if (!delta || delta == 0x80000000U) return RL_SAVE_BAD_DATA;
                if (delta < 0x80000000U) *latest = (int)i;
            }
        }
    }
    if (*latest < 0) {
        for (i = 0; i < 2; i++)
            if (corruption(slots[i].status)) return slots[i].status;
        return RL_SAVE_NOT_FOUND;
    }
    for (i = 0; i < 2; i++)
        if (corruption(slots[i].status)) return RL_SAVE_RECOVERED;
    return RL_SAVE_OK;
}

RlSaveStatus rl_storage_load(const RlSaveDevice *d, RlMeta *meta,
                             RunState *run, RlU8 *has_run) {
    RlSlot slots[2];
    RlSaveStatus status;
    int latest;
    if (!valid_device(d) || !meta || !has_run) return RL_SAVE_INVALID;
    status = d->open(d->context, 0);
    if (status != RL_SAVE_OK) return status;
    status = scan(d, slots, &latest);
    if (status != RL_SAVE_OK && status != RL_SAVE_RECOVERED) return status;
    if (rl_save_decode(meta, run, has_run, slots[latest].payload,
                       slots[latest].length) != RL_OK) return RL_SAVE_INVALID;
    return status;
}

RlSaveStatus rl_storage_save(const RlSaveDevice *d, const RlMeta *meta,
                             const RunState *run, int create) {
    RlSlot slots[2];
    RlSlot next;
    RlU8 clear[RL_STORAGE_HEADER_SIZE];
    RlSaveStatus status;
    unsigned i, target, padded;
    int latest;
    if (!valid_device(d) || (create != 0 && create != 1)) return RL_SAVE_INVALID;
    for (i = 0; i < sizeof(next.payload); i++) next.payload[i] = 0;
    if (rl_save_encode(meta, run, next.payload, sizeof(next.payload),
                       &next.length) != RL_OK) return RL_SAVE_INVALID;
    status = d->open(d->context, create);
    if (status != RL_SAVE_OK) return status;
    status = scan(d, slots, &latest);
    if (status == RL_SAVE_RECOVERED) {
        for (i = 0; i < 2; i++)
            if (corruption(slots[i].status)) return slots[i].status;
    }
    if (status != RL_SAVE_OK && status != RL_SAVE_NOT_FOUND) return status;
    if (latest < 0 && !create) return RL_SAVE_NOT_FOUND;
    target = latest < 0 ? 0 : 1U - (unsigned)latest;
    next.generation = latest < 0 ? 0 : slots[latest].generation + 1;
    for (i = 0; i < sizeof(clear); i++) clear[i] = 0;
    status = d->write(d->context, target * RL_STORAGE_SLOT_SIZE, clear, sizeof(clear));
    if (status != RL_SAVE_OK) return status;
    padded = (next.length + 31) & ~31U;
    status = d->write(d->context, target * RL_STORAGE_SLOT_SIZE + 32, next.payload, padded);
    if (status != RL_SAVE_OK) return status;
    status = d->read(d->context, target * RL_STORAGE_SLOT_SIZE + 32, slots[target].payload, padded);
    if (status != RL_SAVE_OK) return status;
    for (i = 0; i < padded; i++)
        if (next.payload[i] != slots[target].payload[i]) return RL_SAVE_IO;
    for (i = 0; i < sizeof(next.header); i++) next.header[i] = 0;
    put32(next.header, STORAGE_MAGIC);
    put32(next.header + 4, STORAGE_VERSION);
    put32(next.header + 8, next.generation);
    put32(next.header + 12, next.length);
    put32(next.header + 16, rl_save_crc32(next.payload, next.length));
    put32(next.header + 28, rl_save_crc32(next.header, 28));
    status = d->write(d->context, target * RL_STORAGE_SLOT_SIZE, next.header, sizeof(next.header));
    if (status != RL_SAVE_OK) return status;
    status = read_slot(d, target, &slots[target]);
    if (status != RL_SAVE_OK) return status;
    for (i = 0; i < sizeof(next.header); i++)
        if (next.header[i] != slots[target].header[i]) return RL_SAVE_IO;
    for (i = 0; i < next.length; i++)
        if (next.payload[i] != slots[target].payload[i]) return RL_SAVE_IO;
    return RL_SAVE_OK;
}

const char *rl_save_status_text(RlSaveStatus s) {
    switch (s) {
        case RL_SAVE_OK: return "Save complete";
        case RL_SAVE_NOT_FOUND: return "No roguelike save; create explicitly";
        case RL_SAVE_NO_DEVICE: return "Controller Pak missing";
        case RL_SAVE_UNSUPPORTED: return "Unsupported accessory or filesystem";
        case RL_SAVE_DEVICE_CHANGED: return "Controller Pak changed; retry";
        case RL_SAVE_STORAGE_FULL: return "Controller Pak pages or notes full";
        case RL_SAVE_IO: return "Controller Pak I/O failed; reload before retry";
        case RL_SAVE_BAD_CRC: return "Save checksum failed; preserve damaged data";
        case RL_SAVE_BAD_VERSION: return "Unsupported save version; preserve data";
        case RL_SAVE_BAD_DATA: return "Invalid save or filesystem; preserve data";
        case RL_SAVE_INVALID: return "Invalid save API argument";
        case RL_SAVE_RECOVERED: return "Recovered older save; damaged copy preserved";
        default: return "Unknown save status";
    }
}
