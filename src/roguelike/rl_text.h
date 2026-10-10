#ifndef RL_TEXT_H
#define RL_TEXT_H

typedef enum RlTextId {
    RL_TEXT_TITLE,
    RL_TEXT_DEMO_LIST,
    RL_TEXT_CONTROLS,
    RL_TEXT_EMPTY,
    RL_TEXT_ICON_ERROR,
    RL_TEXT_HP,
    RL_TEXT_BULBASAUR,
    RL_TEXT_COUNT
} RlTextId;

extern const char* const rl_text[RL_TEXT_COUNT];
extern const char* const rl_text_demo_items[10];

#endif
