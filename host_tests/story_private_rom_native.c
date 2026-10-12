/* Noninstalled ABI bridge; not part of any production Makefile. */
#include "story_private_rom_native.h"

typedef struct {
    OSIoMesg request;
    OSMesgQueue queue;
    OSMesg cell;
    OSPiHandle *cart;
    bool attempted;
    bool completed, failed;
    uint32_t lifetime;
    SprRequest identity;
} SprNativeTransport;

typedef char SprIoMesgAbi[(sizeof(OSIoMesg) == 24) ? 1 : -1];
typedef char SprQueueAbi[(sizeof(OSMesgQueue) == 24) ? 1 : -1];
typedef char SprTransportAbi[(sizeof(SprNativeTransport) == 108) ? 1 : -1];
typedef char SprRequestAbi[(sizeof(SprRequest) == 44) ? 1 : -1];
typedef char SprLoaderAbi[(sizeof(SprLoader) == 100) ? 1 : -1];

static SprNativeTransport transport;
extern OSPiHandle __CartRomHandle;

void *spr_native_transport(OSPiHandle *cart) {
    if (transport.attempted || transport.failed || cart != &__CartRomHandle) return 0;
    transport.cart = cart;
    return &transport;
}

static bool native_valid(const SprNativeTransport *t, const SprRequest *r) {
    return spr_preflight(r) && osMemSize == 0x800000 &&
        t->cart == &__CartRomHandle &&
        t->cart->type == DEVICE_TYPE_CART && t->cart->domain == PI_DOMAIN1 &&
        t->cart->baseAddress == 0xb0000000;
}

int spr_native_enqueue(void *ctx, const SprRequest *r, const void **expected) {
    SprNativeTransport *t = ctx;
    int result;
    if (t != &transport || t->attempted || t->failed || t->lifetime == UINT32_MAX) return -2;
    if (!native_valid(t, r)) return -1;
    t->attempted = true;
    ++t->lifetime;
    t->identity = *r;
    osCreateMesgQueue(&t->queue, &t->cell, 1);
    t->request.hdr.pri = OS_MESG_PRI_NORMAL;
    t->request.hdr.retQueue = &t->queue;
    t->request.dramAddr = (void *)(r->ram | 0x80000000);
    t->request.devAddr = r->rom_offset;
    t->request.size = r->bytes;
    *expected = &t->request;
    result = osEPiStartDma(t->cart, &t->request, OS_READ);
    if (result != 0) t->failed = true;
    return result;
}

int spr_native_receive(void *ctx, const void **message) {
    SprNativeTransport *t = ctx;
    OSMesg received;
    if (t != &transport || !t->attempted || t->completed || t->failed) return -1;
    if (osRecvMesg(&t->queue, &received, OS_MESG_NOBLOCK) == -1) return 0;
    *message = received;
    if (received == &t->request) t->completed = true;
    else t->failed = true;
    return 1;
}

bool spr_native_prepare_cache(void *ctx, const SprRequest *r) {
    SprNativeTransport *t = ctx;
    if (t != &transport || t->attempted || t->failed || !native_valid(t, r)) return false;
    /* Large invalidation otherwise discards unrelated dirty index lines. */
    osWritebackDCacheAll();
    osInvalDCache((void *)(r->ram | 0x80000000), r->bytes);
    return true;
}

bool spr_native_retire(void *ctx, const SprRequest *r) {
    SprNativeTransport *t = ctx;
    if (t != &transport || !r || !t->attempted || !t->completed || t->failed ||
        !native_valid(t, r) || r->generation != t->identity.generation ||
        r->operation != t->identity.operation || r->ram != t->identity.ram ||
        r->owner_base != t->identity.owner_base || r->owner_end != t->identity.owner_end ||
        r->rom_offset != t->identity.rom_offset || r->rom_end != t->identity.rom_end ||
        r->bytes != t->identity.bytes) return false;
    t->attempted = t->completed = false;
    return true;
}
