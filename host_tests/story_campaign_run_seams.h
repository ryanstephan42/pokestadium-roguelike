#ifndef STORY_CAMPAIGN_RUN_SEAMS_H
#define STORY_CAMPAIGN_RUN_SEAMS_H
/* SUBSTITUTED NATIVE SEAMS for the connected campaign-runner HOST test. Not a
   target ABI fixture: these replace fragment-62 battle records (include/
   rl_battle.h), the RL launcher declarations, controller/Game_Thread state,
   the native probe mapping/capture/publish helpers and the service arena
   accessor with field-name-compatible host views. The actual target layouts
   are exercised by the IDO compile of sc_campaign_run.c and by the
   native-probe/six-runtime runners. */
#include "story_campaign_stubs.h"
#include <stdint.h>
#include "src/roguelike/rl_types.h"
#include "src/story/sc_bridge.h"
#include "src/story/sc_module_arena.h"
#define RL_BATTLE_H
#define RL_BATTLE_LAUNCH_H
#define _CONTROLLER_H_
#define _29BA0_H_
#define SC_RUNTIME_PROBE_H
#define SC_BATTLE_SERVICE_H
#define SC_NATIVE_PROBE_H
#define _1CF30_H_

typedef struct unk_func_80026268_arg0 {
    struct { u8 unk_00; } unk_00;
    u16 unk_02;
    u8 unk_05;
} unk_func_80026268_arg0;
typedef unk_func_80026268_arg0 RlBattlePartyMon;
typedef struct RlBattleMon { u16 unk_0C; u8 unk_15; } RlBattleMon;
typedef struct RlBattleTeam { u8 unk_002; RlBattlePartyMon unk_01C[6]; } RlBattleTeam;
typedef struct RlBattleSide {
    struct { u8 unk_08; RlBattleMon unk_38; } unk_654;
    RlBattleTeam *unk_724;
} RlBattleSide;
#define rl_battle_mon(side) (&(side)->unk_654.unk_38)
#define rl_battle_active_index(side) ((side)->unk_654.unk_08)
#define rl_battle_party_mon(side, index) (&(side)->unk_724->unk_01C[index])
#define rl_battle_hp(mon) ((mon)->unk_0C)
#define rl_battle_status(mon) ((mon)->unk_15)
#define rl_battle_party_hp(mon) ((mon)->unk_02)
#define rl_battle_party_status(mon) ((mon)->unk_05)

typedef struct Controller { u16 buttonDown, buttonPressed; } Controller;
extern Controller *gPlayer1Controller;
#define BTN_IS_DOWN(controller, combo) (((controller)->buttonDown & (combo)) != 0)
#define L_TRIG 0x0020
#define R_TRIG 0x0010
enum { STATE_TITLE_SCREEN = 0x02, STATE_MENU_SELECT = 0x04, STATE_STORY_CAMPAIGN = 0x32 };
extern s32 gCurrentGameState;
u32 osGetCount(void);
extern u32 osMemSize;
/* fragment57/resident text printer (src/1CF30.h) */
void func_8001F3F4(void);
void func_8001EBE0(s32 font, s32 arg1);
void func_8001F324(s32 r, s32 g, s32 b, s32 a);
void func_8001F1E8(s32 x, s32 y, const char *fmt, ...);
void func_8001F444(void);
void osSyncPrintf(const char *, ...);

int sc_native_mapping(const ScState *state, unsigned count, const RlU8 indices[6],
                      RlU8 output[6]);
int sc_native_capture(const ScState *state, const unk_func_80026268_arg0 *party,
                      unsigned count, const RlU8 mapping[6], unsigned active,
                      ScBattleSnapshot snapshots[6]);
int sc_story_publish_live(const ScState *state, const RlU8 map[6], RlBattleSide *side);
ScMaArena *sc_service_native_arena(void);
#endif
