#ifndef SC_NATIVE_PROBE_H
#define SC_NATIVE_PROBE_H

#ifdef STORY_NATIVE_PROBE
#include "src/22630.h"
#include "sc_bridge.h"
#include "sc_combat.h"

#ifdef STORY_SIX_PROBE
int sc_native_mapping(const ScState *state, unsigned count, const RlU8 indices[6],
                       RlU8 output[6]);
#endif
int sc_native_record(const RlMon *story, unk_func_80026268_arg0 *native);
int sc_native_snapshot(const unk_func_80026268_arg0 *native,
                       const RlMon *story, ScBattleSnapshot *snapshot);
int sc_native_observe(ScState *state, ScBridge *bridge, const ScCatalog *catalog,
                      const unk_func_80026268_arg0 *party, unsigned count,
                      const RlU8 mapping[6], unsigned active, unsigned enemy,
                      unsigned live_enemy_hp);
int sc_native_publish(const ScState *state, unk_func_80026268_arg0 *party,
                      unsigned count, const RlU8 mapping[6]);
int sc_native_observe_combat(ScState *state, ScBridge *bridge, const ScCombat *combat,
                             const unk_func_80026268_arg0 *party, unsigned count,
                             const RlU8 mapping[6], unsigned active, unsigned enemy,
                             unsigned live_enemy_hp);
int sc_native_capture(const ScState *state, const unk_func_80026268_arg0 *party,
                       unsigned count, const RlU8 mapping[6], unsigned active,
                       ScBattleSnapshot snapshots[6]);
int sc_pak_probe_load(unsigned controller, const ScCatalog *catalog, ScSnapshot *snapshot);
int sc_pak_probe_store(unsigned controller, const ScCatalog *catalog,
                       const ScSnapshot *snapshot, int create);
#endif

#endif
