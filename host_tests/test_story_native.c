#include <string.h>
#include <stdio.h>
#include "src/story/sc_native_probe.h"

#undef assert
#define assert(condition) do { if (!(condition)) return __LINE__; } while (0)

int main(void) {
    unk_func_80026268_arg0 native, prior;
    RlMon mon;
    ScTemplate input;
    ScBattleSnapshot snapshot;
    ScState state, before;
    ScBridge bridge;
    ScOptions options = {0, SC_STANDARD, SC_STARTER_TRIO, 1};
    RlU8 mapping[6] = {1,0,0,0,0,0};
    unsigned i, lab = 0;
    memset(&input, 0, sizeof(input));
    input.species = 74; input.level = 12; input.fixed_dvs = 1; input.dvs = 0x8889;
    assert(sc_make_mon(&sc_catalog, &input, 0, &mon) == SC_OK);
    assert(mon.max_hp == 33);
    memset(&native, 0xa5, sizeof(native));
    native.unk_00.unk_00 = mon.species;
    native.unk_24 = mon.level;
    native.unk_26 = 34; /* Deliberately different Stadium HP-DV result. */
    assert(sc_native_record(&mon, &native) == SC_OK);
    assert(native.unk_26 == 33 && native.unk_02 == 33 && native.unk_1E == 0x8889);
    assert(native.unk_28 == mon.current_stats[1]);
    assert(native.unk_2A == mon.current_stats[2]);
    assert(native.unk_2C == mon.current_stats[3]);
    assert(native.unk_2E == mon.current_stats[4]);
    assert(native.unk_10 == mon.exp);
    for (i = 0; i < 4; i++) assert(native.unk_20[i] == mon.pp[i]);
    native.unk_02 = 17;
    native.unk_05 = 8;
    assert(sc_native_snapshot(&native, &mon, &snapshot) == SC_OK);
    assert(snapshot.hp == 17 && snapshot.status == 8);
    prior = native;
    mon.species = 95;
    assert(sc_native_record(&mon, &native) == SC_INVALID);
    assert(memcmp(&native, &prior, sizeof(native)) == 0);
    assert(sc_native_snapshot(&native, &mon, &snapshot) == SC_INVALID);
    assert(sc_new(&state, &sc_catalog, &options, 42) == SC_OK);
    state.party_count = 2;
    state.party[1] = state.party[0];
    state.party[0].cur_hp = 0; state.party[0].fainted = 1;
    state.party[1].level = 6;
    state.party[1].exp = sc_xp(7, state.party[1].growth) - 1;
    sc_recalculate(&state.party[1], 1);
    for (i = 0; i < sc_catalog.count; i++)
        if (strcmp(sc_catalog.encounters[i].id, "OaksLab/rival-lab") == 0) lab = i;
    assert(sc_begin(&state, &sc_catalog, lab, 2) == SC_OK);
    memset(&bridge, 0, sizeof(bridge));
    bridge.enemy_count = 1;
    memset(&native, 0, sizeof(native));
    native.unk_00.unk_00 = state.party[1].species;
    assert(sc_native_publish(&state, &native, 1, mapping) == SC_OK);
    assert(sc_native_observe(&state, &bridge, &sc_catalog, &native, 1,
                              mapping, 0, 0, 0) == SC_OK);
    assert(state.learn_mask == 2 && bridge.participants[0] == 2);
    assert(state.party[0].exp == sc_xp(5, state.party[0].growth));
    assert(sc_native_publish(&state, &native, 1, mapping) == SC_OK);
    assert(native.unk_24 == state.party[1].level && native.unk_26 == state.party[1].max_hp);
    before = state;
    assert(sc_native_observe(&state, &bridge, &sc_catalog, &native, 1,
                              mapping, 0, 0, 0) == SC_PENDING);
    assert(memcmp(&state, &before, sizeof(state)) == 0);
    assert(sc_learn(&state, &sc_catalog, 1, 3, 1) == SC_OK);
    assert(sc_native_publish(&state, &native, 1, mapping) == SC_OK);
    assert(native.unk_09[3] == state.party[1].moves[3]);
    assert(sc_native_observe(&state, &bridge, &sc_catalog, &native, 1,
                              mapping, 0, 0, 0) == SC_DUPLICATE);
    mapping[0] = 0;
    assert(sc_native_publish(&state, &native, 1, mapping) == SC_INVALID);
    puts("Story native record stat/DV and state-copy feasibility: OK (not runtime proof)");
    return 0;
}
