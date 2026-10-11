#ifndef SC_SCENE_H
#define SC_SCENE_H

#ifdef ROGUELIKE
#include "sc_ui.h"

/* Caller owns admitted backing and initialized shared UI/display-list scope. */
void sc_scene_draw(const ScUi *ui);
#endif

#endif
