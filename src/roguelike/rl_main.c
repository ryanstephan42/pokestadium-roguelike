#include "global.h"
#include "rl_main.h"
#include "src/controller.h"
#include "src/29BA0.h"
#include "rl_save_probe.h"

extern u8 fragmentRL_TEXT_START[];
extern u8 fragmentRL_ROM_START[];
extern u8 fragmentRL_relocs_ROM_END[];

s32 func_80029008(s32 arg0, u8* romStart, u8* romEnd, u32 arg3, u32 arg4);

#ifdef RL_HAVE_BATTLE
void rl_battle_test_entry(void);
#endif

RlMainState gRlMain;
RunState gRlRun;
RlPendingBattle gRlPendingBattle;
volatile u32 gRlPlayScene;
volatile u32 gRlPlayExit;
volatile u32 gRlPlayBattleCount;
volatile u32 gRlPlayTestMode;
volatile u32 gRlPlayTestSeed;
volatile u8 gRlPlayDraftSpecies[6];

int rl_main_queue_battle(const RlMon* player, unsigned player_count,
                         const RlMon* enemy, unsigned enemy_count,
                         const RlBattleRules* rules,
                         const unsigned char* player_slots,
                         const RlMon* trainer, unsigned trainer_count) {
    unsigned i;

    if (gRlPendingBattle.pending || gRlPendingBattle.post_battle ||
        player == NULL || enemy == NULL || rules == NULL ||
        player_slots == NULL || trainer == NULL ||
        player_count == 0 || player_count > 3 ||
        enemy_count == 0 || enemy_count > RL_BATTLE_MAX_MONS ||
        trainer_count == 0 || trainer_count > RL_BATTLE_MAX_MONS) {
        return 0;
    }
    for (i = 0; i < player_count; i++) {
        gRlPendingBattle.player[i] = player[i];
        gRlPendingBattle.player_slots[i] = player_slots[i];
    }
    for (i = 0; i < enemy_count; i++) {
        gRlPendingBattle.enemy[i] = enemy[i];
    }
    for (i = 0; i < trainer_count; i++) {
        gRlPendingBattle.trainer[i] = trainer[i];
    }
    gRlPendingBattle.rules = *rules;
    gRlPendingBattle.player_count = (unsigned char)player_count;
    gRlPendingBattle.enemy_count = (unsigned char)enemy_count;
    gRlPendingBattle.trainer_count = (unsigned char)trainer_count;
    gRlPendingBattle.pending = 1;
    gRlPendingBattle.error = 0;
    return 1;
}

static void rl_main_run_pending_battle(void) {
    RlLaunchResult result;
    unsigned active_mask;
    unsigned i;
    int outcome;

    gCurrentGameState = STATE_ROGUELIKE_BATTLE_TEST;
    outcome = rl_start_battle(gRlPendingBattle.player,
                              gRlPendingBattle.player_count,
                              gRlPendingBattle.enemy,
                              gRlPendingBattle.enemy_count,
                              &gRlPendingBattle.rules, STATE_ROGUELIKE,
                              &result);
    gRlPendingBattle.pending = 0;
    gRlPendingBattle.outcome = outcome < 0 ? 0xFF : (unsigned char)outcome;
    gRlPendingBattle.error = outcome < 0 ? 1 : 0;
    active_mask = 0;
    if (outcome >= 0) {
        for (i = 0; i < gRlPendingBattle.player_count; i++) {
            if (gRlPendingBattle.player_slots[i] >= gRlRun.party_count) {
                gRlPendingBattle.error = 2;
                break;
            }
            active_mask |= 1U << gRlPendingBattle.player_slots[i];
        }
        for (i = 0; !gRlPendingBattle.error && i < result.player_count; i++) {
            if (result.player_index[i] >= gRlPendingBattle.player_count) {
                gRlPendingBattle.error = 2;
                break;
            }
        }
        if (!gRlPendingBattle.error) {
            for (i = 0; i < result.player_count; i++) {
                unsigned slot;
                slot = gRlPendingBattle.player_slots[result.player_index[i]];
                gRlRun.party[slot] = result.player[i];
            }
            if (rl_run_on_battle_result(&gRlRun, outcome == RL_BATTLE_WIN,
                                        (RlU8)active_mask) != RL_OK) {
                gRlPendingBattle.error = 3;
            }
        }
    }
    gRlPendingBattle.post_battle = 1;
    gRlPlayBattleCount = gRlRun.stats.battles;
}

void rl_main_state_roguelike(void) {
    s32 next;

    gRlMain.magic = RL_MAIN_MAGIC;
    gRlMain.visits++;
    if (gRlMain.returnState == 0) {
        gRlMain.returnState = STATE_MENU_SELECT;
    }
    if (rl_save_probe_poll()) {
        gCurrentGameState = STATE_MENU_SELECT;
        return;
    }

    next = FRAGMENT_LOAD_AND_CALL(fragmentRL, 0, 0);
    gRlMain.lastResult = next;

    if (gRlPendingBattle.pending) {
        rl_main_run_pending_battle();
        gCurrentGameState = STATE_ROGUELIKE;
        return;
    }
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
