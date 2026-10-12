#include "src/story/sc_native_memory.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks, retire_calls;
static int retirement = SC_PENDING;
static ScNativeMemory *tamper_memory;
static ScMaHandle tamper_allocation;
#define CHECK(c) do { ++checks; if (!(c)) { \
    fprintf(stderr, "%d: %s\n", __LINE__, #c); exit(1); } } while (0)

void osSyncPrintf(const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    vfprintf(stderr, format, arguments);
    va_end(arguments);
}

static int retired(void *context, const ScNativeCampaignBacking *backing) {
    CHECK(context == &retirement && backing->lease_id);
    ++retire_calls;
    if (tamper_memory) tamper_memory->allocation = tamper_allocation;
    return retirement;
}

int main(void) {
    ScMaArena arena = {0};
    ScNativeMemory memory = {0}, refused = {0};
    ScNativeLoop loop = {0};
    ScNativeDisplay display = {0};
    ScNativeCampaignBacking backing, stale, wrong;
    ScMaHandle service, filler, allocation;
    ScMaLease service_lease = {{0, 0}, 0, 0};
    ScCatalog catalog;
    RlU8 *buffer = malloc(0x3da7e0U);
    unsigned i, tickets;
    CHECK(buffer);
    memset(buffer, 0xa5, 0x3da7e0U);
    memset(&catalog, 0, sizeof(catalog));
    CHECK(sc_ma_init(&arena, 0x800000U, buffer, 0x3da7e0U));
    CHECK(sc_ma_alloc(&arena, 75616U, &service));
    CHECK(sc_ma_staging(&arena, service, &service_lease));
    CHECK(sc_native_memory_acquire(&memory, &arena, &catalog, 42, &loop, &display, &backing) == SC_OK);
    CHECK(sc_native_memory_validate(&memory, &backing) == SC_OK);
    CHECK((sc_ma_address(&arena, memory.allocation) + memory.offset) % 64 == 0);
    CHECK(backing.framebuffer[1].pixels - backing.framebuffer[0].pixels ==
          SC_NATIVE_FRAMEBUFFER_BYTES);
    CHECK(backing.ui == &memory.storage->ui && backing.event == &memory.storage->event);
    CHECK(!sc_ma_map(&arena, &memory.lease, 0, 4, SC_MA_EXEC));
    for (i = 0; i < sizeof(*memory.storage); i++)
        CHECK(((RlU8 *)memory.storage)[i] == 0);
    CHECK(backing.framebuffer[0].pixels[0] == 0xa5);
    CHECK(backing.framebuffer[1].pixels[SC_NATIVE_FRAMEBUFFER_BYTES - 1] == 0xa5);
    wrong = backing; ++wrong.seed;
    CHECK(sc_native_memory_validate(&memory, &wrong) == SC_INVALID);
    wrong = backing; ++wrong.lease_id;
    CHECK(sc_native_memory_validate(&memory, &wrong) == SC_INVALID);
    wrong = backing; wrong.request = &memory.storage->submitted;
    CHECK(sc_native_memory_validate(&memory, &wrong) == SC_INVALID);
    wrong = backing; ++wrong.framebuffer[0].pixels;
    CHECK(sc_native_memory_validate(&memory, &wrong) == SC_INVALID);
    wrong = backing; --wrong.framebuffer[1].bytes;
    CHECK(sc_native_memory_validate(&memory, &wrong) == SC_INVALID);
    allocation = memory.allocation;
    memory.allocation = service;
    CHECK(sc_native_memory_validate(&memory, &backing) == SC_INVALID);
    CHECK(sc_native_memory_release(&memory, &backing, retired, &retirement) == SC_INVALID);
    CHECK(!retire_calls && service_lease.ticket && memory.lease.ticket);
    memory.allocation = allocation;
    ++memory.allocation.generation;
    CHECK(sc_native_memory_validate(&memory, &backing) == SC_INVALID);
    memory.allocation = allocation;
    tickets = arena.next_ticket;
    CHECK(sc_native_memory_acquire(&memory, &arena, &catalog, 42, &loop, &display, &wrong) == SC_INVALID);
    for (i = 0; i < sizeof(wrong); i++) CHECK(((RlU8 *)&wrong)[i] == 0);
    CHECK(arena.next_ticket == tickets);
    display.acquired = 1;
    CHECK(sc_native_memory_release(&memory, &backing, retired, &retirement) == SC_PENDING);
    CHECK(!retire_calls);
    display.acquired = 0;
    CHECK(sc_native_memory_release(&memory, &backing, retired, &retirement) == SC_PENDING);
    CHECK(memory.live && memory.lease.ticket && retire_calls == 1);
    retirement = SC_INVALID;
    CHECK(sc_native_memory_release(&memory, &backing, retired, &retirement) == SC_INVALID);
    CHECK(memory.live && sc_native_memory_validate(&memory, &backing) == SC_OK);
    loop.acquired = loop.prepared = 1;
    CHECK(sc_native_memory_release(&memory, &backing, retired, &retirement) == SC_PENDING);
    loop.exited = 1;
    retirement = SC_OK;
    tamper_memory = &memory;
    tamper_allocation = service;
    CHECK(sc_native_memory_release(&memory, &backing, retired, &retirement) == SC_INVALID);
    CHECK(memory.live && memory.lease.ticket);
    CHECK(sc_ma_map(&arena, &service_lease, 0, 4, SC_MA_READ));
    memory.allocation = allocation;
    tamper_memory = NULL;
    stale = backing;
    CHECK(sc_native_memory_release(&memory, &backing, retired, &retirement) == SC_OK);
    CHECK(!memory.live && !memory.storage);
    CHECK(sc_native_memory_validate(&memory, &stale) == SC_INVALID);
    loop.acquired = loop.prepared = 0;
    CHECK(sc_native_memory_acquire(&memory, &arena, &catalog, 43, &loop, &display, &backing) == SC_OK);
    CHECK(backing.lease_id != stale.lease_id);
    CHECK(sc_native_memory_validate(&memory, &stale) == SC_INVALID);
    CHECK(sc_native_memory_release(&memory, &backing, retired, &retirement) == SC_OK);
    CHECK(sc_ma_alloc(&arena, 0x300000U, &filler));
    memset(&wrong, 0xff, sizeof(wrong));
    CHECK(sc_native_memory_acquire(&refused, &arena, &catalog, 1, &loop, &display, &wrong) == SC_FULL);
    CHECK(!refused.live && !refused.failed);
    for (i = 0; i < sizeof(wrong); i++) CHECK(((RlU8 *)&wrong)[i] == 0);
    CHECK(sc_ma_release(&arena, filler));
    arena.next_ticket = 0xffffffffU;
    memset(&wrong, 0xff, sizeof(wrong));
    CHECK(sc_native_memory_acquire(&refused, &arena, &catalog, 1, &loop, &display, &wrong) == SC_UNAVAILABLE);
    CHECK(!refused.live && !refused.failed);
    for (i = 0; i < sizeof(wrong); i++) CHECK(((RlU8 *)&wrong)[i] == 0);
    CHECK(sc_ma_end(&arena, &service_lease));
    CHECK(sc_ma_release(&arena, service));
    CHECK(sc_ma_shutdown(&arena));
    free(buffer);
    printf("NATIVE MEMORY HOST PASSED: %u checks; retirement callback simulated, not GPU proof\n", checks);
    return 0;
}
