#include "global.h"
#include "rl_main.h"
#include "src/controller.h"
#include "src/29BA0.h"

extern u8 fragmentRL_TEXT_START[];
extern u8 fragmentRL_ROM_START[];
extern u8 fragmentRL_relocs_ROM_END[];

s32 func_80029008(s32 arg0, u8* romStart, u8* romEnd, u32 arg3, u32 arg4);

#ifdef RL_HAVE_BATTLE
void rl_battle_test_entry(void);
#endif

RlMainState gRlMain;

void rl_main_state_roguelike(void) {
    s32 next;

    gRlMain.magic = RL_MAIN_MAGIC;
    gRlMain.visits++;
    if (gRlMain.returnState == 0) {
        gRlMain.returnState = STATE_MENU_SELECT;
    }

    next = FRAGMENT_LOAD_AND_CALL(fragmentRL, 0, 0);
    gRlMain.lastResult = next;

    gCurrentGameState = (next > 0) ? next : gRlMain.returnState;
}

void rl_main_state_battle_test(void) {
#ifdef RL_HAVE_BATTLE
    rl_battle_test_entry();
#else
    gCurrentGameState = STATE_ROGUELIKE;
#endif
}

s32 rl_main_menu_hook(s32 state) {
    /* Hold L while confirming any Menu Select entry to enter Roguelike mode. */
    if ((state > 0) && (state != STATE_TITLE_SCREEN) && (state != STATE_MENU_SELECT) &&
        BTN_IS_DOWN(gPlayer1Controller, L_TRIG)) {
        gRlMain.returnState = STATE_MENU_SELECT;
        return STATE_ROGUELIKE;
    }
    return state;
}
