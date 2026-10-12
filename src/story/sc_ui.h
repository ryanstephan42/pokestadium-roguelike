#ifndef SC_UI_H
#define SC_UI_H

#include "sc_combat.h"
#include "../roguelike/rl_ui_model.h"
#include "../roguelike/rl_battle.h"

enum ScUiScreen {
    SC_UI_MENU, SC_UI_CREATE, SC_UI_REGIONS, SC_UI_ENCOUNTERS,
    SC_UI_PARTY, SC_UI_MON, SC_UI_HEAL, SC_UI_VARIANT, SC_UI_CONFIRM,
    SC_UI_REWARD, SC_UI_REPLACE, SC_UI_DONOR, SC_UI_TARGET, SC_UI_MOVE,
    SC_UI_LEARN, SC_UI_EVOLVE, SC_UI_BATTLE, SC_UI_TERMINAL, SC_UI_SAVE_ERROR
};
enum ScUiOperation {
    SC_UI_NEW, SC_UI_START, SC_UI_ACTION, SC_UI_RECRUIT, SC_UI_TRANSFER,
    SC_UI_DECLINE, SC_UI_LEARN_MOVE, SC_UI_EVOLUTION, SC_UI_HEAL_SITE,
    SC_UI_END, SC_UI_GIFT, SC_UI_SAVE_EXIT, SC_UI_EVOLUTION_SKIP
};
enum ScUiResult { SC_UI_STAY, SC_UI_EXIT, SC_UI_REQUEST };

/* Value-only handoff. It is not a native battle entry point or loader token. */
typedef struct ScUiBattleRequest {
    RlMon party[6], enemy[6];
    ScCombat progression;
    RlU16 encounter;
    RlU8 variant, party_count, enemy_count;
    RlU32 operation_id;
} ScUiBattleRequest;

typedef struct ScUi {
    const ScCatalog *catalog;
    const ScSaveDevice *device;
    ScState state;
    ScSnapshot pending;
    ScBridge bridge;
    ScCombat postbattle_combat;
    ScKoLedger postbattle_ledger;
    ScOptions options;
    RlU32 seed, operation_id;
    RlUiList list;
    RlUiItem items[SC_ENCOUNTER_CAPACITY];
    RlU16 ids[SC_ENCOUNTER_CAPACITY];
    char labels[SC_ENCOUNTER_CAPACITY][72];
    char region[72];
    const char *message;
    unsigned screen, back, operation, node, variant, slot, donor, move, evolution;
    int status, storage_status, active, request_ready, pending_save, create_save;
    int postbattle_active;
    RlU8 postbattle_evolution_mask;
} ScUi;

int sc_ui_init(ScUi *ui, const ScCatalog *catalog, const ScSaveDevice *device, RlU32 seed);
int sc_ui_input(ScUi *ui, unsigned pressed);
int sc_ui_battle_request(const ScUi *ui, ScUiBattleRequest *request);
int sc_ui_observe(ScUi *ui, RlU32 operation_id, const ScBattleSnapshot party[6], unsigned active_slot,
                  unsigned enemy_slot, unsigned enemy_hp);
int sc_ui_finish(ScUi *ui, RlU32 operation_id, unsigned outcome);
int sc_ui_resume_postbattle(ScUi *ui, const ScUiBattleRequest *request,
                            const ScState *battle_state, const RlLaunchResult *result,
                            const ScKoLedger *ledger, unsigned outcome);
int sc_ui_unsupported(ScUi *ui);
const char *sc_ui_status_text(int status);
const char *sc_ui_save_text(int status);

#endif
