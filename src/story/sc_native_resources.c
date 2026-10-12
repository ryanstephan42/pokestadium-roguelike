#ifdef STORY_NATIVE_UI
#include "sc_native_resources.h"
#include "sc_native_font.h"
#include "sc_native_icons.h"
#include "sc_resource_arena.h"
#include "src/2E110.h"
#include "src/memory.h"
#include "src/stage_loader.h"
#include "src/dma.h"

extern u8 pokemon_icons_ROM_START[], pokemon_icons_ROM_END[];
extern u8 textdata_ROM_END[];
extern OSMesgQueue D_800A62C0;

/* Restore records hold caller slot pointers, which may lie in the mapped
   story image; it must stay mapped while any record is outstanding. */
static u32 outstanding;
u32 sc_native_resources_outstanding(void) { return outstanding; }

typedef struct ScUiResourceRestore {
    BinArchive **icon_slot, *icons;
    char ***move_slot, ***species_slot, **moves, **species;
} ScUiResourceRestore;

static void restore_resources(MainPoolBlock *block, u32 arg) {
    ScUiResourceRestore *restore = (ScUiResourceRestore *)block;
    (void)arg;
    *restore->icon_slot = restore->icons;
    *restore->move_slot = restore->moves;
    *restore->species_slot = restore->species;
    if (outstanding) --outstanding;
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

typedef struct ScUpperRestore {
    ScUiResourceRestore slots;
    unk_D_800AC870 *font;
    ScResourceArena storage;
    int acquired;
} ScUpperRestore;

static ScUpperRestore *upper;

static int same_bytes(const void *a, const void *b, unsigned count) {
    const u8 *left = a, *right = b;
    while (count--) if (*left++ != *right++) return 0;
    return 1;
}

static int read_rom(u8 *out, u8 *start, unsigned bytes) {
    OSMesg result;
    unsigned chunk;
    while (bytes) {
        chunk = bytes > 0x1000 ? 0x1000 : bytes;
        if (func_80000E80(0, out, (s32)start, chunk, (s32)&D_800A62C0, 0) ||
            osRecvMesg(&D_800A62C0, &result, 1) || result) return 0;
        out += chunk; start += chunk; bytes -= chunk;
    }
    return 1;
}

static void restore_upper_slots(ScUpperRestore *restore) {
    D_800AC870 = restore->font;
    *restore->slots.icon_slot = restore->slots.icons;
    *restore->slots.move_slot = restore->slots.moves;
    *restore->slots.species_slot = restore->slots.species;
}

static void restore_upper(MainPoolBlock *block, u32 arg) {
    ScUpperRestore *restore = (ScUpperRestore *)block;
    (void)arg;
    if (restore->acquired && restore->storage.active) {
        /* A forced pool pop cannot stand in for display-task retirement. */
        if (upper == restore) upper = NULL;
        HAL_Printf(8, 8, "Story UI: resource scope popped before retirement");
        return;
    }
    restore_upper_slots(restore);
    if (restore->storage.active && !sc_resource_arena_close(&restore->storage, 1)) {
        HAL_Printf(8, 8, "Story UI: upper resource rollback failed");
        return;
    }
    if (upper == restore) upper = NULL;
    if (outstanding) --outstanding;
}

int sc_native_resources_retire(int tasks_retired) {
    if (!upper || !upper->storage.active || !tasks_retired) return 0;
    restore_upper_slots(upper);
    return sc_resource_arena_close(&upper->storage, tasks_retired);
}

static BinArchive *upper_table(ScResourceArena *storage, u8 *start, u8 *end,
                                unsigned count, u8 *scratch) {
    BinArchive *archive;
    unsigned bytes = sizeof(BinArchive) + count * sizeof(BinArchiveFile);
    if ((u32)(end - start) < bytes) return NULL;
    if (!read_rom(scratch, start, sizeof(BinArchive))) return NULL;
    if (((BinArchive *)scratch)->num_files != count) return NULL;
    archive = sc_resource_arena_take(storage, bytes);
    if (!archive) return NULL;
    if (!read_rom((u8 *)archive, start, bytes)) return NULL;
    archive->unk_00 = 2;
    archive->unk_04 = (u32)start;
    return archive;
}

static void *upper_file(ScResourceArena *storage, BinArchive *archive,
                        unsigned index, u8 *start, u8 *end, unsigned bytes,
                        int compressed, u8 *scratch) {
    BinArchiveFile *file;
    PERSSZP *header;
    void *data;
    u32 length = (u32)(end - start);
    if (!archive || index >= archive->num_files) return NULL;
    file = (BinArchiveFile *)((u8 *)archive + sizeof(*archive)) + index;
    if (file->offset > length || file->size > length - file->offset ||
        file->offset < sizeof(*archive) + archive->num_files * sizeof(*file))
        return NULL;
    if (compressed) {
        if (file->size < 40 || file->size > 16464) return NULL;
        if (!read_rom(scratch, start + file->offset, file->size)) return NULL;
        header = (PERSSZP *)scratch;
        if (!same_bytes(header->magic, "PERS-SZP", 8) || header->header_size != 24 ||
            header->decompressed_size1 != bytes || header->decompressed_size2 != bytes ||
            header->unk_14 || !same_bytes(scratch + 24, "Yay0", 4) ||
            ((Yay0 *)(scratch + 24))->decompressed_size != bytes) return NULL;
    } else if (file->size != bytes) return NULL;
    data = sc_resource_arena_take(storage, bytes);
    if (!data) return NULL;
    if (compressed) func_80003890(scratch, data);
    else if (!read_rom(data, start + file->offset, file->size)) return NULL;
    file->unk_08 = (u32)data;
    return data;
}

int sc_native_resources_acquire_upper(ScMaArena *arena, BinArchive **icons,
                                      char ***moves, char ***species) {
    static const unsigned glyph_bytes[3] = {37264, 69744, 83664};
    ScUpperRestore *restore;
    unk_D_800AC870 *font;
    BinArchive *fonts, *names;
    ret_func_8000484C *glyphs[3];
    u8 *scratch;
    unsigned i;
    if (!arena || !icons || !moves || !species || upper || outstanding) return 0;
    restore = main_pool_alloc_with_func(sizeof(*restore), 0, 0, restore_upper);
    if (!restore) return 0;
    bzero(restore, sizeof(*restore));
    restore->slots.icon_slot = icons; restore->slots.icons = *icons;
    restore->slots.move_slot = moves; restore->slots.moves = *moves;
    restore->slots.species_slot = species; restore->slots.species = *species;
    restore->font = D_800AC870;
    upper = restore;
    ++outstanding;
    *icons = NULL; *moves = *species = NULL;
    /* Target descriptor rounds to96; host pointer width may require more. */
    if (!sc_resource_arena_open(&restore->storage, arena,
            SC_NATIVE_RESOURCE_BYTES - 96 + ((sizeof(*font) + 15) & ~15U))) return 0;
    font = sc_resource_arena_take(&restore->storage, sizeof(*font));
    scratch = sc_resource_arena_take(&restore->storage, 16464);
    if (!font || !scratch) return 0;
    fonts = upper_table(&restore->storage, fonts_ROM_START, fonts_ROM_END, 6, scratch);
    *icons = upper_table(&restore->storage, pokemon_icons_ROM_START,
                          pokemon_icons_ROM_END, 152, scratch);
    if (!fonts || !*icons) return 0;
    /* Checked story cache never falls back to the native pool loader. */
    (*icons)->unk_00 |= SC_NATIVE_ICON_UPPER_CACHE;
    /* The text table is acquisition scratch, not retained by name getters. */
    if ((u32)(textdata_ROM_END - textdata_ROM_START) < 752) return 0;
    if (!read_rom(scratch, textdata_ROM_START, 752)) return 0;
    names = (BinArchive *)scratch;
    if (names->num_files != 46) return 0;
    *species = upper_file(&restore->storage, names, 36, textdata_ROM_START,
                           textdata_ROM_END, 1872, 0, scratch);
    *moves = upper_file(&restore->storage, names, 37, textdata_ROM_START,
                         textdata_ROM_END, 2224, 0, scratch);
    if (!*species || !*moves) return 0;
    for (i = 0; i < 3; ++i) {
        glyphs[i] = upper_file(&restore->storage, fonts, i + 2, fonts_ROM_START,
                                fonts_ROM_END, glyph_bytes[i], 1, scratch);
        if (!glyphs[i]) return 0;
    }
    for (i = 0; i < 151; ++i)
        if (!upper_file(&restore->storage, *icons, i, pokemon_icons_ROM_START,
                         pokemon_icons_ROM_END, 3200, 0, scratch)) return 0;
    if (!sc_native_font_install(font, fonts, glyphs)) return 0;
    restore->acquired = 1;
    return 1;
}
#endif
