#ifndef SC_RUNTIME_PROBE_H
#define SC_RUNTIME_PROBE_H

#ifdef STORY_NATIVE_PROBE
#include "sc_native_probe.h"
#include "include/rl_battle.h"

typedef struct ScProbeMailbox {
    RlU32 command, status, controller, create, seed;
    RlU32 active, ticks, ko_count, pause_ticks, resume, force_ko;
    RlU32 phase, exp, level, hp, max_hp, dvs;
    RlU32 live_hp, live_max_hp, live_attack, live_base_attack, attack_stage;
    RlU32 state_address, state_size, completed_crc, interrupted, post_pool_crc;
    RlU32 logic_steps, presentation_steps;
    RlU32 native_max_hp, native_dvs, live_dvs;
    RlU32 stage;
    RlU32 pool_free, arena_archive, arena_count, arena_index, arena_required;
    RlU32 cold_before, cold_after, cold_address, cold_size, cold_busy, cold_calls;
    RlU32 combat_address, combat_size, combat_crc;
    RlU32 map_before, map_before_size, map_loaded, map_loaded_size, map_after, map_after_size;
} ScProbeMailbox;
extern volatile ScProbeMailbox gScProbe;
typedef struct ScBridgeView {
    RlU32 species, level, exp, dvs, hp, max_hp, status;
    RlU32 moves[4], pp[4], stats[5], base[4], stages[6];
} ScBridgeView;
typedef struct ScBridgeProbe {
    RlU32 enabled, action, boundary, error;
    ScBridgeView initial_party, initial_live, carry_party, carry_live;
    ScBridgeView current_party, current_live;
} ScBridgeProbe;
extern volatile ScBridgeProbe gScBridgeProbe;
extern int gScProbeActive;
int sc_runtime_probe_poll(void);
int sc_runtime_probe_tick(RlBattleSide *player, RlBattleSide *enemy);
/* Gate: 0 advance, 1 render only, 2 resume the deferred post-logic frame. */
int sc_runtime_probe_gate(RlBattleSide *player, RlBattleSide *enemy, int *result);
int sc_runtime_probe_after(RlBattleSide *player, RlBattleSide *enemy, int result);
int sc_runtime_probe_paused(void);

#endif
#endif
