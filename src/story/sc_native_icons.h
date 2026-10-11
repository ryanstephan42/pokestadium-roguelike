#ifndef SC_NATIVE_ICONS_H
#define SC_NATIVE_ICONS_H
#ifdef STORY_NATIVE_UI
#include "src/3FB0.h"
int sc_native_icon_cached(BinArchive *archive, unsigned species);
int sc_native_icon_prefetch(BinArchive *archive, unsigned species);
#endif
#endif
