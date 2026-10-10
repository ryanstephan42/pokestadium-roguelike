#include "global.h"
#include "src/controller.h"
#include "rl_pak.h"

/* Raw reads avoid osPfsInitPak's implicit filesystem repair and block-0 writes. */
extern s32 __osContRamRead(OSMesgQueue *, int, u16, u8 *);
extern s32 __osContRamWrite(OSMesgQueue *, int, u16, u8 *, int);
extern s32 rlPfsAllocateFile(OSPfs *, u16, u32, u8 *, u8 *, int, s32 *);
extern u8 __osPfsInodeCacheBank;

typedef struct RlNativePak {
    unsigned controller;
    RlU8 metadata[RL_PAK_METADATA_SIZE];
    RlPakLayout layout;
} RlNativePak;

static RlSaveStatus io_status(s32 error) {
    switch (error) {
        case 0: return RL_SAVE_OK;
        case PFS_ERR_NOPACK: return RL_SAVE_NO_DEVICE;
        case PFS_ERR_NEW_PACK: return RL_SAVE_DEVICE_CHANGED;
        case PFS_ERR_DEVICE: return RL_SAVE_UNSUPPORTED;
        case PFS_DATA_FULL:
        case PFS_DIR_FULL: return RL_SAVE_STORAGE_FULL;
        case PFS_ERR_INCONSISTENT:
        case PFS_ERR_BAD_DATA: return RL_SAVE_BAD_DATA;
        default: return RL_SAVE_IO;
    }
}

static RlSaveStatus snapshot(RlNativePak *pak) {
    unsigned i;
    RlSaveStatus status;
    u8 bits;
    status = io_status(osPfsIsPlug(&gSIEventMesgQueue, &bits));
    if (status != RL_SAVE_OK) return status;
    if (!(bits & (1U << pak->controller))) return RL_SAVE_NO_DEVICE;
    for (i = 0; i < RL_PAK_METADATA_SIZE / 32; i++) {
        status = io_status(__osContRamRead(&gSIEventMesgQueue, pak->controller,
                                          i, pak->metadata + i * 32));
        if (status != RL_SAVE_OK) return status;
    }
    return rl_pak_validate(pak->metadata, &pak->layout);
}

static RlSaveStatus same_device(RlNativePak *pak) {
    RlU8 id[32];
    RlSaveStatus status;
    unsigned i;
    status = io_status(__osContRamRead(&gSIEventMesgQueue, pak->controller, 1, id));
    if (status != RL_SAVE_OK) return status;
    for (i = 0; i < 32; i++)
        if (id[i] != pak->metadata[i + 32]) return RL_SAVE_DEVICE_CHANGED;
    return RL_SAVE_OK;
}

static RlSaveStatus transfer(void *context, unsigned offset, RlU8 *data,
                             unsigned size, int write) {
    RlNativePak *pak;
    RlSaveStatus status;
    unsigned block, i;
    pak = context;
    if ((offset & 31) || (size & 31) || offset > RL_STORAGE_NOTE_SIZE ||
        size > RL_STORAGE_NOTE_SIZE - offset) return RL_SAVE_INVALID;
    status = same_device(pak);
    if (status != RL_SAVE_OK) return status;
    for (i = 0; i < size; i += 32) {
        block = offset + i;
        block = pak->layout.pages[block / 256] * 8 + (block % 256) / 32;
        if (write)
            status = io_status(__osContRamWrite(&gSIEventMesgQueue, pak->controller,
                                               block, data + i, 0));
        else
            status = io_status(__osContRamRead(&gSIEventMesgQueue, pak->controller,
                                              block, data + i));
        if (status != RL_SAVE_OK) return status;
    }
    return same_device(pak);
}

static RlSaveStatus native_read(void *p, unsigned offset, RlU8 *data, unsigned n) {
    return transfer(p, offset, data, n, 0);
}

