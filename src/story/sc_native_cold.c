#ifdef STORY_NATIVE_PROBE
#include "sc_native_probe.h"

int sc_native_observe(ScState *state, ScBridge *bridge, const ScCatalog *catalog,
                      const unk_func_80026268_arg0 *party, unsigned count,
                      const RlU8 mapping[6], unsigned active, unsigned enemy,
                      unsigned live_enemy_hp) {
    ScBattleSnapshot snapshots[6];
    if (sc_native_capture(state, party, count, mapping, active, snapshots) != SC_OK)
        return SC_INVALID;
    return sc_bridge_observe(state, bridge, catalog, snapshots, mapping[active],
                             enemy, live_enemy_hp);
}
#endif
