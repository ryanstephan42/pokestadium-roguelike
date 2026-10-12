#include "sc_module_arena.h"

#define START 0x400000U
#define TAIL 0x7da800U
#define MAXIMUM 0xffffffffU

static ScMaBlock *block(ScMaArena *a, ScMaHandle h) {
    ScMaBlock *b;
    if (!a || !a->active || h.slot >= SC_MA_SLOTS) return 0;
    b = &a->blocks[h.slot];
    return b->live && b->generation == h.generation ? b : 0;
}

static ScMaBlock *leased(ScMaArena *a, const ScMaLease *l) {
    ScMaBlock *b;
    if (!l || !l->ticket) return 0;
    b = block(a, l->handle);
    return b && b->ticket == l->ticket && b->lease_kind == l->kind ? b : 0;
}

int sc_ma_init(ScMaArena *a, ScMaU32 memory, unsigned char *backing,
               ScMaU32 bytes) {
    unsigned i;
    if (!a || a->active || memory != 0x800000U || !backing || !bytes ||
        bytes >= TAIL - START || (bytes & 15)) return 0;
    for (i = 0; i < SC_MA_SLOTS; ++i)
        if (a->blocks[i].live) return 0;
    a->backing = backing;
    a->backing_bytes = bytes;
    a->end = START + bytes;
    a->active = 1;
    return 1;
}

int sc_ma_alloc(ScMaArena *a, ScMaU32 bytes, ScMaHandle *h) {
    ScMaU32 slot, i, p, conflict;
    ScMaBlock *b;
    if (!a || !a->active || !h || !bytes || (bytes & 15)) return 0;
    for (slot = 0; slot < SC_MA_SLOTS; ++slot)
        if (!a->blocks[slot].live && a->blocks[slot].generation != MAXIMUM) break;
    if (slot == SC_MA_SLOTS) return 0;
    p = START;
    do {
        p = (p + 31) & ~31U;
        if (p >= a->end || bytes > a->end - p) return 0;
        conflict = 0;
        for (i = 0; i < SC_MA_SLOTS; ++i) {
            b = &a->blocks[i];
            if (b->live && p < b->base + b->size && b->base < p + bytes) {
                p = b->base + b->size;
                conflict = 1;
                break;
            }
        }
    } while (conflict);
    b = &a->blocks[slot];
    ++b->generation;
    b->base = p;
    b->size = bytes;
    b->live = 1;
    b->published = b->failed = b->ticket = b->lease_kind = b->span_count = 0;
    h->slot = slot;
    h->generation = b->generation;
    return 1;
}

static int issue(ScMaArena *a, ScMaHandle h, ScMaLease *l, ScMaU32 kind) {
    ScMaBlock *b = block(a, h);
    if (!b || !l || l->ticket || b->failed || b->ticket ||
        a->next_ticket == MAXIMUM || b->published != (kind == 2)) return 0;
    b->ticket = ++a->next_ticket;
    b->lease_kind = kind;
    l->handle = h;
    l->ticket = b->ticket;
    l->kind = kind;
    return 1;
}

int sc_ma_staging(ScMaArena *a, ScMaHandle h, ScMaLease *l) {
    return issue(a, h, l, 1);
}

int sc_ma_module(ScMaArena *a, ScMaHandle h, ScMaLease *l) {
    return issue(a, h, l, 2);
}

int sc_ma_data(ScMaArena *a, ScMaHandle h, ScMaLease *l) {
    return issue(a, h, l, 3);
}

unsigned char *sc_ma_map(ScMaArena *a, const ScMaLease *l, ScMaU32 offset,
                        ScMaU32 size, ScMaU32 rights) {
    ScMaBlock *b = leased(a, l);
    ScMaU32 i;
    if (!b || b->failed || !size || offset >= b->size ||
        size > b->size - offset || !rights ||
        (rights & ~(SC_MA_READ | SC_MA_WRITE | SC_MA_EXEC))) return 0;
    if (l->kind == 1 || l->kind == 3) {
        if (rights & SC_MA_EXEC) return 0;
    } else if (l->kind == 2) {
        for (i = 0; i < b->span_count; ++i) {
            ScMaSpan *s = &b->spans[i];
            if (offset >= s->offset && offset - s->offset < s->size &&
                size <= s->size - (offset - s->offset) &&
                !(rights & ~s->rights)) break;
        }
        if (i == b->span_count) return 0;
    } else return 0;
    return a->backing + (b->base - START) + offset;
}

int sc_ma_end(ScMaArena *a, ScMaLease *l) {
    ScMaBlock *b = leased(a, l);
    if (!b || b->failed) return 0;
    b->ticket = b->lease_kind = 0;
    l->ticket = l->kind = 0;
    return 1;
}

static int plan(ScMaBlock *b, const ScMaSpan *s, ScMaU32 count) {
    ScMaU32 i, cursor = 0, executable = 0;
    if (!b || !s || !count || count > SC_MA_SPANS || b->published ||
        b->failed) return 0;
    for (i = 0; i < count; ++i) {
        if (s[i].offset != cursor || !s[i].size ||
            (((b->base + s[i].offset) | s[i].size) & 15) ||
            ((s[i].rights & SC_MA_EXEC) &&
             (((b->base + s[i].offset) | s[i].size) & 31)) ||
            cursor >= b->size ||
            s[i].size > b->size - cursor || !(s[i].rights & SC_MA_READ) ||
            (s[i].rights & ~(SC_MA_READ | SC_MA_WRITE | SC_MA_EXEC)) ||
            ((s[i].rights & SC_MA_WRITE) && (s[i].rights & SC_MA_EXEC))) return 0;
        cursor += s[i].size;
        executable |= s[i].rights & SC_MA_EXEC;
    }
    if (cursor != b->size || !executable) return 0;
    for (i = 0; i < count; ++i) b->spans[i] = s[i];
    b->span_count = count;
    b->published = 1;
    return 1;
}

int sc_ma_publish(ScMaArena *a, ScMaHandle h, const ScMaSpan *s,
                  ScMaU32 count) {
    ScMaBlock *b = block(a, h);
    return b && !b->ticket && plan(b, s, count);
}

int sc_ma_seal(ScMaArena *a, ScMaLease *l, const ScMaSpan *s,
               ScMaU32 count) {
    ScMaBlock *b = leased(a, l);
    if (!b || l->kind != 1 || !plan(b, s, count)) return 0;
    b->lease_kind = l->kind = 2;
    return 1;
}

int sc_ma_fail(ScMaArena *a, const ScMaLease *l) {
    ScMaBlock *b = leased(a, l);
    if (!b) return 0;
    b->failed = 1;
    return 1;
}

int sc_ma_release(ScMaArena *a, ScMaHandle h) {
    ScMaBlock *b = block(a, h);
    if (!b || b->ticket || b->failed) return 0;
    b->live = 0;
    return 1;
}

int sc_ma_shutdown(ScMaArena *a) {
    unsigned i;
    if (!a || !a->active) return 0;
    for (i = 0; i < SC_MA_SLOTS; ++i) if (a->blocks[i].live) return 0;
    a->active = 0;
    a->backing = 0;
    return 1;
}

ScMaU32 sc_ma_address(ScMaArena *a, ScMaHandle h) {
    ScMaBlock *b = block(a, h);
    return b && !b->failed ? 0x80000000U + b->base : 0;
}
