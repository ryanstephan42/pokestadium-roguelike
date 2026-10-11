#ifndef STORY_EMBER_FIXTURE_H
#define STORY_EMBER_FIXTURE_H

#include "src/story/sc_core.h"

typedef void (*ScEmberTrace)(const char *operation, unsigned index,
                             const ScState *state, int status, void *context);

int sc_ember_checkpoint(ScState *output, const ScCatalog *catalog,
                        ScEmberTrace trace, void *context);

#endif
