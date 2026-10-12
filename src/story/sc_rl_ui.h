#ifndef SC_RL_UI_H
#define SC_RL_UI_H

/* The persistent story image (candidate native UI) links its own copy of the
   unmodified fragment-RL UI source (sc_rl_ui.c), so it never binds a call to
   the transient fragmentRL (0xe8). The copy owns its statics: icon archive and
   name tables acquired by sc_rl_ui_init. Model helpers (rl_ui_model.c) and
   rl_text are resident and keep their names. Other builds are unchanged. */
#if defined(STORY_SERVICE_CANDIDATE) && defined(STORY_NATIVE_UI)
#define rl_ui_init sc_rl_ui_init
#define rl_ui_story_init sc_rl_ui_story_init
#define rl_ui_story_icon_cached sc_rl_ui_story_icon_cached
#define rl_ui_story_icon_prefetch sc_rl_ui_story_icon_prefetch
#define rl_ui_move_name sc_rl_ui_move_name
#define rl_ui_species_name sc_rl_ui_species_name
#define rl_ui_text_wrap sc_rl_ui_text_wrap
#define rl_ui_window sc_rl_ui_window
#define rl_ui_text sc_rl_ui_text
#define rl_ui_list sc_rl_ui_list
#define rl_ui_list_draw sc_rl_ui_list_draw
#define rl_ui_mon_icon sc_rl_ui_mon_icon
#define rl_ui_hp_bar sc_rl_ui_hp_bar
#define rl_ui_demo_init sc_rl_ui_demo_init
#define rl_ui_demo_update sc_rl_ui_demo_update
#define rl_ui_demo_draw sc_rl_ui_demo_draw
#define rl_ui_mon_detail sc_rl_ui_mon_detail
#endif
#include "src/fragments/rl/rl_ui.h"

#endif
