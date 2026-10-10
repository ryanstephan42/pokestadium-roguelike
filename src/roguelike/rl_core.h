#ifndef RL_CORE_H
#define RL_CORE_H

#include "rl_types.h"

#define RL_ROWS 7
#define RL_LANES 4
#define RL_PARTY 6
#define RL_RELIC_COUNT 20
#define RL_ITEM_COUNT 13
#define RL_EVENT_COUNT 12
#define RL_NO_SLOT 255
#define RL_TM_CAPACITY 16

enum RlResult { RL_OK, RL_INVALID, RL_FULL, RL_EMPTY, RL_FUNDS, RL_CORRUPT };
enum RlNodeType { RL_BATTLE, RL_ELITE, RL_CENTER, RL_MART,
                  RL_EVENT, RL_RECRUIT, RL_BOSS };
enum RlRunPhase { RL_DRAFT, RL_MAP, RL_NODE, RL_REWARD, RL_WON, RL_LOST };
enum RlGrowth { RL_MEDIUM_FAST, RL_FAST, RL_SLOW, RL_MEDIUM_SLOW };
enum RlItem { RL_POTION, RL_SUPER_POTION, RL_HYPER_POTION, RL_FULL_RESTORE,
              RL_REVIVE, RL_ETHER, RL_ELIXIR, RL_FULL_HEAL, RL_CANDY,
              RL_VITAMIN, RL_RELIC_ITEM, RL_TM, RL_MONEY };
enum RlStat { RL_HP, RL_ATTACK, RL_DEFENSE, RL_SPEED, RL_SPECIAL };
enum RlType { RL_NORMAL, RL_FIRE, RL_WATER, RL_ELECTRIC, RL_GRASS, RL_ICE,
              RL_FIGHTING, RL_POISON, RL_GROUND, RL_FLYING, RL_PSYCHIC,
              RL_BUG, RL_ROCK, RL_GHOST_TYPE, RL_DRAGON };
enum RlRewardKind { RL_REWARD_RECRUIT, RL_REWARD_CANDY, RL_REWARD_VITAMIN,
                    RL_REWARD_RELIC, RL_REWARD_MONEY, RL_REWARD_TM };
enum RlRelic { RL_BOULDER, RL_CASCADE, RL_THUNDER, RL_RAINBOW, RL_SOUL,
               RL_MARSH, RL_VOLCANO, RL_EARTH, RL_AMULET, RL_EXP_SHARE,
               RL_LUCKY_EGG, RL_LEFTOVERS, RL_FOCUS, RL_SCOPE_LENS,
               RL_SILPH_SCOPE, RL_PP_POUCH, RL_OLD_AMBER, RL_MASTER_BALL,
               RL_GHOST, RL_CURSED_POWER };

/*
 * Compact, platform-neutral battle record. base_stats/stat_exp indices are
 * HP, Attack, Defense, Speed, Special, deliberately independent of STAT_*
 * names in 22630.h. DVs use Gen 1 packed nibbles; PP fields are unpacked.
 * nickname is ASCII and NUL-terminated. Integrators translate native strings,
 * PP-Up packing, and concrete 22630.h records at the boundary.
 */
typedef struct RlMon {
    RlU8 species, level, growth, fainted;
    RlU32 exp;
    RlU16 dvs, stat_exp[5];
    RlU8 base_stats[5];
    RlU16 current_stats[5];
    RlU8 moves[4], pp[4], max_pp[4];
    RlU16 cur_hp, max_hp;
    RlU8 status, held_relic;
    char nickname[11];
} RlMon;

typedef struct RlNode { RlU8 type, edges, target_species; } RlNode;
typedef struct RlMap {
    RlU8 lanes[RL_ROWS];
    RlNode nodes[RL_ROWS][RL_LANES];
} RlMap;
typedef struct RlStats { RlU32 battles, wins, nodes, recruits; } RlStats;
typedef struct RunState {
    RlU32 seed, rng;
    RlU8 act, row, lane, phase, ascension, party_count;
    RlMap map;
    RlMon party[RL_PARTY];
    RlU16 bag[RL_ITEM_COUNT];
    RlU32 money, relics, consumed_relics;
    RlU8 tm_count, tm_moves[RL_TM_CAPACITY];
    RlStats stats;
    RlMon daycare;
    RlU8 daycare_nodes, daycare_active;
} RunState;
typedef struct RlPool {
    const RlMon *mons;
    RlU16 count;
    RlU8 fixed_order;
} RlPool;
typedef struct RlMovePool { const RlU8 *moves; RlU8 count; } RlMovePool;
typedef int (*RlCanLearnMove)(const RlMon *mon, RlU8 move, RlU8 *max_pp,
                              void *context);
