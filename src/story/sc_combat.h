#ifndef SC_COMBAT_H
#define SC_COMBAT_H

#include "sc_bridge.h"

typedef struct ScCombat {
    ScSpecies party_species[6], enemy_species[6];
    RlU8 party_ids[6];
    RlU8 pp[166];
    ScTeam team;
    RlU32 operation_id;
    RlU16 encounter;
    RlU8 variant, kind, ready;
} ScCombat;

#define SC_KO_LEDGER_CAPACITY 6
typedef struct ScKoEvent {
    RlU8 enemy_slot, participants, recipients;
} ScKoEvent;
typedef struct ScKoLedger {
    /* Appended in KO order; recipients are participants alive at that KO. */
    ScKoEvent events[SC_KO_LEDGER_CAPACITY];
    RlU32 operation_id;
    RlU16 encounter;
    RlU8 variant, party_ids[6], progression_ids[6];
    RlU8 count, next, enemy_count, party_count, bound;
} ScKoLedger;

void sc_ko_ledger_init(ScKoLedger *ledger);
void sc_ko_ledger_discard(ScKoLedger *ledger);
int sc_combat_prepare(ScCombat *combat, const ScState *state, const ScCatalog *catalog);
int sc_ko_ledger_bind(ScKoLedger *ledger, const ScCombat *combat, RlU32 operation_id);
int sc_ko_ledger_follow_evolution(ScKoLedger *ledger, const ScCombat *combat,
                                  const ScState *state, unsigned party_slot,
                                  unsigned prior_species);
int sc_ko_values(ScState *state, const ScSpecies party_species[6],
                 const ScSpecies enemy_species[6], const ScTeam *team,
                 unsigned kind, unsigned enemy_slot, RlU8 participants);
int sc_ko_ledger_record(ScKoLedger *ledger, unsigned enemy_slot,
                        unsigned enemy_count, unsigned party_count,
                        RlU8 participants, RlU8 recipients);
/* Captures active participation and KO-time eligibility without changing the
   campaign state or party progression. Repeated observations are idempotent. */
int sc_combat_capture_ko(ScBridge *bridge, ScKoLedger *ledger,
                         const ScCombat *combat, const ScBattleSnapshot party[6],
                         unsigned active_slot, unsigned enemy_slot, unsigned enemy_hp);
/* Applies exactly one captured KO. SC_PENDING means more KOs remain or a
   learning choice must be resolved before the next call. */
int sc_ko_ledger_apply_next(ScKoLedger *ledger, ScState *state,
                            const ScCombat *combat);
int sc_add_xp_species(RlMon *mon, RlU32 award, RlU8 *learn_move,
                      const ScSpecies *species);
int sc_add_xp_species_deferred(RlMon *mon, RlU32 award, RlU8 *learn_move,
                               const ScSpecies *species);
int sc_learn_pp(ScState *state, const RlU8 pp[166], unsigned party_slot,
                unsigned move_slot, int accept);
int sc_combat_observe(ScState *state, ScBridge *bridge, const ScCombat *combat,
                      const ScBattleSnapshot party[6], unsigned active_slot,
                      unsigned enemy_slot, unsigned enemy_hp);
int sc_observe_values(ScState *state, ScBridge *bridge, const ScSpecies party_species[6],
                      const ScSpecies enemy_species[6], const ScTeam *team, unsigned kind,
                      const ScBattleSnapshot party[6], unsigned active_slot,
                      unsigned enemy_slot, unsigned enemy_hp);

#endif
