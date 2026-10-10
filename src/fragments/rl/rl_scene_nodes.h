#ifndef RL_SCENE_NODES_H
#define RL_SCENE_NODES_H

#ifdef ROGUELIKE
#include "global.h"
#include "src/roguelike/rl_core.h"

enum { RL_SCENE_BAG = 7, RL_SCENE_PARTY = 8 };
enum { RL_SCENE_ONGOING, RL_SCENE_COMPLETED, RL_SCENE_CANCEL };

/* Node types or independent map modes above. Caller finishes completed nodes. */
void rl_scene_nodes_begin(RunState* run, const RlPool* rentals, unsigned mode);
int rl_scene_nodes_update(u16 pressed);
void rl_scene_nodes_draw(void);
#endif

#endif
