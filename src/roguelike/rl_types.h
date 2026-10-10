#ifndef RL_TYPES_H
#define RL_TYPES_H

/* Keep these aliases usable by both host C89 and the game's IDO compiler. */
typedef unsigned char RlU8;
typedef unsigned short RlU16;
typedef unsigned int RlU32;
typedef signed int RlS32;
typedef char RlAssertU8[(sizeof(RlU8) == 1) ? 1 : -1];
typedef char RlAssertU16[(sizeof(RlU16) == 2) ? 1 : -1];
typedef char RlAssertU32[(sizeof(RlU32) == 4) ? 1 : -1];

#endif
