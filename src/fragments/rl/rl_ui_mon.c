#ifdef ROGUELIKE
#include "rl_ui.h"

static void mon_number(char* out, unsigned value, unsigned digits) {
    while (digits) {
        digits--;
        out[digits] = (char)('0' + value % 10);
        value /= 10;
    }
}

void rl_ui_mon_detail(const RlMon* mon, s16 x, s16 y) {
    static const Color_RGBA8 white = { 255, 255, 255, 255 };
    static const Color_RGBA8 gold = { 255, 224, 96, 255 };
    static const Color_RGBA8 muted = { 176, 192, 216, 255 };
    unsigned i;
    char level[] = "Level 000";
    char hp[] = "HP 000/000";
    char pp[] = "PP 00/00";
    if (mon == NULL || mon->species == 0) return;
    rl_ui_text_wrap(x, y, 200, 2, rl_ui_species_name(mon->species), gold);
    rl_ui_mon_icon(mon->species, x + 150, y + 40);
    mon_number(level + 6, mon->level, 3);
    rl_ui_text(x, y + 40, level, white);
    mon_number(hp + 3, mon->cur_hp, 3);
    mon_number(hp + 7, mon->max_hp, 3);
    rl_ui_text(x, y + 60, hp, white);
    rl_ui_hp_bar(x, y + 82, 190, mon->cur_hp, mon->max_hp);
    for (i = 0; i < 4; i++) {
        rl_ui_text_wrap(x, y + 96 + i * 44, 200, 1,
                        rl_ui_move_name(mon->moves[i]), white);
        mon_number(pp + 3, mon->pp[i], 2);
        mon_number(pp + 6, mon->max_pp[i], 2);
        rl_ui_text(x + 92, y + 118 + i * 44, pp, muted);
    }
}
#endif
