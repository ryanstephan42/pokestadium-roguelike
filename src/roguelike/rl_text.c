/* PURE: ASCII plus Stadium's single-byte extensions, never UTF-8. */
#ifdef ROGUELIKE
#include "rl_text.h"

const char* const rl_text[RL_TEXT_COUNT] = {
    "Pok\xe9" "mon Stadium Roguelike",
    "Choose your next stop",
    "D-pad: move/page   A: select   B: return",
    "No choices available",
    "Icon unavailable",
    "HP",
    "Bulbasaur"
};

const char* const rl_text_demo_items[10] = {
    "Trainer battle",
    "Pok\xe9" "mon Center",
    "Pok\xe9" " Mart",
    "Recruit a Pok\xe9" "mon",
    "Mt. Moon fossil",
    "Move tutor (locked)",
    "Rare Candy",
    "Gym Leader",
    "Elite Four",
    "Mewtwo"
};
#endif
