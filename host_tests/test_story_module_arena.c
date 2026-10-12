#include "module-spans.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define DATA_BYTES (2U * 640U * 480U * 2U + 64U)
#define CHECK(c) do { ++checks; if (!(c)) { \
    fprintf(stderr, "%d: %s\n", __LINE__, #c); exit(1); } } while (0)

int main(int argc, char **argv) {
    ScMaArena a = {0};
    ScMaArena offset_arena = {0};
    ScMaHandle h, next, other;
    ScMaLease staging = {{0,0},0,0}, cpu = {{0,0},0,0}, nested = {{0,0},0,0};
    ScMaLease copied;
    ScMaSpan bad[SC_MA_SPANS];
    unsigned char *storage = calloc(1, 0x140000);
    unsigned i;
    int failure = argc > 1 && strcmp(argv[1], "fail") == 0;
    int data_failure = argc > 1 && strcmp(argv[1], "data-fail") == 0;
    CHECK(storage != 0);
    CHECK(sc_ma_init(&offset_arena, 0x800000, storage, 0x30000));
    CHECK(sc_ma_alloc(&offset_arena, 16, &h));
    CHECK(sc_ma_alloc(&offset_arena, SC_IMAGE_RAM, &next));
    CHECK(sc_ma_address(&offset_arena, next) == 0x80400020U);
    CHECK(sc_ma_publish(&offset_arena, next, sc_image_spans, SC_IMAGE_SPAN_COUNT));
    CHECK(sc_ma_release(&offset_arena, h));
    CHECK(sc_ma_release(&offset_arena, next));
    CHECK(sc_ma_shutdown(&offset_arena));
    CHECK(sc_ma_init(&offset_arena, 0x800000, storage, 0x140000));
    CHECK(sc_ma_alloc(&offset_arena, 16, &other));
    CHECK(sc_ma_alloc(&offset_arena, DATA_BYTES, &h));
    CHECK(sc_ma_address(&offset_arena, h) + 32 == 0x80400040U);
    CHECK(sc_ma_data(&offset_arena, h, &staging));
    CHECK(sc_ma_map(&offset_arena, &staging, 32, DATA_BYTES - 64,
                    SC_MA_READ | SC_MA_WRITE) == storage + 64);
    CHECK(!sc_ma_map(&offset_arena, &staging, 0, 4, SC_MA_EXEC));
    CHECK(!sc_ma_map(&offset_arena, &staging, DATA_BYTES - 1, 2, SC_MA_WRITE));
    CHECK(!sc_ma_map(&offset_arena, &staging, 0, DATA_BYTES + 1, SC_MA_READ));
    CHECK(!sc_ma_staging(&offset_arena, h, &nested));
    CHECK(!sc_ma_data(&offset_arena, h, &nested));
    CHECK(!sc_ma_module(&offset_arena, h, &nested));
    CHECK(!sc_ma_seal(&offset_arena, &staging, sc_image_spans, SC_IMAGE_SPAN_COUNT));
    CHECK(!sc_ma_publish(&offset_arena, h, sc_image_spans, SC_IMAGE_SPAN_COUNT));
    CHECK(!sc_ma_release(&offset_arena, h));
    CHECK(!sc_ma_shutdown(&offset_arena));
    copied = staging;
    copied.kind = 1;
    CHECK(!sc_ma_map(&offset_arena, &copied, 0, 4, SC_MA_READ));
    copied = staging;
    copied.kind = 2;
    CHECK(!sc_ma_map(&offset_arena, &copied, 0, 4, SC_MA_EXEC));
    if (data_failure) {
        CHECK(sc_ma_fail(&offset_arena, &staging));
        CHECK(!sc_ma_map(&offset_arena, &staging, 0, 4, SC_MA_READ));
        CHECK(!sc_ma_end(&offset_arena, &staging));
        CHECK(!sc_ma_release(&offset_arena, h));
        CHECK(!sc_ma_shutdown(&offset_arena));
        CHECK(offset_arena.blocks[h.slot].live &&
              offset_arena.blocks[h.slot].ticket == staging.ticket);
        free(storage);
        printf("MODULE DATA FAIL-STOP PASSED: %u checks; host only\n", checks);
        return 0;
    }
    copied = staging;
    CHECK(sc_ma_end(&offset_arena, &staging));
    CHECK(sc_ma_data(&offset_arena, h, &staging));
    CHECK(!sc_ma_map(&offset_arena, &copied, 0, 4, SC_MA_READ));
    CHECK(!sc_ma_end(&offset_arena, &copied));
    CHECK(sc_ma_end(&offset_arena, &staging));
    offset_arena.next_ticket = 0xffffffffU;
    CHECK(!sc_ma_data(&offset_arena, h, &staging));
    CHECK(sc_ma_release(&offset_arena, h));
    CHECK(sc_ma_alloc(&offset_arena, 128, &next));
    CHECK(next.generation != h.generation);
    CHECK(!sc_ma_data(&offset_arena, h, &staging));
    CHECK(sc_ma_release(&offset_arena, next));
    CHECK(sc_ma_release(&offset_arena, other));
    CHECK(sc_ma_shutdown(&offset_arena));
    CHECK(!sc_ma_init(&a, 0x400000, storage, 0x30000));
    CHECK(sc_ma_init(&a, 0x800000, storage, 0x30000));
    CHECK(!sc_ma_init(&a, 0x800000, storage, 0x30000));
    CHECK(sc_ma_alloc(&a, SC_IMAGE_RAM, &h));
    CHECK(sc_ma_alloc(&a, SC_IMAGE_RAM, &other));
    CHECK(sc_ma_address(&a, h) == 0x80400000U);
    CHECK(sc_ma_staging(&a, h, &staging));
    CHECK(!sc_ma_staging(&a, h, &nested));
    CHECK(!sc_ma_release(&a, h));
    CHECK(!sc_ma_publish(&a, h, sc_image_spans, SC_IMAGE_SPAN_COUNT));
    CHECK(sc_ma_map(&a, &staging, 0, SC_IMAGE_RAM, SC_MA_WRITE) == storage);
    CHECK(!sc_ma_map(&a, &staging, 0, 4, SC_MA_EXEC));
    copied = staging;
    CHECK(sc_ma_end(&a, &staging));
    CHECK(!sc_ma_end(&a, &copied));
    memcpy(bad, sc_image_spans, sizeof(sc_image_spans));
    bad[0].rights |= SC_MA_WRITE;
    CHECK(!sc_ma_publish(&a, h, bad, SC_IMAGE_SPAN_COUNT));
    memcpy(bad, sc_image_spans, sizeof(sc_image_spans));
    ++bad[1].offset;
    CHECK(!sc_ma_publish(&a, h, bad, SC_IMAGE_SPAN_COUNT));
    memcpy(bad, sc_image_spans, sizeof(sc_image_spans));
    --bad[0].size;
    --bad[1].offset;
    ++bad[1].size;
    CHECK(!sc_ma_publish(&a, h, bad, SC_IMAGE_SPAN_COUNT));
    CHECK(sc_ma_publish(&a, h, sc_image_spans, SC_IMAGE_SPAN_COUNT));
    CHECK(!sc_ma_staging(&a, h, &staging));
    CHECK(sc_ma_module(&a, h, &cpu));
    CHECK(!sc_ma_module(&a, h, &nested));
    CHECK(!sc_ma_end(&a, &copied));
    CHECK(!sc_ma_release(&a, h));
    for (i = 0; i < SC_IMAGE_SPAN_COUNT; ++i) {
        const ScMaSpan *s = &sc_image_spans[i];
        unsigned char *p = sc_ma_map(&a, &cpu, s->offset, s->size, s->rights);
        CHECK(p == storage + s->offset);
        CHECK(!sc_ma_map(&a, &cpu, s->offset, s->size + 1, s->rights));
        CHECK(!sc_ma_map(&a, &cpu, s->offset + s->size - 1, 2, s->rights));
        if (s->rights & SC_MA_WRITE) {
            CHECK(!sc_ma_map(&a, &cpu, s->offset, 4, SC_MA_EXEC));
            p[0] = 17;
            CHECK(p[0] == 17);
        } else {
            CHECK(!sc_ma_map(&a, &cpu, s->offset, 4, SC_MA_WRITE));
        }
    }
    CHECK(!sc_ma_map(&a, &cpu, 0xffffffffU, 4, SC_MA_READ));
    CHECK(!sc_ma_map(&a, &cpu, 0, 0xffffffffU, SC_MA_READ));
    CHECK(sc_ma_release(&a, other));
    if (failure) {
        CHECK(sc_ma_fail(&a, &cpu));
        CHECK(!sc_ma_end(&a, &cpu));
        CHECK(!sc_ma_release(&a, h));
        CHECK(!sc_ma_shutdown(&a));
        CHECK(!sc_ma_module(&a, h, &nested));
        CHECK(!sc_ma_map(&a, &cpu, SC_IMAGE_PAYLOAD, 4, SC_MA_WRITE));
        CHECK(a.blocks[h.slot].live && a.blocks[h.slot].ticket == cpu.ticket);
    } else {
        CHECK(sc_ma_end(&a, &cpu));
        CHECK(sc_ma_release(&a, h));
        CHECK(sc_ma_alloc(&a, SC_IMAGE_RAM, &next));
        CHECK(next.slot == h.slot && next.generation != h.generation);
        CHECK(!sc_ma_module(&a, h, &cpu));
        CHECK(!sc_ma_release(&a, h));
        CHECK(sc_ma_release(&a, next));
        CHECK(sc_ma_shutdown(&a));
        CHECK(sc_ma_init(&a, 0x800000, storage, 0x30000));
        CHECK(sc_ma_alloc(&a, SC_IMAGE_RAM, &next));
        CHECK(next.generation > h.generation);
        CHECK(sc_ma_release(&a, next));
        CHECK(sc_ma_shutdown(&a));
    }
    free(storage);
    printf("MODULE ARENA PASSED: %u checks, fail=%d\n", checks, failure);
    return 0;
}
