#ifndef RL_GEO_GUARD_H
#define RL_GEO_GUARD_H

#define RL_GEO_OPCODES 39
#define RL_GEO_STACK_WORDS 16
#define RL_GEO_NODES 32

/* sizes[op] is the exact command extent; 0 marks a command that cannot advance. */
int rl_geo_admit(const unsigned char* cmd, const unsigned char* mem_lo, const unsigned char* mem_hi,
                 const unsigned char* sizes, int stack, int ret, int node);
int rl_geo_state_ok(int stack, int ret, int node);

#endif
