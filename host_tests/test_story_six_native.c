#include <stdio.h>
#include <string.h>
#include "tools/rltest/story_six_fixture.h"
#include "src/story/sc_native_probe.h"

#undef assert
#define assert(condition) do { if (!(condition)) { \
    printf("Six-party adapter assertion failed at line %u\n", __LINE__); return __LINE__; \
} } while (0)

static int records(const ScState *state, const RlU8 map[6], unsigned count,
                     unk_func_80026268_arg0 native[6]) {
    unsigned i;
    memset(native, 0, 6 * sizeof(*native));
    for (i = 0; i < count; i++) native[i].unk_00.unk_00 = state->party[map[i]].species;
    assert(sc_native_publish(state, native, count, map) == SC_OK);
    return 0;
}

int main(void) {
    ScState checkpoint, state, before, expected;
    ScBridge bridge, prior_bridge;
    ScCombat combat;
    ScBattleSnapshot snapshots[6], wanted[6];
    unk_func_80026268_arg0 native[6], prior_native[6];
    RlU8 map[6], actual[6], prior_map[6];
    unsigned rotation, i, active, enemy;
    assert(sc_six_checkpoint(&checkpoint, &sc_catalog, NULL, NULL) == SC_OK);
    assert(checkpoint.party[3].species == checkpoint.party[4].species);
    for (rotation = 0; rotation < 6; rotation++) {
        state = checkpoint;
        for (i = 0; i < 6; i++) {
            map[i] = (RlU8)((i + rotation) % 6);
            state.party[i].cur_hp--;
            state.party[i].pp[0] -= (RlU8)(i + 1);
            state.party[i].status = (RlU8)(i ? 8 : 0);
        }
        assert(sc_native_mapping(&state, 6, map, actual) == SC_OK);
        assert(!memcmp(actual, map, 6));
        assert(records(&state, actual, 6, native) == 0);
        for (active = 0; active < 6; active++) {
            assert(sc_native_capture(&state, native, 6, actual, active, snapshots) == SC_OK);
            for (i = 0; i < 6; i++) {
                assert(snapshots[i].hp == state.party[i].cur_hp);
                assert(snapshots[i].status == state.party[i].status);
                assert(!memcmp(snapshots[i].pp, state.party[i].pp, 4));
            }
        }
        memcpy(prior_map, actual, 6);
        map[1] = map[0];
        assert(sc_native_mapping(&state, 6, map, actual) == SC_INVALID);
        assert(!memcmp(actual, prior_map, 6));
        memcpy(prior_native, native, sizeof(native));
        assert(sc_native_publish(&state, native, 6, map) == SC_INVALID);
        assert(!memcmp(native, prior_native, sizeof(native)));
        map[1] = 6;
        assert(sc_native_mapping(&state, 6, map, actual) == SC_INVALID);
        assert(!memcmp(actual, prior_map, 6));
        assert(sc_begin(&state, &sc_catalog, 58, 0) == SC_OK);
        assert(sc_combat_prepare(&combat, &state, &sc_catalog) == SC_OK);
        memset(&bridge, 0, sizeof(bridge)); bridge.enemy_count = 2;
        before = state; prior_bridge = bridge;
        assert(sc_native_observe_combat(&state, &bridge, &combat, native, 6,
                                        map, 0, 0, 0) == SC_INVALID);
        assert(!memcmp(&state, &before, sizeof(state)) &&
               !memcmp(&bridge, &prior_bridge, sizeof(bridge)));
    }
    state = checkpoint;
    state.party[0].cur_hp = 0; state.party[0].fainted = 1;
    for (i = 0; i < 5; i++) map[i] = (RlU8)(i + 1);
    assert(sc_native_mapping(&state, 5, map, actual) == SC_OK && actual[5] == RL_NO_SLOT);
    assert(records(&state, actual, 5, native) == 0);
    assert(sc_native_capture(&state, native, 5, actual, 0, snapshots) == SC_OK);
    assert(!snapshots[0].hp);
    map[4] = 0;
    memcpy(prior_map, actual, 6);
    assert(sc_native_mapping(&state, 5, map, actual) == SC_INVALID);
    assert(!memcmp(actual, prior_map, 6));

    for (active = 0; active < 6; active++) {
        state = checkpoint;
        assert(sc_begin(&state, &sc_catalog, 58, 0) == SC_OK);
        assert(sc_combat_prepare(&combat, &state, &sc_catalog) == SC_OK);
        memset(&bridge, 0, sizeof(bridge)); bridge.enemy_count = 2;
        for (i = 0; i < 6; i++) map[i] = (RlU8)(5 - i);
        assert(sc_native_mapping(&state, 6, map, actual) == SC_OK);
        assert(records(&state, actual, 6, native) == 0);
        for (enemy = 0; enemy < 2; enemy++) {
            before = state;
            assert(sc_native_capture(&state, native, 6, actual, active, snapshots) == SC_OK);
            expected = state;
            assert(sc_on_ko(&expected, &sc_catalog, enemy, (RlU8)(1U << actual[active]), 0) == SC_OK);
            assert(sc_native_observe_combat(&state, &bridge, &combat, native, 6,
                                            actual, active, enemy, 0) == SC_OK);
            assert(!memcmp(&state, &expected, sizeof(state)));
            assert(bridge.participants[enemy] == (1U << actual[active]));
            for (i = 0; i < 6; i++)
                if (i != actual[active]) assert(!memcmp(&state.party[i], &before.party[i], sizeof(RlMon)));
            while (state.learn_mask)
                for (i = 0; i < 6; i++)
                    if (state.learn_mask & (1U << i))
                        assert(sc_learn(&state, &sc_catalog, i, 3, 0) == SC_OK);
            assert(sc_native_publish(&state, native, 6, actual) == SC_OK);
            before = state; prior_bridge = bridge;
            assert(sc_native_observe_combat(&state, &bridge, &combat, native, 6,
                                            actual, active, enemy, 0) == SC_OK);
            assert(!memcmp(&state, &before, sizeof(state)) &&
                   !memcmp(&bridge, &prior_bridge, sizeof(bridge)));
        }
    }
    state = checkpoint;
    assert(sc_begin(&state, &sc_catalog, 58, 0) == SC_OK);
    assert(sc_combat_prepare(&combat, &state, &sc_catalog) == SC_OK);
    memset(&bridge, 0, sizeof(bridge)); bridge.enemy_count = 2;
    for (i = 0; i < 6; i++) map[i] = (RlU8)i;
    assert(records(&state, map, 6, native) == 0);
    assert(sc_native_observe_combat(&state, &bridge, &combat, native, 6, map, 0, 0, 10) == SC_OK);
    native[0].unk_02 = 0;
    assert(sc_native_capture(&state, native, 6, map, 1, wanted) == SC_OK);
    expected = state;
    for (i = 0; i < 6; i++) {
        expected.party[i].cur_hp = wanted[i].hp;
        expected.party[i].fainted = !wanted[i].hp;
    }
    before = expected;
    assert(sc_on_ko(&expected, &sc_catalog, 0, 3, 0) == SC_OK);
    assert(sc_native_observe_combat(&state, &bridge, &combat, native, 6, map, 1, 0, 0) == SC_OK);
    assert(!memcmp(&state, &expected, sizeof(state)) && bridge.participants[0] == 3);
    assert(state.party[0].exp == before.party[0].exp);
    assert(state.party[1].exp - before.party[1].exp == ((86 / 2 * 12 / 7) * 3 / 2));
    puts("Story six-party mapping/active/bench/fainted/participant/duplicate/transactional adapter: OK (host only)");
    return 0;
}
