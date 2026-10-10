#include <assert.h>
#include "rl_ui_model.h"
#include "rl_text.h"

static const RlUiItem items[] = {
    { "one", 0 }, { "two", 1 }, { "three", 0 }, { "four", 0 },
    { "five", 0 }, { "six", 0 }, { "seven", 0 }, { "eight", 0 },
    { "nine", 0 }, { "ten", 0 }
};

int main(void) {
    RlUiList list;
    RlUiOrder order = {{0, 0, 0, 0, 0, 0}, 0};
    RlUiOrder saved;

    assert(rl_ui_order_toggle(&order, 4, 3) == RL_UI_NONE);
    assert(rl_ui_order_toggle(&order, 0, 3) == RL_UI_NONE);
    assert(rl_ui_order_toggle(&order, 2, 3) == RL_UI_NONE);
    assert(order.count == 3 && order.slots[0] == 4 && order.slots[2] == 2);
    saved = order;
    assert(rl_ui_order_toggle(&order, 1, 3) == RL_UI_DISABLED);
    assert(order.count == saved.count && order.slots[0] == saved.slots[0]);
    assert(rl_ui_order_move(&order, 2, -1) == RL_UI_NONE);
    assert(rl_ui_order_move(&order, 2, -1) == RL_UI_NONE);
    assert(order.slots[0] == 2 && order.slots[1] == 4 && order.slots[2] == 0);
    assert(rl_ui_order_move(&order, 2, -1) == RL_UI_DISABLED);
    assert(rl_ui_order_move(&order, 1, 1) == RL_UI_INVALID);
    assert(rl_ui_order_toggle(&order, 4, 3) == RL_UI_NONE);
    assert(order.count == 2 && order.slots[0] == 2 && order.slots[1] == 0);
    assert(rl_ui_order_toggle(&order, 4, 3) == RL_UI_NONE);
    assert(order.slots[2] == 4);
    assert(rl_ui_order_index(&order, 6) == RL_UI_INVALID);
    assert(rl_ui_order_toggle(&order, 6, 3) == RL_UI_INVALID);
    assert(rl_ui_order_move(&order, 2, 0) == RL_UI_INVALID);

    assert((unsigned char)rl_text[RL_TEXT_TITLE][3] == 0xE9);
    assert((unsigned char)rl_text_demo_items[1][3] == 0xE9);
    assert(rl_ui_list_init(&list, "title", items, 10, 5) == RL_UI_NONE);
    assert(rl_ui_list_valid(&list));
    assert(rl_ui_list_step(&list, RL_UI_DOWN) == RL_UI_NONE);
    assert(list.cursor == 1 && list.scroll == 0);
    assert(rl_ui_list_step(&list, RL_UI_A) == RL_UI_DISABLED);
    assert(rl_ui_list_step(&list, RL_UI_DOWN) == RL_UI_NONE);
    assert(rl_ui_list_step(&list, RL_UI_A) == 2);
    assert(rl_ui_list_step(&list, RL_UI_RIGHT) == RL_UI_NONE);
    assert(list.cursor == 7 && list.scroll == 3);
    assert(rl_ui_list_step(&list, RL_UI_DOWN) == RL_UI_NONE);
    assert(list.cursor == 8 && list.scroll == 4);
    assert(rl_ui_list_step(&list, RL_UI_RIGHT) == RL_UI_NONE);
    assert(list.cursor == 9 && list.scroll == 5);
    assert(rl_ui_list_step(&list, RL_UI_DOWN) == RL_UI_NONE);
    assert(list.cursor == 9 && list.scroll == 5);
    assert(rl_ui_list_step(&list, RL_UI_LEFT) == RL_UI_NONE);
    assert(list.cursor == 4 && list.scroll == 4);
    assert(rl_ui_list_step(&list, RL_UI_UP | RL_UI_B) == RL_UI_CANCEL);

    assert(rl_ui_list_init(&list, "empty", 0, 0, 3) == RL_UI_NONE);
    assert(rl_ui_list_step(&list, RL_UI_A) == RL_UI_NONE);
    assert(rl_ui_list_step(&list, RL_UI_B) == RL_UI_CANCEL);
    assert(rl_ui_list_init(&list, "bad", items, 10, 0) == RL_UI_INVALID);
    assert(rl_ui_hp_width(100, 25, 100) == 25);
    assert(rl_ui_hp_width(100, 150, 100) == 100);
    assert(rl_ui_hp_width(100, 0, 100) == 0);
    assert(rl_ui_hp_width(100, 10, 0) == RL_UI_INVALID);
    assert(rl_ui_hp_width(641, 10, 100) == RL_UI_INVALID);
    return 0;
}
