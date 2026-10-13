#ifdef STORY_NATIVE_UI
#include "global.h"
#include "sc_native_font.h"
#include "src/1CF30.h"
#include "src/3FB0.h"
#include "src/memory.h"
#include "src/stage_loader.h"

typedef struct ScFontRestore {
    unk_D_800AC870 *previous;
    unk_D_800AC870 value;
} ScFontRestore;

static void restore_font(MainPoolBlock *block, u32 arg) {
    ScFontRestore *restore = (ScFontRestore *)block;
    (void)arg;
    D_800AC870 = restore->previous;
    if (restore->previous) *restore->previous = restore->value;
}

static void release_font(MainPoolBlock *block, u32 arg) {
    (void)block;
    (void)arg;
    func_8001E7C0(0, 0);
}

static void release_glyph(MainPoolBlock *block, u32 arg) {
    (void)block;
    func_8001E7D4(0, arg);
}

int sc_native_font_install(unk_D_800AC870 *font, BinArchive *archive,
                           ret_func_8000484C *glyphs[3]) {
    static const s16 widths[3] = {0x10, 0x18, 0x18};
    static const s16 heights[3] = {0x10, 0x14, 0x18};
    unsigned i;
    if (!font || !archive || !glyphs) return 0;
    for (i = 0; i < 3; ++i) if (!glyphs[i]) return 0;
    bzero(font, sizeof(*font));
    font->unk_48 = archive;
    for (i = 0; i < 3; ++i) {
        font->unk_00[i + 2].unk_00 = widths[i];
        font->unk_00[i + 2].unk_02 = heights[i];
        font->unk_00[i + 2].unk_04 = (u8 *)glyphs[i];
        font->unk_00[i + 2].unk_08 = (u8 *)&glyphs[i]->unk_90;
    }
    font->unk_50 = 4;
    D_800AC870 = font;
    return 1;
}

int sc_native_font_acquire(void) {
    static const s16 widths[6] = {0x10, 0x10, 0x10, 0x18, 0x18, 0x20};
    static const s16 heights[6] = {0xA, 0xC, 0x10, 0x14, 0x18, 0x20};
    ScFontRestore *restore;
    BinArchive *archive;
    ret_func_8000484C *glyph;
    unsigned i;
    restore = main_pool_alloc_with_func(sizeof(*restore), 0, 0, restore_font);
    if (!restore) return 0;
    restore->previous = D_800AC870;
    if (D_800AC870) restore->value = *D_800AC870;
    else {
        D_800AC870 = main_pool_alloc_with_func(sizeof(*D_800AC870), 0, 0x50524E54, release_font);
        if (!D_800AC870) return 0;
        bzero(D_800AC870, sizeof(*D_800AC870));
        D_800AC870->unk_48 = ASSET_LOAD2(fonts, 1, 1);
    }
    archive = D_800AC870->unk_48;
    if (!archive || archive->num_files < 5 || !(archive->unk_00 & 4) ||
        (archive->unk_00 & 1)) return 0;
    for (i = 2; i <= 4; i++) {
        if (D_800AC870->unk_00[i].unk_04) {
            if (!D_800AC870->unk_00[i].unk_08) return 0;
            continue;
        }
        glyph = func_8000484C(archive, i);
        if (!glyph) return 0;
        D_800AC870->unk_50 = i;
        D_800AC870->unk_00[i].unk_00 = widths[i];
        D_800AC870->unk_00[i].unk_02 = heights[i];
        D_800AC870->unk_00[i].unk_04 = (u8 *)glyph;
        D_800AC870->unk_00[i].unk_08 = (u8 *)&glyph->unk_90;
        main_pool_set_func(glyph, i, release_glyph);
    }
    return 1;
}
#endif