typedef struct RlReward { RlU8 kind, detail; RlU16 amount; RlMon mon; } RlReward;
typedef struct RlShopEntry { RlU8 item, detail; RlU16 price; } RlShopEntry;
typedef struct RlEventDef { const char *name; const char *choices[2]; } RlEventDef;
typedef struct RlRelicDef { const char *name; const char *effect; } RlRelicDef;
typedef struct RlAscension {
    RlU8 level_bonus, fewer_centers, permadeath, full_team;
    RlU8 money_percent, enemy_dv_bonus, heal_percent;
} RlAscension;
typedef struct RlBattleHooks { RlU8 focus_used; } RlBattleHooks;

extern const RlPool rl_placeholder_pool;
extern const RlEventDef rl_events[RL_EVENT_COUNT];
extern const RlRelicDef rl_relic_defs[RL_RELIC_COUNT];

RlU32 rl_random(RlU32 *state);
RlU32 rl_random_bounded(RlU32 *state, RlU32 limit);
void rl_run_new(RunState *run, RlU32 seed, unsigned ascension);
int rl_run_advance(RunState *run, unsigned lane);
int rl_run_finish_node(RunState *run);
int rl_run_on_battle_result(RunState *run, int won, RlU8 active_mask);
int rl_run_is_over(const RunState *run);
void rl_map_generate(RlMap *map, RlU32 *rng, unsigned act, unsigned ascension);
int rl_map_valid(const RlMap *map);
unsigned rl_map_rows(unsigned act);
RlAscension rl_ascension(unsigned level);
unsigned rl_enemy_level(unsigned act, unsigned row, unsigned type, unsigned ascension);
int rl_enemy_generate(RunState *run, const RlPool *pool, unsigned type,
                      RlMon *team, unsigned count);
RlU32 rl_xp_for_level(unsigned level, unsigned growth);
int rl_mon_add_xp(RlMon *mon, RlU32 xp);
unsigned rl_mon_stat_value(const RlMon *mon, unsigned stat, unsigned level);
void rl_mon_recalculate_stats(RlMon *mon, int preserve_hp);
void rl_mon_heal(RlMon *mon, unsigned percent);
void rl_mon_restore_hp(RlMon *mon, unsigned amount);
int rl_party_recruit(RunState *run, const RlMon *mon);
int rl_party_release(RunState *run, unsigned slot);
int rl_party_swap(RunState *run, unsigned a, unsigned b);
int rl_party_revive(RunState *run, unsigned slot);
int rl_item_use(RunState *run, unsigned item, unsigned slot, unsigned move);
unsigned rl_rewards_generate(RunState *run, const RlPool *pool,
                             const RlMovePool *moves,
                             RlReward *out, unsigned capacity);
int rl_reward_apply(RunState *run, const RlReward *reward);
unsigned rl_shop_generate(RunState *run, const RlMovePool *moves,
                          RlShopEntry *out, unsigned capacity);
int rl_shop_buy(RunState *run, const RlShopEntry *entry);
int rl_tm_teach(RunState *run, unsigned tm_slot, unsigned party_slot,
                unsigned move_slot, RlCanLearnMove can_learn, void *context);
unsigned rl_event_wild_options(const RunState *run, const RlPool *pool,
                               RlMon *out, unsigned capacity);
int rl_event_resolve(RunState *run, unsigned event, unsigned choice,
                     unsigned slot, const RlPool *pool,
                     const RlMovePool *moves);
unsigned rl_relic_modify_stat(RlU32 relics, unsigned stat, unsigned base);
unsigned rl_relic_modify_damage(RlU32 relics, unsigned type, unsigned base);
unsigned rl_relic_modify_money(RlU32 relics, unsigned base);
void rl_relic_end_of_turn(RlU32 relics, RlMon *mon);
void rl_relic_post_battle(RunState *run);
unsigned rl_relic_reward_count(RlU32 relics);
int rl_relic_status_blocked(RlU32 relics, unsigned turn);
int rl_relic_survive(RlU32 relics, RlBattleHooks *hooks, RlMon *mon);
unsigned rl_relic_crit_bonus(RlU32 relics);
unsigned rl_relic_pp_max(RlU32 relics, unsigned base);
int rl_relic_consume_recruit(RunState *run, unsigned relic, const RlMon *mon);

#endif
