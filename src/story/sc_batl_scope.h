#ifndef SC_BATL_SCOPE_H
#define SC_BATL_SCOPE_H

#include "src/fragments/62/fragment62.h"
#include "sc_batl_loader.h"

typedef struct { u32 generation, ticket; } ScBatPermit;
typedef struct {
    u32 generation, map_generation;
    uintptr_t native_base;
    size_t native_size;
    const f32 *clock;
    unk_D_84390010 *roots[2];
    unk_D_800AE540_0004 *groups[2];
    const void *registrations[2], *metadata[2];
    u32 counts[2];
    unk_D_86002F58_004_000_010 *workers[2];
} ScBatImports;

/* Private service interfaces; never carried by campaign/UI requests. */
const f32 *sc_batl_clock_source(void);
int sc_batl_scope_open(void);
int sc_batl_scope_borrow(ScBatPermit *, ScBatImports *);
int sc_batl_scope_check(const ScBatPermit *);
int sc_batl_scope_retire(ScBatPermit *);
int sc_batl_scope_close(void);
void sc_batl_scope_fail(void);
void sc_batl_scope_require(int);
int sc_batl_scope_map_change(void);
int sc_batl_worker_admit(unk_D_86002F58_004_000_010 *);
int sc_batl_model_admit(unk_D_86002F58_004_000_010 *, u16);
int sc_batl_scope_drain(const ScBatPermit *);
u32 Memmap_GetStoryBattleMapGeneration(void);
/* Loader job completion admission; failure is terminal through sc_batl_job_fatal. */
struct sp18_func_800198E4;
int sc_batl_job_admit(unk_D_86002F58_004_000_010 *, struct sp18_func_800198E4 *);
void sc_batl_job_fatal(u32);
enum { SC_BATL_JOB_REFUSED = 1, SC_BATL_JOB_UNPUBLISHED = 2, SC_BATL_FX_SEND = 3, SC_BATL_FX_REQUEST = 4,
       SC_BATL_FX_PUBLISH = 5, SC_BATL_FX_STALE = 6 };
int sc_batl_fx_send(unk_D_86002F58_004_000_010 *);
int sc_batl_fx_request(void);
int sc_batl_fx_publish(int);
void sc_batl_fx_note(s32, s32);
int sc_batl_fx_admit(s32, s32);


#endif
