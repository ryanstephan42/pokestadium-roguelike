#ifndef RL_GEO_GUARD_H
#define RL_GEO_GUARD_H

#define RL_GEO_OPCODES 39
#define RL_GEO_STACK_WORDS 16
#define RL_GEO_NODES 32
/* bytes of a GraphNode written by the parser through a cmd7 node pointer */
#define RL_GEO_NODE_BYTES 0x18

/* sizes[op] is the exact command extent; 0 marks a command that cannot advance.
 * Payload pointers dereferenced during construction are admitted too: a cmd7 node
 * must be NULL or a word-aligned GraphNode wholly inside [mem_lo, mem_hi); a cmd8
 * callback must be NULL or word-aligned (its address class is checked by
 * Util_ConvertAddrToVirtAddr, which yields NULL for invalid addresses). */
int rl_geo_admit(const unsigned char* cmd, const unsigned char* mem_lo, const unsigned char* mem_hi,
                 const unsigned char* sizes, int stack, int ret, int node);
int rl_geo_state_ok(int stack, int ret, int node);

/* Loader-image admission (N64 addresses as 32-bit words, image = [lo, hi) = fragment start to
 * its relocation table). Mirrors the parser's cmd17 and the recolor writer func_80018C40. */
typedef struct {
    unsigned char kind;
    unsigned char type;
    short width;
    short height;
    short texels;
    unsigned int texture;
} RlGeoTexDesc; /* unk_D_86002F34_018, 12 bytes */
typedef struct {
    int count;
    unsigned int data;
    unsigned int pad;
} RlGeoPalDesc; /* unk_D_86002F34_01C, 12 bytes */

int rl_geo_span_in(unsigned int lo, unsigned int hi, unsigned int at, unsigned int bytes, unsigned int align);
int rl_geo_image_ok(unsigned int lo, unsigned int reloc, unsigned int ram, unsigned int mem_lo,
                    unsigned int mem_hi);
/* cmd: whole command inside image; cmdA refused; cmd17 descriptor arrays and Vtx inside */
int rl_geo_image_cmd_ok(const unsigned char* cmd, unsigned int at, unsigned int lo, unsigned int hi,
                        const unsigned char* sizes);
/* every span func_80018C40 would write lies inside the image */
int rl_geo_recolor_ok(const RlGeoTexDesc* tex, int ntex, const RlGeoPalDesc* pal, int npal, unsigned int lo,
                      unsigned int hi);

#endif
