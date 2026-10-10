#ifndef RL_BATTLE_H
#define RL_BATTLE_H

#include "src/fragments/62/fragment62.h"

/* Views into fragment 62's pool; never retain them after BATL pool release. */
typedef unk_D_84390010 RlBattleSide;
typedef unk_D_800FCB18 RlBattleMon;
typedef unk_func_80026268_arg0 RlBattlePartyMon;

typedef enum RlBattleResult {
    RL_BATTLE_ONGOING = 0,
    RL_BATTLE_SIDE_0_WON = 1,
    RL_BATTLE_SIDE_1_WON = 2,
    RL_BATTLE_DRAW = 3
} RlBattleResult;

typedef enum RlBattleStage {
    RL_BATTLE_STAGE_ATTACK = 0,
    RL_BATTLE_STAGE_DEFENSE = 1,
    RL_BATTLE_STAGE_SPEED = 2,
    RL_BATTLE_STAGE_SPECIAL = 3,
    RL_BATTLE_STAGE_ACCURACY = 4,
    RL_BATTLE_STAGE_EVASION = 5
} RlBattleStage;

#define RL_BATTLE_STAGE_NEUTRAL 7
#define RL_BATTLE_PP_MASK 0x3F
#define RL_BATTLE_MON_OFFSET 0x68C
#define RL_BATTLE_MON_HP_OFFSET 0x0C
#define RL_BATTLE_MON_PP_OFFSET 0x32

/* Lvalue accessors preserve native types and evaluate each argument once.
 * The caller supplies a loaded side/mon and valid party/move/stage indices.
 */
#define rl_battle_mon(side) (&(side)->unk_654.unk_38)
#define rl_battle_active_index(side) ((side)->unk_654.unk_08)
#define rl_battle_incoming_index(side) ((side)->unk_654.unk_18)
#define rl_battle_trainer_index(side) ((side)->unk_654.unk_2B)
#define rl_battle_incoming_trainer_index(side) ((side)->unk_654.unk_2C)
#define rl_battle_move_slot(side) ((side)->unk_654.unk_0C)
#define rl_battle_party_mon(side, index) (&(side)->unk_724->unk_01C[index])

#define rl_battle_species(mon) ((mon)->unk_0B)
#define rl_battle_level(mon) ((mon)->unk_26)
#define rl_battle_hp(mon) ((mon)->unk_0C)
#define rl_battle_max_hp(mon) ((mon)->unk_28)
#define rl_battle_attack(mon) ((mon)->unk_2A)
#define rl_battle_defense(mon) ((mon)->unk_2C)
#define rl_battle_speed(mon) ((mon)->unk_2E)
#define rl_battle_special(mon) ((mon)->unk_30)
#define rl_battle_base_attack(mon) ((mon)->unk_38)
#define rl_battle_base_defense(mon) ((mon)->unk_3A)
#define rl_battle_base_speed(mon) ((mon)->unk_3C)
#define rl_battle_base_special(mon) ((mon)->unk_3E)
#define rl_battle_move(mon, slot) ((mon)->unk_1F[slot])
#define rl_battle_packed_pp(mon, slot) ((mon)->unk_32[slot])
#define rl_battle_pp(mon, slot) (rl_battle_packed_pp(mon, slot) & RL_BATTLE_PP_MASK)
#define rl_battle_status(mon) ((mon)->unk_15)
#define rl_battle_stage(mon, stage) ((mon)->unk_5C[stage])
#define rl_battle_fainted(mon) (rl_battle_hp(mon) == 0)

/* Original party PP is authoritative for transformed mons. Party HP animates. */
#define rl_battle_party_hp(mon) ((mon)->unk_02)
#define rl_battle_party_status(mon) ((mon)->unk_05)
#define rl_battle_party_move(mon, slot) ((mon)->unk_09[slot])
#define rl_battle_party_packed_pp(mon, slot) ((mon)->unk_20[slot])

#endif
