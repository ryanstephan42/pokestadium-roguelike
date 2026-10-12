#ifndef SC_CAMPAIGN_BATTLE_H
#define SC_CAMPAIGN_BATTLE_H
#ifdef STORY_NATIVE_PROBE
extern int gScProbeActive;
#if defined(STORY_SERVICE_CANDIDATE) && defined(STORY_NATIVE_UI)
#include "include/rl_battle.h"
#include "src/roguelike/rl_types.h"
#define SC_CAMPAIGN_BATTLE_WIRED 1
/* Set by the resident campaign launcher only around rl_start_battle for the
   admitted RUNNING operation; never by ordinary RL or the probe. */
extern int gScCampaignBattle;
/* Story service/BATL predicate for the native battle fragment. */
#define SC_STORY_BATTLE (gScProbeActive || gScCampaignBattle)
/* Player-side native->story slot mapping for the active story battle. */
const RlU8 *sc_story_battle_mapping(void);
/* Launcher records the native->story party mapping before arming. */
int sc_campaign_battle_mapping(const RlU8 indices[6], unsigned count);
/* Fragment-62 frame gate, same contract as sc_runtime_probe_gate/after/paused:
   gate 0 advance, 1 render only, 2 resume the deferred post-logic frame. */
int sc_campaign_battle_gate(RlBattleSide *player, RlBattleSide *enemy, int *result);
int sc_campaign_battle_after(RlBattleSide *player, RlBattleSide *enemy, int result);
int sc_campaign_battle_paused(void);
/* Fragment-62 refusal of an unbound native effect dispatch: pauses the
   battle the same way as an invalid observation (render only). */
void sc_campaign_battle_fault(void);
/* Handler for STATE_STORY_CAMPAIGN and the Menu Select hook. The poll returns 1
   when a battle launch is ready; Game_Thread then calls the launch from its own
   frame (a shallower battle stack). sc_campaign_state composes the two. */
int sc_campaign_state_poll(void);
void sc_campaign_state_launch(void);
void sc_campaign_state(void);
int sc_campaign_menu_hook(int state);
/* Fragment57 top-page story entry: R request (1 = accepted) and hint text. */
int sc_campaign_menu_request(void);
void sc_campaign_menu_hint(void);

/* Resident run diagnostics (never authority). */
typedef struct ScCampaignRun {
    unsigned stage, error, battles, observations, publishes, pauses, refusals;
    RlU32 launched;
    int last_status, last_outcome;
} ScCampaignRun;
extern ScCampaignRun gScCampaignRun;
enum {
    SC_RUN_ENTRY, SC_RUN_ACTIVE, SC_RUN_CLOSING, SC_RUN_BATTLE_READY,
    SC_RUN_BATTLE_ACTIVE, SC_RUN_REOPEN, SC_RUN_FAILED
};
#else
#define SC_STORY_BATTLE gScProbeActive
#endif
#endif
#endif
