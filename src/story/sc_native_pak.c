#if defined(STORY_NATIVE_PROBE) || defined(STORY_NATIVE_UI)
#include "sc_native_pak.h"
#include "src/controller.h"

extern s32 __osContRamRead(OSMesgQueue *, int, u16, u8 *);
extern s32 __osContRamWrite(OSMesgQueue *, int, u16, u8 *, int);
extern s32 rlPfsAllocateFile(OSPfs *, u16, u32, u8 *, u8 *, int, s32 *);
extern u8 __osPfsInodeCacheBank;

static ScNativePak *sSession;

static int io_status(s32 error) {
    switch (error) {
        case 0: return SC_SAVE_OK;
        case PFS_ERR_NOPACK: return SC_SAVE_NO_DEVICE;
        case PFS_ERR_NEW_PACK: return SC_SAVE_DEVICE_CHANGED;
        case PFS_DATA_FULL:
        case PFS_DIR_FULL: return SC_SAVE_FULL;
        case PFS_ERR_DEVICE:
        case PFS_ERR_INCONSISTENT:
        case PFS_ERR_BAD_DATA: return SC_SAVE_BAD_DATA;
        default: return SC_SAVE_IO;
    }
}

static int snapshot(ScNativePak *pak) {
    static const RlPakIdentity identity = {
        sc_save_name, sc_save_extension, SC_SAVE_GAME, SC_SAVE_COMPANY, SC_SAVE_NOTE_SIZE / 256
    };
    unsigned i;
    int status;
    u8 bits;
    RlSaveStatus checked;
    status = io_status(osPfsIsPlug(&gSIEventMesgQueue, &bits));
    if (status != SC_SAVE_OK) return status;
    if (!(bits & (1U << pak->controller))) return SC_SAVE_NO_DEVICE;
    for (i = 0; i < RL_PAK_METADATA_SIZE / 32; i++) {
        status = io_status(__osContRamRead(&gSIEventMesgQueue, pak->controller,
                                          i, pak->metadata + i * 32));
        if (status != SC_SAVE_OK) return status;
    }
    checked = rl_pak_validate_note(pak->metadata, &identity, &pak->layout);
    if (checked == RL_SAVE_OK || checked == RL_SAVE_NOT_FOUND) {
        for (i = 0; i < 32; i++)
            if (pak->identified && pak->device_id[i] != pak->metadata[i + 32])
                return SC_SAVE_DEVICE_CHANGED;
        for (i = 0; i < 32; i++) pak->device_id[i] = pak->metadata[i + 32];
        pak->identified = 1;
    }
    if (checked == RL_SAVE_OK) {
        pak->note_seen = 1;
        return SC_SAVE_OK;
    }
    if (checked == RL_SAVE_NOT_FOUND)
        return pak->note_seen ? SC_SAVE_BAD_DATA : SC_SAVE_NOT_FOUND;
    return SC_SAVE_BAD_DATA;
}

static int same_device(ScNativePak *pak) {
    RlU8 id[32];
    unsigned i;
    int status = io_status(__osContRamRead(&gSIEventMesgQueue, pak->controller, 1, id));
    if (status != SC_SAVE_OK) return status;
    for (i = 0; i < 32; i++)
        if (id[i] != pak->metadata[i + 32]) return SC_SAVE_DEVICE_CHANGED;
    return SC_SAVE_OK;
}

static int transfer(void *context, unsigned offset, RlU8 *data, unsigned size, int write) {
    ScNativePak *pak = context;
    unsigned i, block;
    int status;
    if (!pak || sSession != pak || !pak->opened || !data ||
        (offset & 31) || (size & 31) || offset > SC_SAVE_NOTE_SIZE ||
        size > SC_SAVE_NOTE_SIZE - offset) return SC_SAVE_INVALID;
    status = same_device(pak);
    if (status != SC_SAVE_OK) return status;
    for (i = 0; i < size; i += 32) {
        block = offset + i;
        block = pak->layout.pages[block / 256] * 8 + (block % 256) / 32;
        if (write)
            status = io_status(__osContRamWrite(&gSIEventMesgQueue, pak->controller,
                                               block, data + i, 0));
        else
            status = io_status(__osContRamRead(&gSIEventMesgQueue, pak->controller,
                                              block, data + i));
        if (status != SC_SAVE_OK) return status;
    }
    return same_device(pak);
}

static int native_read(void *context, unsigned offset, RlU8 *data, unsigned size) {
    return transfer(context, offset, data, size, 0);
}

static int native_write(void *context, unsigned offset, const RlU8 *data, unsigned size) {
    RlU8 block[32];
    unsigned i, j;
    int status;
    ScNativePak *pak = context;
    if (!pak || sSession != pak || !pak->opened || !data ||
        (offset & 31) || (size & 31) || offset > SC_SAVE_NOTE_SIZE ||
        size > SC_SAVE_NOTE_SIZE - offset) return SC_SAVE_INVALID;
    for (i = 0; i < size; i += 32) {
        for (j = 0; j < 32; j++) block[j] = data[i + j];
        status = transfer(pak, offset + i, block, 32, 1);
        if (status != SC_SAVE_OK) return status;
    }
    return SC_SAVE_OK;
}

