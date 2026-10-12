#ifndef SC_AI_RUNTIME_H
#define SC_AI_RUNTIME_H

#include "global.h"
#include "src/memmap.h"
#include "sc_ai_overlay.h"

enum { SC_AI_ID = 233 };
typedef struct ScAiFatal {
    u32 code, owner, generation, address, size, side, forced, choice;
} ScAiFatal;
extern volatile ScAiFatal gScAiFatal;

void sc_ai_fatal(unsigned code);
unsigned sc_ai_load_begin(unsigned owner);
void sc_ai_load_end(unsigned owner, unsigned token, Fragment *fragment);
void sc_ai_unload_begin(unsigned owner, unsigned token);
void sc_ai_unload_end(unsigned owner, unsigned token);
void sc_ai_loader_admit(unsigned id);
unsigned sc_ai_callback_tag(unsigned id);
int sc_ai_callback_clear(unsigned base, unsigned tag);
void sc_ai_map_set(unsigned id, unsigned base, unsigned size);
void sc_ai_map_clear(unsigned id);
void sc_ai_reference_admit(unsigned id);
int sc_ai_dispatch(unsigned owner, unsigned token, int command, void *request);
void sc_ai_battle_load(void);
void sc_ai_battle_unload(void);
void sc_ai_battle_popped(void);
unsigned sc_ai_battle_token(void);
int sc_ai_native_choose(unk_D_84390010 *side, int forced);
void sc_ai_native_validate(unk_D_84390010 *side, int choice, int forced);

#endif
