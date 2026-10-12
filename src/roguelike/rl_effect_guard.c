/* PURE */
#include "rl_effect_guard.h"

int rl_effect_dispatch(const void* tested, const void* actor, int idx, int count) {
    if (tested != actor) {
        return RL_EFFECT_SKIP;
    }
    if (idx < 1 || idx > count) {
        return RL_EFFECT_REFUSE;
    }
    return idx;
}
