#ifndef RL_EFFECT_GUARD_H
#define RL_EFFECT_GUARD_H

#define RL_EFFECT_SKIP 0
#define RL_EFFECT_REFUSE (-1)

/* Decide a native move-effect dispatch. Handlers act on the tested battler (D_843C5238), so the
   acting battler's effect is dispatched only when both are the same object; otherwise (the
   confusion self-hit swaps D_843C5238 without changing the actor) nothing is dispatched.
   Returns idx (1..count), RL_EFFECT_SKIP, or RL_EFFECT_REFUSE for an out-of-table index. */
int rl_effect_dispatch(const void* tested, const void* actor, int idx, int count);

#endif
