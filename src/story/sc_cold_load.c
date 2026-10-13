#ifdef STORY_NATIVE_PROBE
#include "sc_cold.h"
#include "src/3FB0.h"
#include "src/memory.h"
#include "src/memmap.h"
#include "sc_runtime_probe.h"
#if defined(STORY_SERVICE_CANDIDATE) && defined(STORY_NATIVE_UI)
#include "sc_persist.h"
#define SC_PERSIST_HELD() sc_persist_holds_fragment()
#else
#define SC_PERSIST_HELD() 0
#endif
#ifdef STORY_AI_PROTOTYPE
#include "sc_ai_runtime.h"
#include "sc_ai_lease.h"
#endif

extern u8 fragmentSC_ROM_START[], fragmentSC_relocs_ROM_END[];
extern u8 fragmentSC_HDR_SIZE_IN_RAM[];
static int calling;

static int cold_invoke(ScColdRequest *request, u8 *rom_start, u8 *rom_end, unsigned ram_size) {
    FragmentEntry entry;
    unsigned rom_size, required;
    int status;
#ifdef STORY_AI_PROTOTYPE
    unsigned token;
#endif
    /* A persistent story mapping owns 0xe9 until release; never remap it. */
    if (!request || calling || gScProbeActive || SC_PERSIST_HELD()) {
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
    gScProbe.map_before = Memmap_GetFragmentBaseVaddr(0xe9);
    gScProbe.map_before_size = Memmap_GetFragmentSize(0xe9);
    main_pool_push_state(0x53434c44);
#ifdef STORY_AI_PROTOTYPE
    token = sc_ai_load_begin(SC_AI_LEASE_COLD);
#endif
    entry = (FragmentEntry)func_80004454(0xe9, rom_start, rom_end);
    gScProbe.map_loaded = Memmap_GetFragmentBaseVaddr(0xe9);
    gScProbe.map_loaded_size = Memmap_GetFragmentSize(0xe9);
    gScProbe.cold_address = (unsigned)entry;
    if (!entry) {
        osSyncPrintf("story probe: cold overlay load failed\n");
        status = 0x100 + SC_INVALID;
    } else {
#ifdef STORY_AI_PROTOTYPE
        sc_ai_load_end(SC_AI_LEASE_COLD, token, (Fragment *)entry);
        status = sc_ai_dispatch(SC_AI_LEASE_COLD, token, 0, request);
        sc_ai_unload_begin(SC_AI_LEASE_COLD, token);
#else
        status = entry(0, (unsigned)request);
#endif
    }
#ifdef STORY_AI_PROTOTYPE
    if (!entry) sc_ai_fatal(22);
#endif
    /* No cold code frame or pointer survives this pool pop. */
    main_pool_pop_state(0x53434c44);
#ifdef STORY_AI_PROTOTYPE
    sc_ai_unload_end(SC_AI_LEASE_COLD, token);
#endif
    gScProbe.map_after = Memmap_GetFragmentBaseVaddr(0xe9);
    gScProbe.map_after_size = Memmap_GetFragmentSize(0xe9);
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
