#define _GNU_SOURCE
#include <stdio.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <string.h>
#include "rl_geo_guard.h"

static const unsigned char sizes[RL_GEO_OPCODES] = {
    8, 4, 8, 8, 4, 4, 4, 8, 12, 4, 8, 24, 4, 4, 4, 4, 4, 4, 0, 8,
    12, 12, 4, 20, 8, 8, 4, 16, 16, 28, 8, 24, 20, 16, 8, 16, 4, 4, 20,
};

static unsigned int words[16];
static int failures;

#define CHECK(c)                                                  \
    do {                                                          \
        if (!(c)) {                                               \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);    \
            failures++;                                           \
        }                                                         \
    } while (0)

static int admit_at(unsigned int offset, unsigned int op, int stack, int ret, int node) {
    unsigned char* mem = (unsigned char*)words;
    memset(words, 0, sizeof(words));
    if (offset < sizeof(words)) {
        mem[offset] = (unsigned char)op;
    }
    return rl_geo_admit(mem + offset, mem, mem + sizeof(words), sizes, stack, ret, node);
}

int main(void) {
    unsigned char* mem = (unsigned char*)words;
    unsigned int op;

    for (op = 0; op < RL_GEO_OPCODES; op++) {
        CHECK(admit_at(0, op, 2, 2, 1) == (op != 0x12));
    }
    CHECK(!admit_at(0, RL_GEO_OPCODES, 2, 2, 0));
    CHECK(!admit_at(0, 0xFF, 2, 2, 0));
    CHECK(!admit_at(0, 0x12, 2, 2, 0));

    /* extent: last word fits a 4-byte command but not an 8/12/28-byte command */
    CHECK(admit_at(60, 0x05, 2, 2, 0));
    CHECK(!admit_at(60, 0x02, 2, 2, 0));
    CHECK(!admit_at(56, 0x08, 2, 2, 0));
    CHECK(admit_at(52, 0x08, 2, 2, 0));
    CHECK(!admit_at(40, 0x1D, 2, 2, 0));
    CHECK(admit_at(36, 0x1D, 2, 2, 0));
    CHECK(!rl_geo_admit(mem + 2, mem, mem + 64, sizes, 2, 2, 0));
    CHECK(!rl_geo_admit(mem + 64, mem, mem + 64, sizes, 2, 2, 0));
    CHECK(!rl_geo_admit(mem, mem + 4, mem + 64, sizes, 2, 2, 0));

    /* branch stack: link pushes two words, branch one, return/end pop */
    CHECK(admit_at(0, 0x00, 14, 2, 0));
    CHECK(!admit_at(0, 0x00, 15, 2, 0));
    CHECK(admit_at(0, 0x03, 15, 2, 0));
    CHECK(!admit_at(0, 0x03, 16, 2, 0));
    CHECK(admit_at(0, 0x04, 1, 0, 0));
    CHECK(!admit_at(0, 0x04, 0, 0, 0));
    CHECK(admit_at(0, 0x01, 2, 2, 0));
    CHECK(!admit_at(0, 0x01, 2, 1, 0));

    /* node list: open writes index+1, close may not go below root */
    CHECK(admit_at(0, 0x05, 2, 2, 30));
    CHECK(!admit_at(0, 0x05, 2, 2, 31));
    CHECK(admit_at(0, 0x06, 2, 2, 1));
    CHECK(!admit_at(0, 0x06, 2, 2, 0));

    CHECK(rl_geo_state_ok(0, 0, 0));
    CHECK(rl_geo_state_ok(16, 16, 31));
    CHECK(!rl_geo_state_ok(17, 2, 0));
    CHECK(!rl_geo_state_ok(-1, 2, 0));
    CHECK(!rl_geo_state_ok(2, 17, 0));
    CHECK(!rl_geo_state_ok(2, -1, 0));
    CHECK(!rl_geo_state_ok(2, 2, 32));
    CHECK(!rl_geo_state_ok(2, 2, -1));
    CHECK(!admit_at(0, 0x07, 17, 2, 0));

    /* payload pointers: cmd7 node (written via func_80012094), cmd8 callback (invoked).
     * The N64 field is a 32-bit word, so the arena must sit below 4 GiB on the host. */
    {
        unsigned char* lo = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
        unsigned char* hi = lo + 256;
        unsigned int* slot = (unsigned int*)(lo + 8);
        unsigned int base = (unsigned int)(unsigned long)lo;

        CHECK(lo != MAP_FAILED && (unsigned long)lo == base);
        if (lo != MAP_FAILED && (unsigned long)lo == base) {
            lo[4] = 0x07;
            *slot = 0;
            CHECK(rl_geo_admit(lo + 4, lo, hi, sizes, 2, 2, 0));
            *slot = base + 64;
            CHECK(rl_geo_admit(lo + 4, lo, hi, sizes, 2, 2, 0));
            *slot = base;
            CHECK(rl_geo_admit(lo + 4, lo, hi, sizes, 2, 2, 0));
            *slot = base + 256 - RL_GEO_NODE_BYTES;
            CHECK(rl_geo_admit(lo + 4, lo, hi, sizes, 2, 2, 0));
            *slot = base + 256 - RL_GEO_NODE_BYTES + 4;
            CHECK(!rl_geo_admit(lo + 4, lo, hi, sizes, 2, 2, 0));
            *slot = base + 256;
            CHECK(!rl_geo_admit(lo + 4, lo, hi, sizes, 2, 2, 0));
            *slot = base + 66;
            CHECK(!rl_geo_admit(lo + 4, lo, hi, sizes, 2, 2, 0));
            *slot = 4;
            CHECK(!rl_geo_admit(lo + 4, lo, hi, sizes, 2, 2, 0));
            *slot = base;
            CHECK(!rl_geo_admit(lo + 4, lo + 4, hi, sizes, 2, 2, 0));
            *slot = base + 64;
            CHECK(!rl_geo_admit(lo + 4, lo, lo + 11, sizes, 2, 2, 0));
            CHECK(!rl_geo_admit(lo + 4, lo, hi, sizes, 2, 2, 32));

            lo[4] = 0x08;
            *slot = 0;
            CHECK(rl_geo_admit(lo + 4, lo, hi, sizes, 2, 2, 0));
            *slot = 0x86002F34U;
            CHECK(rl_geo_admit(lo + 4, lo, hi, sizes, 2, 2, 0));
            *slot = 0x86002F36U;
            CHECK(!rl_geo_admit(lo + 4, lo, hi, sizes, 2, 2, 0));
            *slot = base + 1;
            CHECK(!rl_geo_admit(lo + 4, lo, hi, sizes, 2, 2, 0));
            munmap(lo, 4096);
        }
    }

    /* loader image: [lo, hi) = fragment start .. relocOffset (addresses are N64 words) */
    {
        static unsigned int img[64];
        unsigned char* c = (unsigned char*)img;
        const unsigned int lo = 0x80400000U, hi = lo + 0x1000;
        RlGeoTexDesc tex[2];
        RlGeoPalDesc pal[1];

        CHECK(sizeof(RlGeoTexDesc) == 12 && sizeof(RlGeoPalDesc) == 12);
        CHECK(rl_geo_image_ok(lo, 0x1000, 0x1200, 0x80000000U, 0x80800000U));
        CHECK(!rl_geo_image_ok(lo, 0x1F, 0x1200, 0x80000000U, 0x80800000U));
        CHECK(!rl_geo_image_ok(lo, 0x1300, 0x1200, 0x80000000U, 0x80800000U));
        CHECK(!rl_geo_image_ok(lo + 2, 0x1000, 0x1200, 0x80000000U, 0x80800000U));
        CHECK(!rl_geo_image_ok(0x807FF000U, 0x2000, 0x2000, 0x80000000U, 0x80800000U));
        CHECK(!rl_geo_image_ok(0x7FFFF000U, 0x100, 0x100, 0x80000000U, 0x80800000U));

        memset(img, 0, sizeof(img));
        c[0] = 0x05;
        CHECK(rl_geo_image_cmd_ok(c, lo, lo, hi, sizes));
        CHECK(rl_geo_image_cmd_ok(c, hi - 4, lo, hi, sizes));
        CHECK(!rl_geo_image_cmd_ok(c, hi, lo, hi, sizes));
        CHECK(!rl_geo_image_cmd_ok(c, lo - 4, lo, hi, sizes));
        CHECK(!rl_geo_image_cmd_ok(c, lo + 2, lo, hi, sizes));
        c[0] = 0x02;
        CHECK(!rl_geo_image_cmd_ok(c, hi - 4, lo, hi, sizes));
        CHECK(rl_geo_image_cmd_ok(c, hi - 8, lo, hi, sizes));
        c[0] = 0x0A;
        CHECK(!rl_geo_image_cmd_ok(c, lo, lo, hi, sizes));
        c[0] = 0x12;
        CHECK(!rl_geo_image_cmd_ok(c, lo, lo, hi, sizes));

        c[0] = 0x17;
        *(short*)(c + 2) = 0x0102; /* low byte 2 textures */
        *(short*)(c + 4) = 1;
        img[2] = lo + 0x100;
        img[3] = lo + 0x200;
        img[4] = lo + 0x300;
        CHECK(rl_geo_image_cmd_ok(c, lo, lo, hi, sizes));
        img[2] = hi - 24;
        CHECK(rl_geo_image_cmd_ok(c, lo, lo, hi, sizes));
        img[2] = hi - 20;
        CHECK(!rl_geo_image_cmd_ok(c, lo, lo, hi, sizes));
        img[2] = lo + 0x102;
        CHECK(!rl_geo_image_cmd_ok(c, lo, lo, hi, sizes));
        img[2] = lo - 12;
        CHECK(!rl_geo_image_cmd_ok(c, lo, lo, hi, sizes));
        *(short*)(c + 2) = 0x0100;
        CHECK(rl_geo_image_cmd_ok(c, lo, lo, hi, sizes));
        *(short*)(c + 2) = 2;
        img[2] = lo + 0x100;
        img[3] = hi;
        CHECK(!rl_geo_image_cmd_ok(c, lo, lo, hi, sizes));
        *(short*)(c + 4) = 0;
        CHECK(rl_geo_image_cmd_ok(c, lo, lo, hi, sizes));
        *(short*)(c + 4) = 1;
        img[3] = lo + 0x200;
        img[4] = 0;
        CHECK(!rl_geo_image_cmd_ok(c, lo, lo, hi, sizes));
        img[4] = lo + 0x304;
        CHECK(!rl_geo_image_cmd_ok(c, lo, lo, hi, sizes));
        img[4] = hi - 16;
        CHECK(rl_geo_image_cmd_ok(c, lo, lo, hi, sizes));
        img[4] = hi - 8;
        CHECK(!rl_geo_image_cmd_ok(c, lo, lo, hi, sizes));
        CHECK(!rl_geo_image_cmd_ok(c, hi - 16, lo, hi, sizes));

        memset(tex, 0, sizeof(tex));
        memset(pal, 0, sizeof(pal));
        tex[0].type = 2;
        tex[0].texels = 0x100;
        tex[0].texture = hi - 0x200;
        tex[1].type = 3;
        tex[1].texels = 4;
        tex[1].texture = lo;
        pal[0].count = 16;
        pal[0].data = hi - 32;
        CHECK(rl_geo_recolor_ok(tex, 2, pal, 1, lo, hi));
        tex[0].texture = hi - 0x1FE;
        CHECK(!rl_geo_recolor_ok(tex, 2, pal, 1, lo, hi));
        tex[0].kind = 1; /* not written by the recolor writer */
        CHECK(rl_geo_recolor_ok(tex, 2, pal, 1, lo, hi));
        tex[0].kind = 0;
        tex[0].type = 4;
        CHECK(rl_geo_recolor_ok(tex, 2, pal, 1, lo, hi));
        tex[0].type = 2;
        tex[0].texels = -1;
        CHECK(rl_geo_recolor_ok(tex, 2, pal, 1, lo, hi));
        tex[0].texels = 0x100;
        tex[0].texture = hi - 0x200;
        tex[1].texture = lo + 2;
        CHECK(!rl_geo_recolor_ok(tex, 2, pal, 1, lo, hi));
        CHECK(rl_geo_recolor_ok(tex, 1, pal, 1, lo, hi));
        tex[1].texture = lo - 4;
        CHECK(!rl_geo_recolor_ok(tex, 2, pal, 1, lo, hi));
        tex[1].texture = lo;
        pal[0].data = hi - 30;
        CHECK(!rl_geo_recolor_ok(tex, 2, pal, 1, lo, hi));
        pal[0].data = hi - 31;
        CHECK(!rl_geo_recolor_ok(tex, 2, pal, 1, lo, hi));
        pal[0].data = hi - 32;
        pal[0].count = 0x7FFFFFFF;
        CHECK(!rl_geo_recolor_ok(tex, 2, pal, 1, lo, hi));
        pal[0].count = -5;
        CHECK(rl_geo_recolor_ok(tex, 2, pal, 1, lo, hi));
        CHECK(rl_geo_recolor_ok(tex, 0, pal, 0, lo, hi));
    }

    if (failures) {
        return 1;
    }
    printf("geo guard: OK\n");
    return 0;
}
