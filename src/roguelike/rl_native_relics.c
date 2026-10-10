#ifdef ROGUELIKE
#include "rl_native_relics.h"
#include "src/roguelike/rl_battle.h"

volatile RlRelicTrace gRlRelicTrace;
unsigned int gRlNativeRelics;

static RlBattleSide* player_side;
static unsigned int completed_rounds;
static int round_open;
static int focus_used;

static int owned(void) {
    return gRlBattleActive && player_side != NULL;
}

static int player_mon(RlBattleMon* mon) {
    return owned() && mon == rl_battle_mon(player_side);
}

static void trace(unsigned int kind, unsigned int a, unsigned int b,
                  unsigned int c, unsigned int d, unsigned int e, unsigned int f) {
    volatile RlRelicTraceEvent* event;
    unsigned int index = gRlRelicTrace.count;
    if (index >= RL_RELIC_TRACE_CAPACITY) {
        gRlRelicTrace.dropped++;
        return;
    }
    event = &gRlRelicTrace.events[index];
    event->kind = kind;
    event->turn = completed_rounds;
    event->a = a;
    event->b = b;
    event->c = c;
    event->d = d;
    event->e = e;
    event->f = f;
    gRlRelicTrace.count = index + 1;
}

void rl_native_relics_reset(unsigned int relics) {
    gRlNativeRelics = relics;
    player_side = NULL;
    completed_rounds = 0;
    round_open = 0;
    focus_used = 0;
    gRlRelicTrace.count = 0;
    gRlRelicTrace.dropped = 0;
}

void rl_native_relics_bind(RlBattleSide* player, RlBattleSide* enemy) {
    if (gRlBattleActive) {
        player_side = player;
    }
}

static void modify_stat(unsigned int stat, u16* effective, u16* base,
                        unsigned int species) {
    unsigned int before = *base;
    unsigned int effective_before = *effective;
    *base = MIN(999, rl_relic_modify_stat(gRlNativeRelics, stat, before));
    *effective = *base;
    trace(1, stat, before, *base, effective_before, *effective, species);
}

void rl_native_relics_stats(RlBattleSide* side) {
    RlBattleMon* mon = rl_battle_mon(side);
    if (!player_mon(mon)) {
        return;
    }
    modify_stat(RL_DEFENSE, &rl_battle_defense(mon), &rl_battle_base_defense(mon),
                rl_battle_species(mon));
    modify_stat(RL_SPEED, &rl_battle_speed(mon), &rl_battle_base_speed(mon),
                rl_battle_species(mon));
    modify_stat(RL_SPECIAL, &rl_battle_special(mon), &rl_battle_base_special(mon),
                rl_battle_species(mon));
}

void rl_native_relics_open_round(void) {
    if (owned()) {
        round_open = 1;
    }
}

int rl_native_relics_finish_round(unsigned int second_action) {
    RlBattleMon* live;
    RlBattlePartyMon* original;
    RlMon mon;
    unsigned int before;
    if (!owned() || !round_open) {
        return 0;
    }
    round_open = 0;
    live = rl_battle_mon(player_side);
    original = rl_battle_party_mon(player_side, rl_battle_active_index(player_side));
    before = rl_battle_hp(live);
    mon.cur_hp = before;
    mon.max_hp = rl_battle_max_hp(live);
    mon.fainted = before == 0;
    rl_relic_end_of_turn(gRlNativeRelics, &mon);
    rl_battle_hp(live) = mon.cur_hp;
    /* Phase 4 has settled action HP; keep the party display/next switch coherent. */
    rl_battle_party_hp(original) = mon.cur_hp;
    trace(3, before, mon.cur_hp, mon.max_hp,
          rl_battle_party_packed_pp(original, 0) & RL_BATTLE_PP_MASK,
          second_action, player_side->unk_654.unk_2D);
    completed_rounds++;
    return before != 0 && mon.cur_hp == 0;
}

static unsigned int move_type(unsigned int native_type) {
    switch (native_type) {
        case 0x14: return RL_FIRE;
        case 0x15: return RL_WATER;
        case 8: return RL_GHOST_TYPE;
        default: return RL_NORMAL;
    }
}

unsigned int rl_native_relics_damage(RlBattleMon* attacker, RlBattleMon* defender,
                                      unsigned int damage, unsigned int type,
                                      int substitute) {
    if (!owned()) {
        return damage;
    }
    if (player_mon(attacker)) {
        damage = MIN(65535, rl_relic_modify_damage(gRlNativeRelics, move_type(type), damage));
    }
    if (player_mon(defender) && !substitute && rl_battle_hp(defender) != 0 &&
        damage >= rl_battle_hp(defender) && !focus_used &&
        (gRlNativeRelics & (1U << RL_FOCUS))) {
        focus_used = 1;
        trace(5, rl_battle_hp(defender), damage, 1, rl_battle_species(defender), 0, 0);
        damage = rl_battle_hp(defender) - 1;
    }
    return damage;
}

void rl_native_relics_damage_trace(RlBattleMon* attacker, unsigned int type,
                                   unsigned int before, unsigned int modified,
                                   unsigned int hp, unsigned int committed) {
    if (owned()) {
        trace(2, type, before, modified, hp, committed, rl_battle_species(attacker));
    }
}

unsigned int rl_native_relics_crit(RlBattleMon* attacker, unsigned int threshold) {
    if (player_mon(attacker) && rl_relic_crit_bonus(gRlNativeRelics)) {
        return MIN(255, threshold * 2);
    }
    return threshold;
}

void rl_native_relics_crit_trace(RlBattleMon* attacker, unsigned int before,
                                unsigned int after, unsigned int draw) {
    if (owned()) {
        trace(6, before, after, draw, draw < after, rl_battle_species(attacker),
              attacker->unk_44.unk_00);
    }
}

int rl_native_relics_block_status(RlBattleMon* target, unsigned int status,
                                  unsigned int move) {
    if (player_mon(target) && rl_relic_status_blocked(gRlNativeRelics, completed_rounds)) {
        trace(4, status, rl_battle_status(target), move, 0, 0, 0);
        return 1;
    }
    return 0;
}

void rl_native_relics_end(unsigned int result) {
    RlBattleMon* live;
    RlBattlePartyMon* original;
    if (owned()) {
        live = rl_battle_mon(player_side);
        original = rl_battle_party_mon(player_side, rl_battle_active_index(player_side));
        trace(7, rl_battle_hp(live), rl_battle_party_hp(original), result, 0, 0, 0);
    }
    player_side = NULL;
    round_open = 0;
    gRlNativeRelics = 0;
}
#endif
