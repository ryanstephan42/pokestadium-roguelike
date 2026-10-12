#ifdef STORY_NATIVE_UI
#include "sc_native_resources.h"
#include "sc_native_font.h"
#include "src/2E110.h"
#include "src/memory.h"

extern u8 pokemon_icons_ROM_START[], pokemon_icons_ROM_END[];

typedef struct ScUiResourceRestore {
    BinArchive **icon_slot, *icons;
    char ***move_slot, ***species_slot, **moves, **species;
} ScUiResourceRestore;

/* Restore records hold caller slot pointers, which may lie in the mapped
   story image; it must stay mapped while any record is outstanding. */
static u32 outstanding;

u32 sc_native_resources_outstanding(void) { return outstanding; }

static void restore_resources(MainPoolBlock *block, u32 arg) {
    ScUiResourceRestore *restore = (ScUiResourceRestore *)block;
    (void)arg;
    if (outstanding) --outstanding;
    *restore->icon_slot = restore->icons;
    *restore->move_slot = restore->moves;
    *restore->species_slot = restore->species;
}

int sc_native_resources_acquire(BinArchive **icons, char ***moves, char ***species) {
    ScUiResourceRestore *restore;
    if (!icons || !moves || !species) return 0;
    restore = main_pool_alloc_with_func(sizeof(*restore), 0, 0, restore_resources);
    if (!restore) return 0;
    ++outstanding;
    restore->icon_slot = icons; restore->icons = *icons;
    restore->move_slot = moves; restore->moves = *moves;
    restore->species_slot = species; restore->species = *species;
    *icons = NULL; *moves = *species = NULL;
    if (!sc_native_font_acquire()) return 0;
    *icons = ASSET_LOAD2(pokemon_icons, 1, 0);
    if (!*icons || (*icons)->num_files < 151 || !func_8002D510Checked()) return 0;
    *moves = func_8002D5AC(0x25);
    *species = func_8002D5AC(0x24);
    return *moves != NULL && *species != NULL;
}
#endif
