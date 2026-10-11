#ifndef SC_CORE_H
#define SC_CORE_H

#include "sc_data.h"

#define SC_BITS (SC_ENCOUNTER_CAPACITY / 8)

enum ScStatus {
    SC_OK, SC_INVALID, SC_UNAVAILABLE, SC_FULL, SC_DUPLICATE,
    SC_PENDING, SC_UNREVIEWED
};
enum ScPhase { SC_ROUTE, SC_BATTLE, SC_LEARNING, SC_REWARD, SC_LOST, SC_FINISHED };
enum ScDifficulty { SC_STANDARD, SC_LIMITED, SC_BRUTAL };
enum ScStarterPool { SC_STARTER_TRIO, SC_UNEVOLVED, SC_ALL_SPECIES };
enum ScOutcome { SC_LOSS, SC_WIN, SC_DRAW };

typedef struct ScOptions {
    RlU8 version, difficulty, starter_pool, starter;
} ScOptions;
typedef struct ScState {
    RlU32 seed, rng, milestones, healing_used;
    ScOptions options;
    RlU8 phase, party_count, rival_branch, defeated_mask, evolution_mask, encounter_variant;
    RlU8 learn_mask, learn_moves[6], return_phase, offered_slot;
    RlU8 fossil_choice;
    RlU16 encounter;
    RlU8 completed[SC_BITS], retired[SC_BITS];
    RlMon party[6], offer;
    RlU8 moon_stones, moon_stones_spent;
} ScState;

int sc_catalog_valid(const ScCatalog *catalog);
int sc_new(ScState *state, const ScCatalog *catalog, const ScOptions *options,
           RlU32 seed);
int sc_available(const ScState *state, const ScCatalog *catalog, unsigned encounter);
int sc_action(ScState *state, const ScCatalog *catalog, unsigned node,
              unsigned choice, unsigned destination_warp);
int sc_location_heal(ScState *state, const ScCatalog *catalog, unsigned location);
int sc_team(const ScState *state, const ScCatalog *catalog, unsigned encounter,
            unsigned variant, RlMon output[6], unsigned *count);
int sc_begin(ScState *state, const ScCatalog *catalog, unsigned encounter,
             unsigned variant);
int sc_on_ko(ScState *state, const ScCatalog *catalog, unsigned enemy_slot,
             RlU8 participants, unsigned variant);
int sc_finish(ScState *state, const ScCatalog *catalog, unsigned outcome,
              unsigned variant);
int sc_recruit(ScState *state, const ScCatalog *catalog, unsigned outgoing_slot, int confirmed);
int sc_transfer(ScState *state, const ScCatalog *catalog, unsigned donor_move,
                unsigned party_slot, unsigned move_slot, int confirmed);
int sc_decline(ScState *state, const ScCatalog *catalog);
int sc_heal(ScState *state, const ScCatalog *catalog, unsigned location,
            const ScGate *gate);
int sc_grant(ScState *state, const ScCatalog *catalog, RlU32 milestones);
int sc_learn(ScState *state, const ScCatalog *catalog, unsigned party_slot,
             unsigned move_slot, int accept);
int sc_evolve(ScState *state, const ScCatalog *catalog, unsigned slot,
              unsigned evolution, unsigned stone_mask, int trade_unlocked,
              int confirmed);
int sc_pickup_stone(ScState *state, const ScCatalog *catalog, unsigned pickup);
int sc_evolve_owned(ScState *state, const ScCatalog *catalog, unsigned slot,
                    unsigned evolution, int confirmed);

RlU32 sc_random(RlU32 *state);
RlU32 sc_random_bounded(RlU32 *state, RlU32 bound);
RlU32 sc_xp(unsigned level, unsigned growth);
unsigned sc_stat(const RlMon *mon, unsigned stat);
void sc_recalculate(RlMon *mon, int preserve_hp);
int sc_make_mon(const ScCatalog *catalog, const ScTemplate *input,
                 RlU16 random_dvs, RlMon *output);
int sc_add_xp(RlMon *mon, RlU32 award, RlU8 *learn_move,
              const ScCatalog *catalog);

#endif
