#ifndef _ROGUELIKE_RL_MAIN_H_
#define _ROGUELIKE_RL_MAIN_H_

/*
 * W0 build/boot glue for the roguelike mode. Resident (main segment), compiled
 * only for `make rl`. Gameplay/run state lives in rl_core.{c,h} (W5); this file
 * only owns the state-machine hooks and the overlay loader.
 */

#include "ultra64.h"
#include "rl_battle.h"
#include "rl_save_format.h"

#define RL_FRAGMENT_VRAM 0x8F800000

/* Placeholder boot bookkeeping. Real run state: see rl_core.h (W5). */
typedef struct RlMainState {
    /* 0x00 */ u32 magic;       /* RL_MAIN_MAGIC once rl_main_state_roguelike ran */
    /* 0x04 */ u32 visits;      /* times the RL overlay was entered */
    /* 0x08 */ s32 lastResult;  /* last value returned by the overlay */
    /* 0x0C */ s32 returnState; /* game state to return to when leaving RL mode */
    /* 0x10 */ u32 frames;      /* frames drawn by the overlay (all visits) */
} RlMainState; /* size = 0x14 */

typedef struct RlPendingBattle {
    RlMon player[3];
    RlMon enemy[RL_BATTLE_MAX_MONS];
    RlMon trainer[RL_BATTLE_MAX_MONS];
    RlBattleRules rules;
    unsigned char player_slots[3];
    unsigned char player_count;
    unsigned char enemy_count;
    unsigned char trainer_count;
    unsigned char pending;
    unsigned char post_battle;
    unsigned char error;
    unsigned char outcome;
} RlPendingBattle;

#define RL_MAIN_MAGIC 0x524C4D30 /* 'RLM0' */

extern RlMainState gRlMain;
extern RunState gRlRun;
typedef struct RlPlayProfile {
    RlMeta meta, next_meta;
    RlU8 summary_prepared, summary_saved, ascension, starter_pool, unsaved;
} RlPlayProfile;
extern RlPlayProfile gRlPlayProfile;
extern RlPendingBattle gRlPendingBattle;
extern volatile u32 gRlPlayScene;
extern volatile u32 gRlPlayExit;
extern volatile u32 gRlPlayBattleCount;
extern volatile u32 gRlPlayTestMode;
extern volatile u32 gRlPlayTestSeed;
extern volatile u8 gRlPlayDraftSpecies[6];

int rl_main_queue_battle(const RlMon* player, unsigned player_count,
                         const RlMon* enemy, unsigned enemy_count,
                         const RlBattleRules* rules,
                         const unsigned char* player_slots,
                         const RlMon* trainer, unsigned trainer_count);

/* Called from Game_Thread for STATE_ROGUELIKE. Loads and runs fragmentRL. */
void rl_main_state_roguelike(void);
/* Called from Game_Thread for STATE_ROGUELIKE_BATTLE_TEST (0x31). */
void rl_main_state_battle_test(void);
/* Menu hook: returns STATE_ROGUELIKE if the shortcut is held, else `state`. */
s32 rl_main_menu_hook(s32 state);

#endif
