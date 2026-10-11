#ifndef SC_NATIVE_FONT_H
#define SC_NATIVE_FONT_H
#ifdef STORY_NATIVE_UI
/* Must run inside the owned graphics pool scope. Pop restores borrowed font. */
int sc_native_font_acquire(void);
#endif
#endif
