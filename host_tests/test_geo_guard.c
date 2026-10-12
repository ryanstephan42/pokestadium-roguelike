#include <stdio.h>
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

    if (failures) {
        return 1;
    }
    printf("geo guard: OK\n");
    return 0;
}
