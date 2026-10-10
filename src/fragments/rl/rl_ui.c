#ifdef ROGUELIKE
#include "rl_ui.h"
#include "src/1CF30.h"
#include "src/20470.h"
#include "src/3FB0.h"
#include "src/49790.h"
#include "src/roguelike/rl_text.h"
#include "src/stage_loader.h"

extern u8 pokemon_icons_ROM_START[];
extern u8 pokemon_icons_ROM_END[];

static BinArchive* sIconArchive;
static const Color_RGBA8 sWhite = { 255, 255, 255, 255 };
static const Color_RGBA8 sGold = { 255, 224, 96, 255 };
static const Color_RGBA8 sDisabled = { 128, 144, 160, 255 };
static RlUiList sDemoList;
static RlUiItem sDemoItems[10];
static s32 sDemoSelection;

s32 rl_ui_init(void) {
    sIconArchive = NULL;
    if (D_800AC870 == NULL) {
        func_8001E94C(0x1C, 0);
    } else {
        func_8001E9F0(0x1C);
    }
    sIconArchive = ASSET_LOAD2(pokemon_icons, 1, 0);
    if (D_800AC870 == NULL || sIconArchive == NULL || sIconArchive->num_files < 151) {
        HAL_Printf(8, 8, "RL UI: resources unavailable");
        return RL_UI_INVALID;
    }
    return RL_UI_NONE;
}

void rl_ui_window(s16 x, s16 y, s16 w, s16 h) {
    if (w < 4 || h < 4) {
        HAL_Printf(8, 8, "RL UI: invalid window");
        return;
    }
    func_8001F504(x, y, w, h, 96, 136, 208, 255);
    func_8001F504(x + 2, y + 2, w - 4, h - 4, 20, 32, 64, 240);
}

void rl_ui_text(s16 x, s16 y, const char* str, Color_RGBA8 color) {
    s16 pen = x - 1;

    if (str == NULL || D_800AC870 == NULL) {
        HAL_Printf(8, 8, "RL UI: text/font unavailable");
        return;
    }
    func_8001F3F4();
    func_8001EBE0(8, 0);
    func_8001F324(color.r, color.g, color.b, color.a);
    /* Iterate raw font bytes so '%' stays literal and long strings stay bounded by NUL. */
    while (*str != '\0') {
        if (*str == '\n') {
            pen = x - 1;
            y += 20;
        } else {
            pen = func_8001ECB4(pen, y, (u8)*str);
        }
        str++;
    }
    func_8001F444();
}

s32 rl_ui_list(RlUiList* list, u16 pressed) {
    s32 old_cursor;
    s32 result;

    if (list == NULL) {
        HAL_Printf(8, 8, "RL UI: invalid list");
        return RL_UI_INVALID;
    }
    old_cursor = list->cursor;
    result = rl_ui_list_step(list, pressed);
    if (result == RL_UI_INVALID) {
        HAL_Printf(8, 8, "RL UI: invalid list");
    } else if (result == RL_UI_CANCEL) {
        func_80048B90(3);
    } else if (result == RL_UI_DISABLED) {
        func_80048B90(0x1D);
    } else if (result >= 0) {
        func_80048B90(2);
    } else if (old_cursor != list->cursor) {
        func_80048B90(1);
    }
    return result;
}

void rl_ui_list_draw(const RlUiList* list, s16 x, s16 y, s16 w) {
    s32 row;
    s32 index;
    s16 item_y;
    Color_RGBA8 color;
    s16 window_h;

    if (!rl_ui_list_valid(list) || list->visible > 12 || w < 48) {
        HAL_Printf(8, 8, "RL UI: invalid list geometry");
        return;
    }
    window_h = 54 + list->visible * 26;
    rl_ui_window(x, y, w, window_h);
    rl_ui_text(x + 12, y + 8, list->title, sGold);
    if (list->count == 0) {
        rl_ui_text(x + 12, y + 34, rl_text[RL_TEXT_EMPTY], sDisabled);
    }
    for (row = 0; row < list->visible && row + list->scroll < list->count; row++) {
        index = row + list->scroll;
        item_y = y + 34 + row * 26;
        if (index == list->cursor) {
            func_8001F504(x + 6, item_y - 2, w - 12, 24, 48, 72, 120, 255);
            rl_ui_text(x + 10, item_y, ">", sGold);
        }
        color = list->items[index].disabled ? sDisabled : sWhite;
        rl_ui_text(x + 30, item_y, list->items[index].text, color);
    }
    if (list->scroll > 0) {
        rl_ui_text(x + w - 20, y + 8, "^", sGold);
    }
    if (list->scroll + list->visible < list->count) {
        rl_ui_text(x + w - 20, y + window_h - 24, "v", sGold);
    }
}

