/* PURE */
#include "sc_save.h"

typedef struct ScSlot {
    RlU8 bytes[SC_SAVE_SIZE];
    RlU32 generation;
    int status;
} ScSlot;

static int device_valid(const ScSaveDevice *device) {
    return device && device->open && device->read && device->write;
}

static int open_note(const ScSaveDevice *device, int create) {
    return device->open(device->context, SC_SAVE_COMPANY, SC_SAVE_GAME,
                        sc_save_name, sc_save_extension, SC_SAVE_NOTE_SIZE, create);
}

static int scan(const ScSaveDevice *device, const ScCatalog *catalog,
                ScSlot slots[2], int *latest) {
    unsigned i, j;
    int status, damaged = 0;
#ifdef STORY_NATIVE_PROBE
    static ScSnapshot snapshot;
#else
    ScSnapshot snapshot;
#endif
    *latest = -1;
    for (i = 0; i < 2; i++) {
        status = device->read(device->context, i * SC_SAVE_SLOT_SIZE,
                              slots[i].bytes, SC_SAVE_SIZE);
        if (status != SC_SAVE_OK) return status;
        for (j = 0; j < SC_SAVE_SIZE; j++) if (slots[i].bytes[j]) break;
        if (j == SC_SAVE_SIZE) status = SC_SAVE_NOT_FOUND;
        else status = sc_save_decode(&snapshot, catalog, &slots[i].generation,
                                     slots[i].bytes, SC_SAVE_SIZE);
        slots[i].status = status;
        if (status == SC_SAVE_BAD_DATA || status == SC_SAVE_BAD_CRC) damaged = 1;
        else if (status != SC_SAVE_OK && status != SC_SAVE_NOT_FOUND) return status;
        if (status == SC_SAVE_OK) {
            if (*latest < 0) *latest = (int)i;
            else {
                RlU32 delta = slots[i].generation - slots[*latest].generation;
                if (!delta || delta == 0x80000000U) return SC_SAVE_BAD_DATA;
                if (delta < 0x80000000U) *latest = (int)i;
            }
        }
    }
    if (*latest < 0) return damaged ? SC_SAVE_BAD_DATA : SC_SAVE_NOT_FOUND;
    return damaged ? SC_SAVE_RECOVERED : SC_SAVE_OK;
}

int sc_storage_load(const ScSaveDevice *device, const ScCatalog *catalog,
                    ScSnapshot *snapshot) {
#ifdef STORY_NATIVE_PROBE
    static ScSlot slots[2];
#else
    ScSlot slots[2];
#endif
    RlU32 generation;
    int latest, status;
    if (!device_valid(device) || !snapshot || !sc_catalog_valid(catalog) ||
        !catalog->graph) return SC_SAVE_INVALID;
    status = open_note(device, 0);
    if (status != SC_SAVE_OK) return status;
    status = scan(device, catalog, slots, &latest);
    if (status != SC_SAVE_OK && status != SC_SAVE_RECOVERED) return status;
    if (sc_save_decode(snapshot, catalog, &generation, slots[latest].bytes,
                       SC_SAVE_SIZE) != SC_SAVE_OK) return SC_SAVE_BAD_DATA;
    return status;
}

int sc_storage_store(const ScSaveDevice *device, const ScCatalog *catalog,
                     const ScSnapshot *snapshot, int create) {
#ifdef STORY_NATIVE_PROBE
    static ScSlot slots[2];
    static RlU8 bytes[SC_SAVE_SIZE], verify[SC_SAVE_SIZE], empty[32];
#else
    ScSlot slots[2];
    RlU8 bytes[SC_SAVE_SIZE], verify[SC_SAVE_SIZE], empty[32];
#endif
    RlU32 generation;
    unsigned i, offset;
    int latest, status;
    if (!device_valid(device) || (create != 0 && create != 1)) return SC_SAVE_INVALID;
    status = sc_save_encode(snapshot, catalog, 0, bytes);
    if (status != SC_SAVE_OK) return status;
    status = open_note(device, create);
    if (status != SC_SAVE_OK) return status;
    status = scan(device, catalog, slots, &latest);
    if (status != SC_SAVE_OK && status != SC_SAVE_RECOVERED &&
        status != SC_SAVE_NOT_FOUND) return status;
    generation = latest < 0 ? 0 : slots[latest].generation + 1;
    offset = (latest == 0 ? 1 : 0) * SC_SAVE_SLOT_SIZE;
    status = sc_save_encode(snapshot, catalog, generation, bytes);
    if (status != SC_SAVE_OK) return status;
    for (i = 0; i < 32; i++) empty[i] = 0;
    status = device->write(device->context, offset, empty, 32);
    if (status != SC_SAVE_OK) return status;
    status = device->write(device->context, offset + 32, bytes + 32, SC_SAVE_SIZE - 32);
    if (status != SC_SAVE_OK) return status;
    status = device->read(device->context, offset + 32, verify + 32, SC_SAVE_SIZE - 32);
    if (status != SC_SAVE_OK) return status;
    for (i = 32; i < SC_SAVE_SIZE; i++) if (verify[i] != bytes[i]) return SC_SAVE_IO;
    status = device->write(device->context, offset, bytes, 32);
    if (status != SC_SAVE_OK) return status;
    status = device->read(device->context, offset, verify, SC_SAVE_SIZE);
    if (status != SC_SAVE_OK) return status;
    for (i = 0; i < SC_SAVE_SIZE; i++) if (verify[i] != bytes[i]) return SC_SAVE_IO;
    return SC_SAVE_OK;
}
