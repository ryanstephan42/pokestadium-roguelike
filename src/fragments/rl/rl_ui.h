#ifndef RL_UI_H
#define RL_UI_H

#ifdef ROGUELIKE
#include "global.h"
#include "src/roguelike/rl_ui_model.h"
#include "src/roguelike/rl_core.h"

/* Call after creating the scene's display-list workspace, inside its pool scope. */
s32 rl_ui_init(void);
void rl_ui_window(s16 x, s16 y, s16 w, s16 h);
void rl_ui_text(s16 x, s16 y, const char* str, Color_RGBA8 color);
void rl_ui_text_wrap(s16 x, s16 y, s16 width, unsigned max_lines,
                     const char* str, Color_RGBA8 color);
const char* rl_ui_move_name(unsigned move);
const char* rl_ui_species_name(unsigned species);
s32 rl_ui_list(RlUiList* list, u16 pressed);
void rl_ui_list_draw(const RlUiList* list, s16 x, s16 y, s16 w);
s32 rl_ui_mon_icon(s32 species, s16 x, s16 y);
void rl_ui_hp_bar(s16 x, s16 y, s16 w, u16 hp, u16 max_hp);
void rl_ui_mon_detail(const RlMon* mon, s16 x, s16 y);

s32 rl_ui_demo_init(void);
s32 rl_ui_demo_update(u16 pressed);
void rl_ui_demo_draw(void);
#endif

#endif
