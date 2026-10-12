#if defined(STORY_SERVICE_CANDIDATE) && defined(STORY_NATIVE_UI)
#include "sc_persist.h"

#define SC_PERSIST_WINDOW 0x100000U

static ScPersist persist;

static ScMaU32 align(ScMaU32 v, ScMaU32 a) { return (v + a - 1) & ~(a - 1); }

static int layout_valid(const ScPersistLayout *l) {
    ScMaU32 rom;
    if (l->rom_end <= l->rom_start) return 0;
    rom = l->rom_end - l->rom_start;
    return l->text && !(l->text & 31) && l->text <= l->data_end &&
        l->data_end <= l->rodata_end && !(l->data_end & 15) &&
        !(l->rodata_end & 15) && rom > l->rodata_end && !(rom & 15) &&
        l->ram > l->rodata_end && !(l->ram & 15) &&
        rom < SC_PERSIST_WINDOW && l->ram < SC_PERSIST_WINDOW;
}

static ScMaU32 spans(const ScPersistLayout *l, ScMaU32 bytes, ScMaSpan *s) {
    ScMaU32 n = 0;
    s[n].offset = 0; s[n].size = l->text; s[n++].rights = SC_MA_READ | SC_MA_EXEC;
    if (l->data_end > l->text) {
        s[n].offset = l->text; s[n].size = l->data_end - l->text;
        s[n++].rights = SC_MA_READ | SC_MA_WRITE;
    }
    if (l->rodata_end > l->data_end) {
        s[n].offset = l->data_end; s[n].size = l->rodata_end - l->data_end;
        s[n++].rights = SC_MA_READ;
    }
    s[n].offset = l->rodata_end; s[n].size = bytes - l->rodata_end;
    s[n++].rights = SC_MA_READ | SC_MA_WRITE;
    return n;
}

static int fail_retain(void) {
    if (persist.lease.ticket) sc_ma_fail(persist.arena, &persist.lease);
    persist.phase = SC_PERSIST_FAILED;
    return 0;
}

int sc_persist_acquire(ScMaArena *arena) {
    ScPersist *p = &persist;
    ScMaSpan s[4];
    ScMaU32 n, rom, bytes, i;
    unsigned char *image;
    if (!arena || p->phase != SC_PERSIST_IDLE || p->depth ||
        p->generation == 0xffffffffU) return 0;
    for (i = 0; i < SC_PERSIST_PIN_KINDS; ++i) if (p->pins[i]) return 0;
    if (!sc_persist_native_layout(&p->layout) || !layout_valid(&p->layout) ||
        !sc_persist_native_unmapped()) return 0;
    rom = p->layout.rom_end - p->layout.rom_start;
    bytes = align(rom > p->layout.ram ? rom : p->layout.ram, 32);
    if (!sc_ma_alloc(arena, bytes, &p->handle)) return 0;
    p->arena = arena;
    p->lease.ticket = 0;
    if (!sc_ma_staging(arena, p->handle, &p->lease)) {
        sc_ma_release(arena, p->handle);
        return 0;
    }
    image = sc_ma_map(arena, &p->lease, 0, bytes, SC_MA_READ | SC_MA_WRITE);
    if (!image) {
        if (!sc_ma_end(arena, &p->lease) || !sc_ma_release(arena, p->handle))
            return fail_retain();
        return 0;
    }
    /* From here the fragment map may name this block: failures retain. */
    p->phase = SC_PERSIST_LOADING;
    p->image = image;
    p->bytes = bytes;
    if (!sc_persist_native_load(image, &p->layout, bytes)) return fail_retain();
    n = spans(&p->layout, bytes, s);
    if (!sc_ma_seal(arena, &p->lease, s, n) ||
        sc_ma_map(arena, &p->lease, 0, p->layout.text, SC_MA_READ | SC_MA_EXEC) != image)
        return fail_retain();
    ++p->generation;
    p->phase = SC_PERSIST_PUBLISHED;
    return 1;
}

int sc_persist_unpublish(void) {
    if (persist.phase != SC_PERSIST_PUBLISHED) return 0;
    persist.phase = SC_PERSIST_DRAINING;
    return 1;
}

int sc_persist_release(void) {
    ScPersist *p = &persist;
    ScMaU32 i;
    int q;
    if (p->phase == SC_PERSIST_FAILED) return -1;
    if (p->phase != SC_PERSIST_DRAINING || p->depth) return 0;
    for (i = 0; i < SC_PERSIST_PIN_KINDS; ++i) if (p->pins[i]) return 0;
    q = sc_persist_native_quiescent();
    if (q < 0) { fail_retain(); return -1; }
    if (!q || !sc_persist_native_pool_clear(p->image, p->bytes)) return 0;
    if (!sc_persist_native_unmap(p->image, p->bytes) ||
        !sc_ma_end(p->arena, &p->lease) || !sc_ma_release(p->arena, p->handle)) {
        fail_retain();
        return -1;
    }
    p->image = 0;
    p->bytes = 0;
    p->phase = SC_PERSIST_IDLE;
    return 1;
}

int sc_persist_fail(void) {
    if (persist.phase == SC_PERSIST_IDLE) return 0;
    fail_retain();
    return 1;
}

int sc_persist_pin(unsigned kind) {
    if (kind >= SC_PERSIST_PIN_KINDS || persist.phase != SC_PERSIST_PUBLISHED ||
        persist.pins[kind] == 0xffffffffU) return 0;
    ++persist.pins[kind];
    return 1;
}

int sc_persist_unpin(unsigned kind) {
    if (kind >= SC_PERSIST_PIN_KINDS || !persist.pins[kind] ||
        (persist.phase != SC_PERSIST_PUBLISHED && persist.phase != SC_PERSIST_DRAINING))
        return 0;
    --persist.pins[kind];
    return 1;
}

static int offset(const void *p, ScMaU32 *o) {
    const unsigned char *a = (const unsigned char *)p;
    if (persist.phase != SC_PERSIST_PUBLISHED || !a || a < persist.image ||
        (ScMaU32)(a - persist.image) >= persist.bytes) return 0;
    *o = (ScMaU32)(a - persist.image);
    return 1;
}

int sc_persist_owns(const void *p, ScMaU32 n) {
    ScMaU32 o;
    return offset(p, &o) && n && n <= persist.bytes - o;
}

int sc_persist_code(ScPersistFn fn) {
    unsigned long a = (unsigned long)fn, b = (unsigned long)persist.image;
    return persist.phase == SC_PERSIST_PUBLISHED && fn && a >= b &&
        a - b < persist.layout.text && !((a - b) & 3);
}

int sc_persist_holds_fragment(void) {
    return persist.phase != SC_PERSIST_IDLE;
}

int sc_persist_call(int arg0, unsigned arg1, int *result) {
    if (!result || persist.phase != SC_PERSIST_PUBLISHED ||
        persist.depth == 0xffffffffU ||
        sc_ma_map(persist.arena, &persist.lease, 0, 8, SC_MA_READ | SC_MA_EXEC) != persist.image)
        return 0;
    ++persist.depth;
    *result = sc_persist_native_enter(persist.image, arg0, arg1);
    --persist.depth;
    return persist.phase == SC_PERSIST_PUBLISHED || persist.phase == SC_PERSIST_DRAINING;
}

const ScPersist *sc_persist_state(void) { return &persist; }
#endif