static int native_open(void *context, unsigned company, RlU32 game,
                       const RlU8 requested_name[16], const RlU8 requested_extension[4],
                       unsigned size, int create) {
    ScNativePak *pak = context;
    s32 note;
    RlU8 zero[32], name[16], extension[4], original_id[32];
    unsigned i;
    int status;
    if (!pak || sSession != pak || !requested_name || !requested_extension)
        return SC_SAVE_INVALID;
    pak->opened = 0;
    if (company != SC_SAVE_COMPANY || game != SC_SAVE_GAME || size != SC_SAVE_NOTE_SIZE)
        return SC_SAVE_INVALID;
    for (i = 0; i < 16; i++) if (requested_name[i] != sc_save_name[i]) return SC_SAVE_INVALID;
    for (i = 0; i < 4; i++) if (requested_extension[i] != sc_save_extension[i]) return SC_SAVE_INVALID;
    status = snapshot(pak);
    if (status != SC_SAVE_NOT_FOUND || !create) {
        pak->opened = status == SC_SAVE_OK;
        return status;
    }
    if (!pak->layout.free_notes || pak->layout.free_pages < SC_SAVE_NOTE_SIZE / 256)
        return SC_SAVE_FULL;
    bzero(&pak->pfs, sizeof(pak->pfs));
    pak->pfs.queue = &gSIEventMesgQueue;
    pak->pfs.channel = pak->controller;
    pak->pfs.status = PFS_INITIALIZED;
    pak->pfs.banks = 1;
    pak->pfs.version = pak->metadata[59];
    pak->pfs.inode_start_page = 5;
    pak->pfs.dir_size = 16;
    pak->pfs.inode_table = 8;
    pak->pfs.minode_table = 16;
    pak->pfs.dir_table = 24;
    for (i = 0; i < 32; i++) {
        pak->pfs.id[i] = original_id[i] = pak->metadata[i + 32];
        zero[i] = 0;
    }
    for (i = 0; i < 16; i++) name[i] = sc_save_name[i];
    for (i = 0; i < 4; i++) extension[i] = sc_save_extension[i];
    status = same_device(pak);
    if (status != SC_SAVE_OK) return status;
    __osPfsInodeCacheBank = 250;
    status = io_status(rlPfsAllocateFile(&pak->pfs, SC_SAVE_COMPANY, SC_SAVE_GAME,
                                        name, extension, SC_SAVE_NOTE_SIZE, &note));
    if (status != SC_SAVE_OK) return status;
    status = snapshot(pak);
    if (status != SC_SAVE_OK) return status == SC_SAVE_NOT_FOUND ? SC_SAVE_IO : status;
    for (i = 0; i < 32; i++)
        if (original_id[i] != pak->metadata[i + 32]) return SC_SAVE_DEVICE_CHANGED;
    if (pak->layout.note != (unsigned)note) return SC_SAVE_BAD_DATA;
    pak->opened = 1;
    for (i = 0; i < SC_SAVE_NOTE_SIZE; i += 32) {
        status = native_write(pak, i, zero, 32);
        if (status != SC_SAVE_OK) { pak->opened = 0; return status; }
    }
    for (i = 0; i < 32; i++) zero[i] = pak->metadata[768 + note * 32 + i];
    zero[8] = 2;
    zero[9] = zero[10] = zero[11] = 0;
    status = same_device(pak);
    if (status == SC_SAVE_OK)
        status = io_status(__osContRamWrite(&gSIEventMesgQueue, pak->controller, 24 + note, zero, 0));
    if (status != SC_SAVE_OK) pak->opened = 0;
    return status;
}

int sc_native_pak_bind(ScNativePak *pak, unsigned controller) {
    if (!pak || pak->bound || controller >= 4 || pak->binding_id == 0xFFFFFFFFU)
        return SC_SAVE_INVALID;
    pak->binding_id++;
    pak->controller = controller;
    pak->device.context = pak;
    pak->device.open = native_open;
    pak->device.read = native_read;
    pak->device.write = native_write;
    pak->opened = pak->session = pak->identified = pak->note_seen = 0;
    pak->bound = 1;
    return SC_SAVE_OK;
}

int sc_native_pak_begin(ScNativePak *pak) {
    if (!pak || !pak->bound || pak->session || sSession) return SC_SAVE_INVALID;
    sSession = pak;
    Cont_BlockEepromQueue();
    pak->session = 1;
    pak->opened = 0;
    return SC_SAVE_OK;
}

int sc_native_pak_end(ScNativePak *pak) {
    if (!pak || sSession != pak || !pak->session) return SC_SAVE_INVALID;
    pak->opened = pak->session = 0;
    Cont_NoBlockEepromQueue();
    sSession = NULL;
    return SC_SAVE_OK;
}

int sc_native_pak_release(ScNativePak *pak) {
    if (!pak || !pak->bound || pak->session) return SC_SAVE_INVALID;
    pak->bound = 0;
    pak->device.context = NULL;
    pak->device.open = NULL;
    pak->device.read = NULL;
    pak->device.write = NULL;
    return SC_SAVE_OK;
}
#endif