s32 rl_ui_mon_icon(s32 species, s16 x, s16 y) {
    BinArchiveFile* file;
    u8* pixels;

    if (sIconArchive == NULL || species < 1 || species > 151 || species > sIconArchive->num_files) {
        rl_ui_text(x, y, rl_text[RL_TEXT_ICON_ERROR], sDisabled);
        return RL_UI_INVALID;
    }
    file = (BinArchiveFile*)((u8*)sIconArchive + sizeof(BinArchive)) + species - 1;
    if (file->size != 40 * 40 * 2) {
        rl_ui_text(x, y, rl_text[RL_TEXT_ICON_ERROR], sDisabled);
        return RL_UI_INVALID;
    }
    pixels = func_8000484C(sIconArchive, species - 1);
    if (pixels == NULL) {
        rl_ui_text(x, y, rl_text[RL_TEXT_ICON_ERROR], sDisabled);
        return RL_UI_INVALID;
    }
    gSPDisplayList(gDisplayListHead++, D_8006F518);
    func_8001C6AC(x, y, 40, 40, pixels, 40, 0);
    gSPDisplayList(gDisplayListHead++, D_8006F630);
    return RL_UI_NONE;
}

void rl_ui_hp_bar(s16 x, s16 y, s16 w, u16 hp, u16 max_hp) {
    s32 fill;
    s32 r = 64;
    s32 g = 208;

    if (w < 4) {
        HAL_Printf(8, 8, "RL UI: invalid HP bar");
        return;
    }
    fill = rl_ui_hp_width(w - 4, hp, max_hp);
    if (fill < 0) {
        HAL_Printf(8, 8, "RL UI: invalid HP");
        return;
    }
    if ((u32)hp * 5 <= max_hp) {
        r = 232;
        g = 64;
    } else if ((u32)hp * 2 <= max_hp) {
        r = 232;
        g = 200;
    }
    func_8001F504(x, y, w, 12, 200, 216, 240, 255);
    func_8001F504(x + 2, y + 2, w - 4, 8, 12, 20, 32, 255);
    if (fill > 0) {
        func_8001F504(x + 2, y + 2, fill, 8, r, g, 64, 255);
    }
}

s32 rl_ui_demo_init(void) {
    s32 i;
    s32 result;

    sDemoSelection = RL_UI_NONE;
    for (i = 0; i < 10; i++) {
        sDemoItems[i].text = rl_text_demo_items[i];
        sDemoItems[i].disabled = i == 5;
    }
    result = rl_ui_init();
    if (result != RL_UI_NONE) {
        return result;
    }
    return rl_ui_list_init(&sDemoList, rl_text[RL_TEXT_DEMO_LIST], sDemoItems, 10, 5);
}

s32 rl_ui_demo_update(u16 pressed) {
    s32 result = rl_ui_list(&sDemoList, pressed);

    if (result >= 0) {
        sDemoSelection = result;
    }
    return result;
}

void rl_ui_demo_draw(void) {
    rl_ui_window(24, 24, 592, 432);
    rl_ui_text(44, 40, rl_text[RL_TEXT_TITLE], sGold);
    rl_ui_list_draw(&sDemoList, 44, 78, 350);
    rl_ui_mon_icon(1, 476, 108);
    rl_ui_text(428, 158, rl_text[RL_TEXT_BULBASAUR], sWhite);
    rl_ui_text(428, 190, rl_text[RL_TEXT_HP], sWhite);
    rl_ui_hp_bar(428, 216, 160, 65, 100);
    rl_ui_text(428, 238, "65 / 100", sWhite);
    if (sDemoSelection >= 0) {
        rl_ui_text(44, 348, sDemoItems[sDemoSelection].text, sGold);
    }
    rl_ui_text(44, 416, rl_text[RL_TEXT_CONTROLS], sWhite);
}
#endif
