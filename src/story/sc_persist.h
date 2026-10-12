#ifndef SC_PERSIST_H
#define SC_PERSIST_H
#if defined(STORY_SERVICE_CANDIDATE) && defined(STORY_NATIVE_UI)
#include "sc_module_arena.h"

/* Campaign-lifetime mapping of the story image (fragment 0xe9) in the
   Expansion Pak arena. Resident control; never stored in the mapped image.
   Order: acquire (load, relocate, seal rights) -> publish hooks/pins ->
   unpublish (no new entries/pins) -> owners retire and unpin, pool scopes
   holding SC callbacks pop -> release (unmap, end lease, free block).
   Any failure, or a crash latch, retains mapping and backing permanently. */
enum {
    SC_PERSIST_IDLE,
    SC_PERSIST_LOADING,
    SC_PERSIST_PUBLISHED,
    SC_PERSIST_DRAINING,
    SC_PERSIST_FAILED
};
enum {
    SC_PERSIST_PIN_HOOK,
    SC_PERSIST_PIN_SERVICE,
    SC_PERSIST_PIN_LIFETIME,
    SC_PERSIST_PIN_RETIREMENT,
    SC_PERSIST_PIN_KINDS
};
typedef struct {
    ScMaU32 rom_start, rom_end;   /* physical cart range: image then relocs */
    ScMaU32 text, data_end, rodata_end, ram; /* image offsets; rodata_end = relocs */
} ScPersistLayout;
typedef struct {
    ScMaArena *arena;
    ScMaHandle handle;
    ScMaLease lease;
    unsigned char *image;
    ScMaU32 bytes, phase, depth, generation;
    ScMaU32 pins[SC_PERSIST_PIN_KINDS];
    ScPersistLayout layout;
} ScPersist;

int sc_persist_acquire(ScMaArena *arena);
int sc_persist_unpublish(void);
/* 1 released; 0 pending (retry later, nothing changed); -1 failed/retained. */
int sc_persist_release(void);
int sc_persist_fail(void);
int sc_persist_pin(unsigned kind);
int sc_persist_unpin(unsigned kind);
typedef void (*ScPersistFn)(void);
/* Nonzero only while published and fn lies in the sealed executable span. */
int sc_persist_code(ScPersistFn fn);
/* Nonzero only while published and [p,p+n) lies in the mapped image. */
int sc_persist_owns(const void *p, ScMaU32 n);
/* True from acquire start until a successful release, including failure. */
int sc_persist_holds_fragment(void);
int sc_persist_call(int arg0, unsigned arg1, int *result);
/* Retirement entry for an existing pinned owner: allowed while published or
   draining only while pins[kind] is held, so unpublish closes admission without
   stranding owners that must run SC code to retire before they unpin. */
int sc_persist_call_pinned(unsigned kind, int arg0, unsigned arg1, int *result);
const ScPersist *sc_persist_state(void);

/* Native backend (sc_persist_native.c); HOST tests supply their own. */
int sc_persist_native_layout(ScPersistLayout *);
int sc_persist_native_unmapped(void);
/* DMA, authenticate header, relocate as 0xe9, zero tail, sync caches. */
int sc_persist_native_load(unsigned char *image, const ScPersistLayout *, ScMaU32 bytes);
/* 1 quiescent; 0 not yet; -1 permanent (crash latch). */
int sc_persist_native_quiescent(void);
/* 1 if no main-pool block callback or argument lies in the mapped image or
   the 0xe9 link window. */
int sc_persist_native_pool_clear(const unsigned char *image, ScMaU32 bytes);
int sc_persist_native_unmap(unsigned char *image, ScMaU32 bytes);
int sc_persist_native_enter(unsigned char *image, int arg0, unsigned arg1);
#endif
#endif
