#ifndef SC_NATIVE_RESOURCES_H
#define SC_NATIVE_RESOURCES_H
#ifdef STORY_NATIVE_UI
#include "src/3FB0.h"
#include "sc_module_arena.h"
#define SC_NATIVE_RESOURCE_BYTES 697088U
int sc_native_resources_acquire(BinArchive **icons, char ***moves, char ***species);
int sc_native_resources_acquire_upper(ScMaArena *, BinArchive **, char ***, char ***);
int sc_native_resources_retire(int tasks_retired);
u32 sc_native_resources_outstanding(void);
#endif
#endif
