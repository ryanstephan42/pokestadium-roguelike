#ifdef STORY_NATIVE_PROBE
#include "sc_native_probe.h"

extern u8 sc_probe_catalog_ROM_START[], sc_probe_catalog_ROM_END[];
extern u8 sc_probe_catalog_VRAM[];

int sc_runtime_catalog_load(void) {
    static int loaded;
    OSIoMesg request;
    OSMesgQueue queue;
    OSMesg message;
    u32 size;
    if (loaded) return SC_OK;
    size = (u32)sc_probe_catalog_ROM_END - (u32)sc_probe_catalog_ROM_START;
    osCreateMesgQueue(&queue, &message, 1);
    request.hdr.pri = OS_MESG_PRI_NORMAL;
    request.hdr.retQueue = &queue;
    request.dramAddr = sc_probe_catalog_VRAM;
    request.devAddr = (u32)sc_probe_catalog_ROM_START;
    request.size = size;
    osInvalDCache(sc_probe_catalog_VRAM, size);
    if (osEPiStartDma(osCartRomInit(), &request, OS_READ) != 0 ||
        osRecvMesg(&queue, NULL, OS_MESG_BLOCK) != 0 ||
        !sc_catalog_valid(&sc_catalog)) {
        osSyncPrintf("story probe: resident catalog DMA/validation failed\n");
        return SC_INVALID;
    }
    loaded = 1;
    return SC_OK;
}
#endif
