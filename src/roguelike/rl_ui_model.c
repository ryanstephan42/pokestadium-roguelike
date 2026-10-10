/* PURE: list navigation and HP geometry, with no platform dependencies. */
#ifdef ROGUELIKE
#include "rl_ui_model.h"

int rl_ui_list_valid(const RlUiList* list) {
    int i;
    int max_scroll;

    if (list == 0 || list->title == 0 || list->count < 0 || list->count > 32767 ||
        list->visible < 1 || list->visible > 32767 || (list->count != 0 && list->items == 0)) {
        return 0;
    }
    if (list->count == 0) {
        return list->cursor == 0 && list->scroll == 0;
    }

    max_scroll = list->count > list->visible ? list->count - list->visible : 0;
    if (list->cursor < 0 || list->cursor >= list->count || list->scroll < 0 ||
        list->scroll > list->cursor || list->cursor >= list->scroll + list->visible ||
        list->scroll > max_scroll) {
        return 0;
    }
    for (i = 0; i < list->count; i++) {
        if (list->items[i].text == 0) {
            return 0;
        }
    }
    return 1;
}

int rl_ui_list_init(RlUiList* list, const char* title, const RlUiItem* items, int count, int visible) {
    RlUiList value;

    value.title = title;
    value.items = items;
    value.count = count;
    value.visible = visible;
    value.cursor = 0;
    value.scroll = 0;
    if (list == 0 || !rl_ui_list_valid(&value)) {
        return RL_UI_INVALID;
    }
    *list = value;
    return RL_UI_NONE;
}

int rl_ui_list_step(RlUiList* list, unsigned int pressed) {
    int next;

    if (!rl_ui_list_valid(list)) {
        return RL_UI_INVALID;
    }
    if (pressed & RL_UI_B) {
        return RL_UI_CANCEL;
    }
    if (list->count == 0) {
        return RL_UI_NONE;
    }

    next = list->cursor;
    if (pressed & RL_UI_UP) {
        next--;
    } else if (pressed & RL_UI_DOWN) {
        next++;
    } else if (pressed & RL_UI_LEFT) {
        next -= list->visible;
    } else if (pressed & RL_UI_RIGHT) {
        next += list->visible;
    } else if (pressed & RL_UI_A) {
        return list->items[next].disabled ? RL_UI_DISABLED : next;
    }

    if (next < 0) {
        next = 0;
    } else if (next >= list->count) {
        next = list->count - 1;
    }
    list->cursor = next;
    if (next < list->scroll) {
        list->scroll = next;
    } else if (next >= list->scroll + list->visible) {
        list->scroll = next - list->visible + 1;
    }
    return RL_UI_NONE;
}

int rl_ui_hp_width(int width, unsigned int hp, unsigned int max_hp) {
    if (width < 0 || width > 640 || max_hp == 0 || max_hp > 65535) {
        return RL_UI_INVALID;
    }
    if (hp > max_hp) {
        hp = max_hp;
    }
    return (int)((hp * (unsigned int)width) / max_hp);
}
#endif