static RlSaveStatus native_write(void *p, unsigned offset, const RlU8 *data, unsigned n) {
    RlU8 block[32];
    RlSaveStatus status;
    unsigned i, j;
    for (i = 0; i < n; i += 32) {
        for (j = 0; j < 32; j++) block[j] = data[i + j];
        status = transfer(p, offset + i, block, 32, 1);
        if (status != RL_SAVE_OK) return status;
    }
    return RL_SAVE_OK;
}

static RlSaveStatus native_open(void *context, int create) {
    RlNativePak *pak;
    RlSaveStatus status;
    OSPfs pfs;
    s32 note;
    RlU8 zero[32], name[16], extension[4], original_id[32];
    unsigned i;
    pak = context;
    status = snapshot(pak);
    if (status != RL_SAVE_NOT_FOUND || !create) return status;
    if (!pak->layout.free_notes || pak->layout.free_pages < RL_PAK_PAGES)
        return RL_SAVE_STORAGE_FULL;
    bzero(&pfs, sizeof(pfs));
    pfs.queue = &gSIEventMesgQueue;
    pfs.channel = pak->controller;
    pfs.status = PFS_INITIALIZED;
    pfs.banks = 1;
    pfs.version = pak->metadata[32 + 27];
    pfs.inode_start_page = 5;
    pfs.dir_size = 16;
    pfs.inode_table = 8;
    pfs.minode_table = 16;
    pfs.dir_table = 24;
    for (i = 0; i < 32; i++) {
        pfs.id[i] = original_id[i] = pak->metadata[i + 32];
        zero[i] = 0;
    }
    for (i = 0; i < 16; i++) name[i] = gRlPakName[i];
    for (i = 0; i < 4; i++) extension[i] = gRlPakExtension[i];
    status = same_device(pak);
    if (status != RL_SAVE_OK) return status;
    __osPfsInodeCacheBank = 250;
    status = io_status(rlPfsAllocateFile(&pfs, RL_PAK_COMPANY_CODE, RL_PAK_GAME_CODE,
                                        name, extension, RL_STORAGE_NOTE_SIZE, &note));
    if (status != RL_SAVE_OK) return status;
    status = snapshot(pak);
    if (status != RL_SAVE_OK) return status == RL_SAVE_NOT_FOUND ? RL_SAVE_IO : status;
    for (i = 0; i < 32; i++)
        if (original_id[i] != pak->metadata[i + 32]) return RL_SAVE_DEVICE_CHANGED;
    if (pak->layout.note != (unsigned)note) return RL_SAVE_BAD_DATA;
    for (i = 0; i < RL_STORAGE_NOTE_SIZE; i += 32) {
        status = native_write(pak, i, zero, sizeof(zero));
        if (status != RL_SAVE_OK) return status;
    }
    for (i = 0; i < 32; i++) zero[i] = pak->metadata[768 + note * 32 + i];
    zero[8] = 2;
    zero[9] = zero[10] = zero[11] = 0;
    status = same_device(pak);
    if (status != RL_SAVE_OK) return status;
    return io_status(__osContRamWrite(&gSIEventMesgQueue, pak->controller,
                                      24 + note, zero, 0));
}

static RlSaveStatus native_call(unsigned controller, RlMeta *load_meta, RunState *load_run,
                                RlU8 *has_run, const RlMeta *meta, const RunState *run,
                                int create, int writing) {
    static RlNativePak pak;
    RlSaveDevice device;
    RlSaveStatus status;
    if (controller >= 4) return RL_SAVE_INVALID;
    device.context = &pak;
    device.open = native_open;
    device.read = native_read;
    device.write = native_write;
    Cont_BlockEepromQueue();
    pak.controller = controller;
    if (writing) status = rl_storage_save(&device, meta, run, create);
    else status = rl_storage_load(&device, load_meta, load_run, has_run);
    Cont_NoBlockEepromQueue();
    return status;
}

RlSaveStatus rl_save_load(unsigned controller, RlMeta *meta, RunState *run, RlU8 *has_run) {
    return native_call(controller, meta, run, has_run, NULL, NULL, 0, 0);
}

RlSaveStatus rl_save_store(unsigned controller, const RlMeta *meta, const RunState *run, int create) {
    return native_call(controller, NULL, NULL, NULL, meta, run, create, 1);
}
