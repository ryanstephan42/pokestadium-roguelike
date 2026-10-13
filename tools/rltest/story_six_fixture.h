#ifndef STORY_SIX_FIXTURE_H
#define STORY_SIX_FIXTURE_H

#include "src/story/sc_graph.h"

typedef void (*ScSixTrace)(const char *operation, unsigned index,
                           const ScState *state, int status, void *context);

int sc_six_checkpoint(ScState *output, const ScCatalog *catalog,
                      ScSixTrace trace, void *context);
/* HOST preparation only: legal all-species random Ditto starter seed. */
#ifndef STORY_SIX_PROBE
int sc_six_transform_checkpoint(ScState *output, const ScCatalog *catalog,
                                ScSixTrace trace, void *context);
#endif

#endif
