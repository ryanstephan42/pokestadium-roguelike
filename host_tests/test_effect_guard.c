#include <stdio.h>
#include <stdlib.h>
#include "rl_effect_guard.h"

static int fails;
#define CHECK(c)                                                  \
    do {                                                          \
        if (!(c)) {                                               \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);    \
            fails++;                                              \
        }                                                         \
    } while (0)

int main(void) {
    int battler[2];
    int i;

    /* Actor and tested battler agree: every table index dispatches, all others refuse. */
    for (i = -2; i <= 90; i++) {
        int r = rl_effect_dispatch(&battler[0], &battler[0], i, 87);
        if (i >= 1 && i <= 87) {
            CHECK(r == i);
        } else {
            CHECK(r == RL_EFFECT_REFUSE);
        }
    }
    /* Confusion self-hit: tested battler is the opponent; never dispatch, whatever the index. */
    for (i = -2; i <= 90; i++) {
        CHECK(rl_effect_dispatch(&battler[1], &battler[0], i, 87) == RL_EFFECT_SKIP);
    }
    CHECK(rl_effect_dispatch(&battler[1], &battler[0], 0x1d, 87) == RL_EFFECT_SKIP);
    CHECK(rl_effect_dispatch(&battler[0], &battler[0], 0x1d, 87) == 0x1d);
    CHECK(rl_effect_dispatch(0, &battler[0], 1, 87) == RL_EFFECT_SKIP);

    if (fails) {
        return EXIT_FAILURE;
    }
    printf("effect guard: OK\n");
    return 0;
}
