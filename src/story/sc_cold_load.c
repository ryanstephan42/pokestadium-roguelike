#ifdef STORY_NATIVE_PROBE
#include "sc_cold.h"
#include "src/3FB0.h"
#include "src/memory.h"
#include "sc_runtime_probe.h"

extern u8 fragmentSC_ROM_START[], fragmentSC_relocs_ROM_END[];
extern u8 fragmentSC_HDR_SIZE_IN_RAM[];
static int calling;

static int cold_invoke(ScColdRequest *request, u8 *rom_start, u8 *rom_end, unsigned ram_size) {
    FragmentEntry entry;
    unsigned rom_size, required;
    int status;
    if (!request || calling || gScProbeActive) {
        osSyncPrintf("story probe: invalid/reentrant cold call\n");
        return 0x100 + SC_INVALID;
    }
    rom_size = rom_end - rom_start;
    if (!rom_size || rom_size >= 0x100000 || !ram_size || ram_size >= 0x100000) {
        osSyncPrintf("story probe: invalid cold overlay bounds\n");
        return 0x100 + SC_INVALID;
    }
    required = ALIGN16(rom_size > ram_size ? rom_size : ram_size) +
        ALIGN16(0x18) + ALIGN16(sizeof(MainPoolState)) + sizeof(MainPoolBlock) * 2;
    if (required > main_pool_get_available()) {
        osSyncPrintf("story probe: cold overlay allocation unavailable\n");
        return 0x100 + SC_INVALID;
    }
    calling = 1;
    gScProbe.cold_busy = 1;
    gScProbe.cold_calls++;
    gScProbe.cold_before = main_pool_get_available();
    gScProbe.cold_size = ram_size;
    main_pool_push_state(0x53434c44);
    entry = (FragmentEntry)func_80004454(0xe9, rom_start, rom_end);
    gScProbe.cold_address = (unsigned)entry;
    if (!entry) {
        osSyncPrintf("story probe: cold overlay load failed\n");
        status = 0x100 + SC_INVALID;
    } else {
        status = entry(0, (unsigned)request);
    }
    /* No cold code frame or pointer survives this pool pop. */
    main_pool_pop_state(0x53434c44);
    gScProbe.cold_address = 0;
    gScProbe.cold_busy = 0;
    gScProbe.cold_after = main_pool_get_available();
    calling = 0;
    return status;
}

int sc_cold_call(ScColdRequest *request) {
    return cold_invoke(request, fragmentSC_ROM_START, fragmentSC_relocs_ROM_END,
                        (unsigned)fragmentSC_HDR_SIZE_IN_RAM);
}
#endif
