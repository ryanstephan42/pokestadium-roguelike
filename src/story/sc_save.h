#ifndef SC_SAVE_H
#define SC_SAVE_H

#include "sc_core.h"

#define SC_SAVE_SIZE 736
#define SC_SAVE_VERSION 2
#define SC_SAVE_INVENTORY_OFFSET 710
#define SC_SAVE_SLOT_SIZE 768
#define SC_SAVE_NOTE_SIZE (2 * SC_SAVE_SLOT_SIZE)
#define SC_SAVE_COMPANY 0x524c
#define SC_SAVE_GAME 0x53433031U

enum ScSaveStatus {
    SC_SAVE_OK, SC_SAVE_NOT_FOUND, SC_SAVE_NO_DEVICE, SC_SAVE_FULL,
    SC_SAVE_IO, SC_SAVE_BAD_DATA, SC_SAVE_BAD_VERSION, SC_SAVE_BAD_CRC,
    SC_SAVE_INVALID, SC_SAVE_RECOVERED, SC_SAVE_DEVICE_CHANGED
};
typedef struct ScSnapshot {
    ScState state;
    RlU8 interrupted, wire_version;
} ScSnapshot;
typedef struct ScSaveDevice {
    void *context;
    int (*open)(void *context, unsigned company, RlU32 game,
                const RlU8 name[16], const RlU8 extension[4],
                unsigned size, int create);
    int (*read)(void *context, unsigned offset, RlU8 *data, unsigned size);
    int (*write)(void *context, unsigned offset, const RlU8 *data, unsigned size);
} ScSaveDevice;

extern const RlU8 sc_save_name[16], sc_save_extension[4];
RlU32 sc_save_crc(const RlU8 *data, unsigned size);
int sc_state_valid(const ScState *state, const ScCatalog *catalog);
int sc_save_encode(const ScSnapshot *snapshot, const ScCatalog *catalog,
                   RlU32 generation, RlU8 data[SC_SAVE_SIZE]);
int sc_save_decode(ScSnapshot *snapshot, const ScCatalog *catalog,
                   RlU32 *generation, const RlU8 *data, unsigned size);
int sc_storage_load(const ScSaveDevice *device, const ScCatalog *catalog,
                    ScSnapshot *snapshot);
int sc_storage_store(const ScSaveDevice *device, const ScCatalog *catalog,
                     const ScSnapshot *snapshot, int create);

#endif
