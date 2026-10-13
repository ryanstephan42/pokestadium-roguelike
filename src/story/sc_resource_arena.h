#ifndef SC_RESOURCE_ARENA_H
#define SC_RESOURCE_ARENA_H

#include "sc_module_arena.h"

typedef struct ScResourceArena {
    ScMaArena *arena;
    ScMaHandle handle;
    ScMaLease lease;
    unsigned char *bytes;
    ScMaU32 capacity, used;
    int active;
} ScResourceArena;

/* Zero-initialized control. Persistent data only; never recycle live pixels. */
int sc_resource_arena_open(ScResourceArena *, ScMaArena *, ScMaU32 capacity);
void *sc_resource_arena_take(ScResourceArena *, ScMaU32 bytes);
int sc_resource_arena_close(ScResourceArena *, int tasks_retired);

#endif
