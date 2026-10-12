/* PURE */
#include "rl_geo_guard.h"

int rl_geo_state_ok(int stack, int ret, int node) {
    return stack >= 0 && stack <= RL_GEO_STACK_WORDS && ret >= 0 && ret <= RL_GEO_STACK_WORDS && node >= 0 &&
           node < RL_GEO_NODES;
}

int rl_geo_admit(const unsigned char* cmd, const unsigned char* mem_lo, const unsigned char* mem_hi,
                 const unsigned char* sizes, int stack, int ret, int node) {
    unsigned long at = (unsigned long)cmd;
    unsigned long lo = (unsigned long)mem_lo;
    unsigned long hi = (unsigned long)mem_hi;
    unsigned int op;
    unsigned int size;

    if (!rl_geo_state_ok(stack, ret, node) || (at & 3) != 0 || at < lo || at >= hi || hi - at < 4) {
        return 0;
    }
    op = cmd[0];
    if (op >= RL_GEO_OPCODES) {
        return 0;
    }
    size = sizes[op];
    if (size == 0 || hi - at < size) {
        return 0;
    }
    switch (op) {
        case 0x00:
            return stack + 2 <= RL_GEO_STACK_WORDS;
        case 0x01:
            return ret >= 2;
        case 0x03:
            return stack + 1 <= RL_GEO_STACK_WORDS;
        case 0x04:
            return stack >= 1;
        case 0x05:
            return node + 1 < RL_GEO_NODES;
        case 0x06:
            return node >= 1;
        default:
            return 1;
    }
}
