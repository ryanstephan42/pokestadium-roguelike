#ifndef SC_NATIVE_CAMPAIGN_H
#define SC_NATIVE_CAMPAIGN_H
#if defined(STORY_SERVICE_CANDIDATE) && defined(STORY_NATIVE_UI)
#include "sc_native_terminal.h"
#include "sc_persist.h"

/* Persistent-image entry selector passed as the fragment entry's first
   argument; cold_invoke always passes 0. */
#define SC_CAMPAIGN_ENTRY 0x53435045
enum ScCampaignCommand {
    SC_CAMPAIGN_OPEN = 1, SC_CAMPAIGN_STEP, SC_CAMPAIGN_CLOSE, SC_CAMPAIGN_OBSERVE
};

/* Resident control (never inside the mapped image or the DATA lease): loop,
   display and retirement controls are still read after the lease is freed. */
typedef struct ScCampaignControl {
    ScNativeLifetime lifetime;
    ScNativeOperation operation;
    ScNativeTerminal terminal;
    ScNativeTerminalBinding binding;
    ScUiBattleRequest sequential_request;
    ScState sequential_state;
    ScBridge sequential_bridge;
    ScKoLedger sequential_ledger;
    RlLaunchResult sequential_result;
    ScMaArena *arena;
    RlU32 seed;
    unsigned installed;
    unsigned sequential_active, sequential_result_ready, sequential_outcome;
    /* Resident observation staging for SC_CAMPAIGN_OBSERVE (story slot order). */
    ScBattleSnapshot observed[6];
    unsigned observed_active, observed_enemy, observed_hp;
} ScCampaignControl;

/* SC image (sc_native_campaign.c): production owner adapter, entered only via
   sc_persist_call*(SC_CAMPAIGN_ENTRY, command). OPEN opens the presentation
   loop with lifetime/operation/terminal callbacks and binds the operation;
   STEP runs one loop frame; CLOSE drives loop close (PENDING retains all);
   OBSERVE submits the resident staged battle observation to the operation. */
int sc_campaign_dispatch(unsigned command);

/* Resident installer (sc_campaign.c). Campaign phases: */
enum {
    SC_CAMPAIGN_IDLE, SC_CAMPAIGN_ACTIVE, SC_CAMPAIGN_CLOSING,
    SC_CAMPAIGN_RELEASING, SC_CAMPAIGN_FAILED
};
/* Maps SC, pins LIFETIME, then opens the loop. A refused open enters CLOSING;
   the caller then drives sc_campaign_close. */
int sc_campaign_open(ScMaArena *arena, RlU32 seed);
int sc_campaign_step(void);
/* Stages one in-battle observation and submits it through the published
   entry; the next STEP consumes the resulting OBSERVE/LEARNING event. */
int sc_campaign_observe(const ScBattleSnapshot party[6], unsigned active, unsigned enemy,
                        unsigned hp);
/* Launcher-detected unrecoverable refusal (no armed battle for a RUNNING
   operation): fail-stops and retains mapping, pins and backing. */
int sc_campaign_fail_stop(void);
/* While a battle or UI is still live returns SC_PENDING without changing
   anything. Otherwise unpublishes first (no new entries, pins or cold calls),
   drives loop/display/lifetime retirement through the LIFETIME-pinned entry
   while SC stays mapped, unpins, then releases the mapping. SC_PENDING: retry
   later. Any refusal fail-stops and retains mapping and backing. */
int sc_campaign_close(void);
/* Campaign predicate: true only while ACTIVE. */
int sc_campaign_active(void);
unsigned sc_campaign_phase(void);
const ScCampaignControl *sc_campaign_control(void);
/* Mutable control for the SC adapter; NULL unless an entry is in progress. */
ScCampaignControl *sc_campaign_bound(void);
#endif
#endif
