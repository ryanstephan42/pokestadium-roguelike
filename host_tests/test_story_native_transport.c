#include "story_private_rom_native.h"
#include <assert.h>
int puts(const char *);

static void clear(void *object, unsigned bytes) {
    unsigned char *p = object;
    unsigned i;
    for (i = 0; i < bytes; ++i) p[i] = 0;
}

OSPiHandle __CartRomHandle;
u32 osMemSize = 0x800000;
static unsigned queued, cache_calls;
static OSMesg completion;

void osCreateMesgQueue(OSMesgQueue *q, OSMesg *buffer, s32 count) {
    assert(q && buffer && count == 1);
}

s32 osEPiStartDma(OSPiHandle *cart, OSIoMesg *request, s32 direction) {
    assert(cart == &__CartRomHandle && request && direction == OS_READ);
    assert(request->dramAddr == (void *)0x80400000 && request->size == 63584);
    assert(request->hdr.retQueue != 0);
    completion = request;
    ++queued;
    return 0; /* Injected OS queue acceptance; no actual DMA. */
}

s32 osRecvMesg(OSMesgQueue *queue, OSMesg *message, s32 flags) {
    assert(queue && message && flags == OS_MESG_NOBLOCK);
    *message = completion;
    return 0; /* Injected manager completion. */
}

void osWritebackDCacheAll(void) { ++cache_calls; }
void osInvalDCache(void *address, s32 bytes) {
    assert(address == (void *)0x80400000 && bytes == 63584);
    ++cache_calls;
}

int main(void) {
    OSPiHandle foreign;
    SprRequest request;
    const void *expected = 0, *received = 0;
    void *transport;
    clear(&__CartRomHandle, sizeof(__CartRomHandle));
    __CartRomHandle.type = DEVICE_TYPE_CART;
    __CartRomHandle.domain = PI_DOMAIN1;
    __CartRomHandle.baseAddress = 0xb0000000;
    foreign = __CartRomHandle;
    assert(spr_native_transport(&foreign) == 0);
    transport = spr_native_transport(&__CartRomHandle);
    assert(transport != 0);
    clear(&request, sizeof(request));
    request.ram = request.owner_base = 0x400000;
    request.owner_end = 0x400000 + 75584;
    request.bytes = 63584;
    request.rom_offset = request.rom_low = 0x206a770;
    request.rom_end = request.rom_low + request.bytes;
    request.installed_bytes = 0x800000;
    request.generation = 7;
    request.operation = 1;
    request.cold_route = request.exclusive_staging = request.unlocked =
        request.cart_handle = true;
    osMemSize = 0x400000;
    assert(!spr_native_prepare_cache(transport, &request));
    osMemSize = 0x800000;
    assert(spr_native_prepare_cache(transport, &request));
    assert(cache_calls == 2 && queued == 0);
    assert(spr_native_enqueue(transport, &request, &expected) == 0);
    assert(expected && queued == 1);
    assert(spr_native_transport(&__CartRomHandle) == 0);
    assert(spr_native_enqueue(transport, &request, &received) == -2);
    assert(spr_native_receive(transport, &received) == 1 && received == expected);
    puts("NATIVE TRANSPORT SOURCE PASSED: canonical identity; injected OS outcomes only");
    return 0;
}
