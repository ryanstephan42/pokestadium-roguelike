#ifndef SC_NATIVE_FONT_H
#define SC_NATIVE_FONT_H
#ifdef STORY_NATIVE_UI
#include "src/1CF30.h"
#include "src/3FB0.h"
/* Must run inside the owned graphics pool scope. Pop restores borrowed font. */
int sc_native_font_acquire(void);
int sc_native_font_install(unk_D_800AC870 *, BinArchive *, ret_func_8000484C *[3]);
#endif
#endif
