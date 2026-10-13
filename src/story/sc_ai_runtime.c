#ifdef STORY_AI_PROTOTYPE
#include "sc_ai_runtime.h"
#include "sc_ai_lease.h"
#include "sc_runtime_probe.h"
#include "src/3FB0.h"
#include "src/memory.h"

static ScAiLease lease;
static unsigned phase, ai_token;
enum { IDLE, LOADING, CALLABLE, REVOKED, CLEARING };
volatile ScAiFatal gScAiFatal;
extern u8 fragmentAI_ROM_START[], fragmentAI_relocs_ROM_END[];
extern u8 fragmentAI_HDR_SIZE_IN_RAM[];

void sc_ai_fatal(unsigned code) {
    if (!gScAiFatal.code) {
        gScAiFatal.code = code;
        gScAiFatal.owner = lease.owner;
        gScAiFatal.generation = lease.generation;
        gScAiFatal.address = lease.address;
        gScAiFatal.size = lease.size;
    }
    gScSixControl.error = 0xA100 + code;
    gScSixControl.role = SC_SIX_INVALID;
    osSyncPrintf("story AI prototype fatal %u owner %u token %u\n",
                 code, lease.owner, lease.generation);
    osStopThread(NULL);
}

unsigned sc_ai_load_begin(unsigned owner) {
    unsigned token = 0;
    if (phase != IDLE || sc_ai_lease_acquire(&lease, owner,
            Memmap_GetFragmentBaseVaddr(SC_AI_ID),
            Memmap_GetFragmentSize(SC_AI_ID), &token))
        sc_ai_fatal(1);
    phase = LOADING;
    return token;
}

void sc_ai_loader_admit(unsigned id) {
    if (id == SC_AI_ID && (phase != LOADING || !lease.owner ||
                           lease.address || lease.error))
        sc_ai_fatal(2);
}

void sc_ai_map_set(unsigned id, unsigned base, unsigned size) {
    if (id == SC_AI_ID) {
        sc_ai_loader_admit(id);
        if (sc_ai_lease_bind(&lease, lease.owner, lease.generation, base, size))
            sc_ai_fatal(3);
    }
}

void sc_ai_load_end(unsigned owner, unsigned token, Fragment *fragment) {
    if (phase != LOADING || !fragment ||
        (unsigned)fragment != lease.address ||
        sc_ai_lease_publish(&lease, owner, token,
            Memmap_GetFragmentBaseVaddr(SC_AI_ID), Memmap_GetFragmentSize(SC_AI_ID)))
        sc_ai_fatal(4);
    phase = CALLABLE;
}

unsigned sc_ai_callback_tag(unsigned id) {
    if (id != SC_AI_ID) return 0x81000000 + id;
    sc_ai_loader_admit(id);
    return 0x81000000 | (lease.generation << 8) | id;
}

void sc_ai_reference_admit(unsigned id) {
    if (id == SC_AI_ID && (!lease.address || lease.error ||
        (phase != LOADING && (phase != CALLABLE || !lease.in_call))))
        sc_ai_fatal(5);
}

int sc_ai_dispatch(unsigned owner, unsigned token, int command, void *request) {
    int status;
    int (*entry)(int, void *);
    if (phase != CALLABLE ||
        sc_ai_lease_enter(&lease, owner, token,
            Memmap_GetFragmentBaseVaddr(SC_AI_ID), Memmap_GetFragmentSize(SC_AI_ID)))
        sc_ai_fatal(6);
    entry = (int (*)(int, void *))lease.address;
    status = entry(command, request);
    if (sc_ai_lease_leave(&lease, owner, token)) sc_ai_fatal(7);
    return status;
}

void sc_ai_unload_begin(unsigned owner, unsigned token) {
    if (phase != CALLABLE || sc_ai_lease_revoke(&lease, owner, token))
        sc_ai_fatal(8);
    phase = REVOKED;
}

int sc_ai_callback_clear(unsigned base, unsigned tag) {
    unsigned token;
    if ((tag & 255) != SC_AI_ID) return 0;
    token = (tag >> 8) & 65535;
    if ((tag & 0xFF000000) != 0x81000000 || phase != REVOKED ||
        sc_ai_lease_free_admit(&lease, lease.owner, token, base,
            Memmap_GetFragmentBaseVaddr(SC_AI_ID), Memmap_GetFragmentSize(SC_AI_ID)))
        sc_ai_fatal(9);
    phase = CLEARING;
    Memmap_ClearFragmentMemmap(SC_AI_ID);
    phase = REVOKED;
    return 1;
}

void sc_ai_map_clear(unsigned id) {
    if (id == SC_AI_ID && phase != CLEARING) sc_ai_fatal(10);
}

void sc_ai_unload_end(unsigned owner, unsigned token) {
    if (phase != REVOKED || sc_ai_lease_release(&lease, owner, token,
            Memmap_GetFragmentBaseVaddr(SC_AI_ID), Memmap_GetFragmentSize(SC_AI_ID)))
        sc_ai_fatal(11);
    phase = IDLE;
}

void sc_ai_battle_load(void) {
    Fragment *fragment;
    ScAiRequest request;
    unsigned rom_size = fragmentAI_relocs_ROM_END - fragmentAI_ROM_START;
    unsigned ram_size = (unsigned)fragmentAI_HDR_SIZE_IN_RAM;
    unsigned required = ALIGN16(rom_size > ram_size ? rom_size : ram_size) +
        2 * sizeof(MainPoolBlock);
    if (ai_token || required > main_pool_get_available()) sc_ai_fatal(12);
    ai_token = sc_ai_load_begin(SC_AI_LEASE_AI);
    fragment = (Fragment *)func_80004454(SC_AI_ID, fragmentAI_ROM_START,
                                       fragmentAI_relocs_ROM_END);
    sc_ai_load_end(SC_AI_LEASE_AI, ai_token, fragment);
    bzero(&request, sizeof(request));
    request.generation = ai_token;
    if (sc_ai_dispatch(SC_AI_LEASE_AI, ai_token, SC_AI_INIT, &request))
        sc_ai_fatal(13);
}

void sc_ai_battle_unload(void) {
    ScAiRequest request;
    if (!ai_token) sc_ai_fatal(14);
    bzero(&request, sizeof(request));
    request.generation = ai_token;
    if (sc_ai_dispatch(SC_AI_LEASE_AI, ai_token, SC_AI_SHUTDOWN, &request))
        sc_ai_fatal(15);
    sc_ai_unload_begin(SC_AI_LEASE_AI, ai_token);
}

void sc_ai_battle_popped(void) {
    sc_ai_unload_end(SC_AI_LEASE_AI, ai_token);
    ai_token = 0;
}

unsigned sc_ai_battle_token(void) { return ai_token; }
#endif
