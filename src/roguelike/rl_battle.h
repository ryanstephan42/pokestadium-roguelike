#ifndef RL_BATTLE_LAUNCH_H
#define RL_BATTLE_LAUNCH_H

/*
 * Roguelike battle-launch API (workstream W3).
 *
 * rl_start_battle() converts RlMon teams to the native 0x54-byte battle mon
 * record, writes them into the global battle context D_800AE540 and runs the
 * battle engine (fragment 62) directly (like STATE_FAST_BATTLE). Afterwards the
 * outcome and post-battle HP/PP/status are written back to RlMon form.
 *
 * No libultra dependency. See docs/roguelike/re-notes/battle-entry.md.
 */

#include "rl_core.h"

/* Debug/test game state that launches a fixed custom battle (W0 owns 0x30). */
#define STATE_RL_BATTLE_TEST 0x31

#define RL_BATTLE_MAX_MONS 6

typedef struct RlBattleRules {
    /*
     * Rule index -> D_800AE540.unk_0001. With battle mode 0xA (event battle)
     * it also selects the arena via fragment62 D_84384350: 0..3 Poke Cup style,
     * 4 Petit, 5 Pika, 6 Prime, 7 Gym Leader Castle (+gym), 8 Mewtwo.
     * Custom teams bypass native repicking; this field selects presentation,
     * not eligibility clauses or level caps. The caller enforces those rules.
     */
    unsigned char stadium;
    unsigned char gym;          /* only for stadium 7: 0..7 gyms, 8 E4, 9 rival */
    unsigned char ai_level;     /* CPU trainer AI byte (native trainer id low byte) */
    unsigned char player_count; /* first N usable party mons are sent in (0 = all) */
    unsigned char player_cpu;   /* 1 = player side also AI controlled (auto battle) */
    unsigned char show_intro;   /* 1 = run fragment63 VS intro first */
    unsigned char use_state;    /* 1 = use RlMon cur_hp/pp/status, 0 = full heal */
    unsigned char pad;
    RlU32 relics;               /* run-wide player relic mask; zero for ordinary tests */
} RlBattleRules;

enum {
    RL_BATTLE_LOSS = 0,
    RL_BATTLE_WIN = 1,
    RL_LAUNCH_DRAW = 2,
};

typedef struct RlLaunchResult {
    unsigned char outcome; /* RL_BATTLE_* */
    unsigned char player_count;
    unsigned char enemy_count;
    unsigned char player_index[RL_BATTLE_MAX_MONS]; /* party index of player[i] */
    RlMon player[RL_BATTLE_MAX_MONS]; /* mons that took part, post-battle */
    RlMon enemy[RL_BATTLE_MAX_MONS];  /* CPU's picked mons, post-battle */
} RlLaunchResult;

/* Result of the last rl_start_battle(); read by the test harness via the .map. */
extern RlLaunchResult gRlLastBattleResult;
extern volatile unsigned int gRlBattleCount;
extern int gRlBattleActive;
extern volatile int gRlBattleTestRequest;
extern RlMon gRlRelicTestParty[6], gRlRelicTestEnemy[6];
extern RlBattleRules gRlRelicTestRules;
extern unsigned int gRlRelicTestPartyCount, gRlRelicTestEnemyCount, gRlRelicTestMask;

/* Rebuild native stats/PP limits/base stats for generated/scaled mons.
 * full_heal=0 preserves HP/PP/status; full_heal=1 restores them. */
int rl_prepare_mon(RlMon* mon, int full_heal);
/* Acquisition-time PP Pouch replacement: one native PP Up per current move,
 * once per run, without restoring PP. Uses the pouch consumed-relic bit. */
int rl_battle_apply_pp_pouch(RunState* run);

/*
 * Launches a battle, blocks until it ends, sets gCurrentGameState to
 * return_state and returns RL_BATTLE_* (-1 on invalid input, logged without
 * changing the context/state). Copy out->player[] using out->player_index[]
 * to update the caller's persistent party; the input arrays are unchanged.
 */
int rl_start_battle(const RlMon* party, int party_n, const RlMon* enemy, int enemy_n,
                    const RlBattleRules* rules, int return_state, RlLaunchResult* out);

/* STATE_RL_BATTLE_TEST handler: fixed custom party vs custom enemy. */
void rl_battle_test_entry(void);

#endif
