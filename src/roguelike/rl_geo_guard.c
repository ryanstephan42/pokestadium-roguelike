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
    /* if-chain rather than switch: no compiler jump table on the guarded parser path */
    if (op == 0x00) {
        return stack + 2 <= RL_GEO_STACK_WORDS;
    }
    if (op == 0x01) {
        return ret >= 2;
    }
    if (op == 0x03) {
        return stack + 1 <= RL_GEO_STACK_WORDS;
    }
    if (op == 0x04) {
        return stack >= 1;
    }
    if (op == 0x05) {
        return node + 1 < RL_GEO_NODES;
    }
    if (op == 0x06) {
        return node >= 1;
    }
    if (op == 0x07 || op == 0x08) {
        /* the N64 payload pointer is the 32-bit word at +4 (sizes[] already covers it) */
        unsigned long ptr = *(const unsigned int*)(cmd + 4);

        if (ptr == 0) {
            return 1;
        }
        if ((ptr & 3) != 0) {
            return 0;
        }
        return op == 0x08 || (ptr >= lo && ptr < hi && hi - ptr >= RL_GEO_NODE_BYTES);
    }
    return 1;
}

int rl_geo_span_in(unsigned int lo, unsigned int hi, unsigned int at, unsigned int bytes, unsigned int align) {
    return lo < hi && at % align == 0 && at >= lo && at < hi && bytes <= hi - at;
}

int rl_geo_image_ok(unsigned int lo, unsigned int reloc, unsigned int ram, unsigned int mem_lo,
                    unsigned int mem_hi) {
    return lo % 4 == 0 && reloc >= 0x20 && reloc <= ram && lo >= mem_lo && lo < mem_hi && reloc <= mem_hi - lo;
}

int rl_geo_image_cmd_ok(const unsigned char* cmd, unsigned int at, unsigned int lo, unsigned int hi,
                        const unsigned char* sizes) {
    unsigned int op;

    if (!rl_geo_span_in(lo, hi, at, 4, 4)) {
        return 0;
    }
    op = cmd[0];
    if (op >= RL_GEO_OPCODES || sizes[op] == 0 || !rl_geo_span_in(lo, hi, at, sizes[op], 4)) {
        return 0;
    }
    if (op == 0x0A) {
        return 0;
    }
    if (op == 0x17) {
        unsigned int ntex = (unsigned int)*(const short*)(cmd + 2) & 0xFF;
        unsigned int npal = (unsigned int)*(const short*)(cmd + 4) & 0xFF;
        unsigned int tex = *(const unsigned int*)(cmd + 8);
        unsigned int pal = *(const unsigned int*)(cmd + 12);
        unsigned int vtx = *(const unsigned int*)(cmd + 16);

        if (ntex != 0 && !rl_geo_span_in(lo, hi, tex, 12 * ntex, 4)) {
            return 0;
        }
        if (npal != 0 && !rl_geo_span_in(lo, hi, pal, 12 * npal, 4)) {
            return 0;
        }
        return vtx != 0 && rl_geo_span_in(lo, hi, vtx, 16, 8);
    }
    return 1;
}

int rl_geo_recolor_ok(const RlGeoTexDesc* tex, int ntex, const RlGeoPalDesc* pal, int npal, unsigned int lo,
                      unsigned int hi) {
    int i;

    for (i = 0; i < ntex; i++) {
        if (tex[i].kind == 0 && tex[i].texels > 0) {
            unsigned int width = tex[i].type == 2 ? 2 : tex[i].type == 3 ? 4 : 0;

            if (width != 0 &&
                !rl_geo_span_in(lo, hi, tex[i].texture, (unsigned int)tex[i].texels * width, width)) {
                return 0;
            }
        }
    }
    for (i = 0; i < npal; i++) {
        /* a positive s32 count doubled cannot wrap a 32-bit span */
        if (pal[i].count > 0 && !rl_geo_span_in(lo, hi, pal[i].data, (unsigned int)pal[i].count * 2, 2)) {
            return 0;
        }
    }
    return 1;
}
