#ifndef RL_UI_MODEL_H
#define RL_UI_MODEL_H

enum {
    RL_UI_NONE = -1,
    RL_UI_CANCEL = -2,
    RL_UI_INVALID = -3,
    RL_UI_DISABLED = -4
};

enum {
    RL_UI_UP = 0x0800,
    RL_UI_DOWN = 0x0400,
    RL_UI_LEFT = 0x0200,
    RL_UI_RIGHT = 0x0100,
    RL_UI_A = 0x8000,
    RL_UI_B = 0x4000
};

typedef struct RlUiItem {
    const char* text;
    int disabled;
} RlUiItem;

typedef struct RlUiList {
    const char* title;
    const RlUiItem* items;
    int count;
    int visible;
    int cursor;
    int scroll;
} RlUiList;

int rl_ui_list_init(RlUiList* list, const char* title, const RlUiItem* items, int count, int visible);
int rl_ui_list_step(RlUiList* list, unsigned int pressed);
int rl_ui_list_valid(const RlUiList* list);
int rl_ui_hp_width(int width, unsigned int hp, unsigned int max_hp);

#endif
