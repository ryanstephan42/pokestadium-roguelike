#ifndef SC_NATIVE_MEMORY_H
#define SC_NATIVE_MEMORY_H
#ifdef STORY_NATIVE_UI
#include "sc_module_arena.h"
#include "sc_native_loop.h"

typedef struct ScNativeCampaignStorage {
    ScUi ui;
    ScUiBattleRequest request, submitted;
    ScUiDriver driver;
    ScNativeUi native_ui;
    ScNativePak pak;
    ScNativeCampaignEvent event;
    ScState settled;
    ScCombat progression;
} ScNativeCampaignStorage;

typedef struct ScNativeMemory {
    ScMaArena *arena;
    ScMaHandle allocation;
    ScMaLease lease;
    ScNativeCampaignStorage *storage;
    ScNativeLoop *loop;
    ScNativeDisplay *display;
    const ScCatalog *catalog;
    RlU32 seed, offset, bytes;
    int live, failed;
} ScNativeMemory;

/* Zeroed private controls; loop/display are caller-retained outside the lease
   freed by release (close still accesses them after its owner callback).
   The same arena must also own the callable service.
   No GPU admission or retirement is inferred from allocation/address shape. */
int sc_native_memory_acquire(ScNativeMemory *, ScMaArena *, const ScCatalog *,
                             RlU32 seed, ScNativeLoop *, ScNativeDisplay *,
                             ScNativeCampaignBacking *);
int sc_native_memory_validate(ScNativeMemory *, const ScNativeCampaignBacking *);
/* The owner callback must prove actual scheduler/VI retirement and admission
   exclusion remains held, not just a value flag or completed worker task. */
int sc_native_memory_release(ScNativeMemory *, const ScNativeCampaignBacking *,
    int (*retired)(void *, const ScNativeCampaignBacking *), void *);
#endif
#endif
