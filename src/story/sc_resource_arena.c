#ifdef STORY_NATIVE_UI
#include "sc_resource_arena.h"

int sc_resource_arena_open(ScResourceArena *r, ScMaArena *arena,
                           ScMaU32 capacity) {
    if (!r || r->active || r->lease.ticket || !arena || !capacity ||
        (capacity & 15)) return 0;
    if (!sc_ma_alloc(arena, capacity, &r->handle)) return 0;
    r->arena = arena;
    r->capacity = capacity;
    r->used = 0;
    r->active = 1;
    if (!sc_ma_data(arena, r->handle, &r->lease)) {
        if (sc_ma_release(arena, r->handle)) r->active = 0;
        return 0;
    }
    r->bytes = sc_ma_map(arena, &r->lease, 0, capacity,
                         SC_MA_READ | SC_MA_WRITE);
    if (!r->bytes) {
        sc_resource_arena_close(r, 1);
        return 0;
    }
    return 1;
}

void *sc_resource_arena_take(ScResourceArena *r, ScMaU32 bytes) {
    ScMaU32 size;
    void *result;
    if (!r || !r->active || !r->lease.ticket || !r->bytes || !bytes ||
        bytes > 0xfffffff0U) return 0;
    size = (bytes + 15) & ~15U;
    if (r->used > r->capacity || size > r->capacity - r->used) return 0;
    result = sc_ma_map(r->arena, &r->lease, r->used, size,
                       SC_MA_READ | SC_MA_WRITE);
    if (!result) return 0;
    r->used += size;
    return result;
}

int sc_resource_arena_close(ScResourceArena *r, int tasks_retired) {
    if (!r || !r->active || !tasks_retired) return 0;
    if (r->lease.ticket && !sc_ma_end(r->arena, &r->lease)) return 0;
    if (!sc_ma_release(r->arena, r->handle)) return 0;
    r->active = 0;
    r->arena = 0;
    r->bytes = 0;
    r->capacity = r->used = 0;
    return 1;
}
#endif
