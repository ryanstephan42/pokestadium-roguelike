#ifndef SC_DATA_H
#define SC_DATA_H

#include "../roguelike/rl_core.h"

#define SC_SPECIES_COUNT 151
#define SC_LEARN_CAPACITY 8
#define SC_EVOLUTION_CAPACITY 3
#define SC_ENCOUNTER_CAPACITY 512
#define SC_NO_ENCOUNTER 65535

enum ScKind { SC_TRAINER, SC_RIVAL, SC_STATIC, SC_GIFT, SC_GHOST };
enum ScEvolutionKind { SC_LEVEL_EVOLUTION = 1, SC_STONE_EVOLUTION, SC_TRADE_EVOLUTION };
enum ScStone { SC_MOON_STONE = 1, SC_FIRE_STONE, SC_THUNDER_STONE,
               SC_WATER_STONE, SC_LEAF_STONE };

typedef struct ScLearn { RlU8 level, move; } ScLearn;
typedef struct ScEvolution { RlU8 method, level, species, stone; } ScEvolution;
typedef struct ScSpecies {
    RlU8 base[5], yield, growth, initial[4], learn_count, evolution_count;
    ScLearn learn[SC_LEARN_CAPACITY];
    ScEvolution evolution[SC_EVOLUTION_CAPACITY];
} ScSpecies;

typedef struct ScTemplate {
    RlU8 species, level, moves[4], fixed_dvs;
    RlU16 dvs;
} ScTemplate;
typedef struct ScTeam { RlU8 count; ScTemplate mons[6]; } ScTeam;

/* Gates are reviewed campaign policy, not inferred from source file order. */
typedef struct ScGate {
    RlU32 required, any, forbidden;
    RlU16 prior[6];
    RlU8 prior_count;
} ScGate;
typedef struct ScEncounter {
    const char *id;
    RlU8 kind, variants;
    ScTeam teams[3];
    ScGate gate;
    RlU32 grants, retires_at;
} ScEncounter;

typedef struct ScClause {
    RlU16 prior[12];
    RlU8 count;
} ScClause;
typedef struct ScNode {
    const char *id;
    ScClause clauses[2];
    RlU16 retires_after[2];
    RlU8 clause_count, retirement_count, variants, destination_warp;
} ScNode;
typedef struct ScHealing {
    const char *id;
    ScClause required;
    RlU16 retires_after;
} ScHealing;
typedef struct ScStonePickup {
    RlU16 node;
    RlU8 stone;
} ScStonePickup;
typedef struct ScGraph {
    const ScNode *nodes;
    const ScHealing *healing;
    const char *review_identity;
    RlU16 count, fossil_action, fossil_offer, champion, league[4];
    RlU8 healing_count;
    const ScStonePickup *pickups;
    RlU8 pickup_count;
    const char *legacy_review_identity;
} ScGraph;

typedef struct ScCatalog {
    const ScSpecies *species; /* Dummy zero entry, followed by national dex IDs. */
    const RlU8 *pp;           /* Dummy zero entry and 165 canonical moves. */
    const ScEncounter *encounters;
    RlU16 count;
    RlU8 graph_reviewed;
    const ScGraph *graph;
} ScCatalog;

extern const ScSpecies sc_species[SC_SPECIES_COUNT + 1];
extern const RlU8 sc_pp[166];
extern const ScEncounter sc_encounters[];
extern const ScCatalog sc_catalog;

#endif
