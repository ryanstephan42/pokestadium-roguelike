#ifndef RL_PLAY_H
#define RL_PLAY_H

#ifdef ROGUELIKE
#include "global.h"

s32 rl_play_init(void);
s32 rl_play_update(u16 pressed);
void rl_play_draw(void);
#endif

#endif
