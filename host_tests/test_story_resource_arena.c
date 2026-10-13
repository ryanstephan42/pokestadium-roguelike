#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "src/story/sc_resource_arena.h"

static unsigned char backing[0x100000];
static ScMaArena arena;
static ScResourceArena resources;

int main(void) {
    ScMaHandle replacement;
    ScMaLease lease = {{0, 0}, 0, 0};
    void *first, *second;
    assert(!sc_resource_arena_open(&resources, &arena, 0x100));
    assert(sc_ma_init(&arena, 0x800000, backing, sizeof(backing)));
    assert(!sc_resource_arena_open(&resources, &arena, 17));
    assert(sc_resource_arena_open(&resources, &arena, 0x100));
    assert(!sc_resource_arena_open(&resources, &arena, 0x100));
    first = sc_resource_arena_take(&resources, 1);
    second = sc_resource_arena_take(&resources, 17);
    assert(first == backing);
    assert(second == backing + 16);
    assert(resources.used == 48);
    memset(first, 0xa5, 16);
    assert(!sc_resource_arena_take(&resources, 0xffffffffU));
    assert(!sc_resource_arena_take(&resources, 0));
    assert(!sc_resource_arena_take(&resources, 209));
    assert(resources.used == 48);
    assert(sc_resource_arena_take(&resources, 208) == backing + 48);
    assert(!sc_resource_arena_take(&resources, 1));
    assert(!sc_resource_arena_close(&resources, 0));
    assert(((unsigned char *)first)[0] == 0xa5);
    assert(!sc_ma_release(&arena, resources.handle));
    assert(!sc_ma_shutdown(&arena));
    assert(!sc_ma_map(&arena, &resources.lease, 0, 16, SC_MA_EXEC));
    assert(sc_resource_arena_close(&resources, 1));
    assert(!sc_resource_arena_close(&resources, 1));
    assert(!sc_resource_arena_take(&resources, 1));
    assert(sc_ma_alloc(&arena, 0x100, &replacement));
    assert(replacement.generation != resources.handle.generation);
    assert(sc_ma_data(&arena, replacement, &lease));
    assert(!sc_ma_map(&arena, &resources.lease, 0, 16, SC_MA_READ));
    assert(sc_ma_end(&arena, &lease));
    assert(sc_ma_release(&arena, replacement));
    assert(sc_resource_arena_open(&resources, &arena, sizeof(backing)));
    assert(sc_resource_arena_take(&resources, sizeof(backing)) == backing);
    assert(sc_resource_arena_close(&resources, 1));
    assert(sc_ma_shutdown(&arena));
    puts("Story resource arena: PASS (owned data, bounds, retirement, stale leases)");
    return 0;
}
