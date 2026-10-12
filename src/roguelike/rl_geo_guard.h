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

#endif
