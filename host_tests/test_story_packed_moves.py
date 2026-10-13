"""Quick actual PURE source tests; no emulator or historical fixture edits."""

import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class PackedMovesTests(unittest.TestCase):
    def test_compaction_import_learning_and_probe_isolation(self):
        fixture = r"""
#include <assert.h>
#include <string.h>
#include "src/story/sc_combat.h"
int main(void) {
    RlMon mon;
    ScState state;
    RlU8 pp[166];
    ScSpecies species[152];
    ScCatalog catalog;
    ScTemplate input;
    ScEncounter encounter;
    ScState before;
    memset(&mon, 0, sizeof(mon));
    mon.moves[1] = 10; mon.moves[3] = 52;
    mon.pp[1] = 7; mon.max_pp[1] = 35;
    mon.pp[3] = 3; mon.max_pp[3] = 25;
    assert(!sc_moves_packed(&mon));
    sc_compact_moves(&mon);
    assert(sc_moves_packed(&mon));
    assert(mon.moves[0] == 10 && mon.moves[1] == 52 && !mon.moves[2]);
    assert(mon.pp[0] == 7 && mon.pp[1] == 3 && !mon.pp[2]);
    assert(mon.max_pp[0] == 35 && mon.max_pp[1] == 25 && !mon.max_pp[2]);
    memset(&state, 0, sizeof(state)); memset(pp, 0, sizeof(pp));
    state.party_count = 1; state.phase = SC_LEARNING; state.return_phase = SC_BATTLE;
    state.learn_mask = 1; state.learn_moves[0] = 45; state.party[0] = mon; pp[45] = 40;
    assert(sc_learn_pp(&state, pp, 0, 3, 1) == SC_OK);
#ifdef SC_STORY_PACKED_MOVES
    assert(state.party[0].moves[2] == 45 && !state.party[0].moves[3]);
#else
    assert(!state.party[0].moves[2] && state.party[0].moves[3] == 45);
#endif
    state.party[0].moves[2] = 45; state.party[0].moves[3] = 33;
    state.phase = SC_LEARNING; state.learn_mask = 1; state.learn_moves[0] = 39; pp[39] = 30;
    assert(sc_learn_pp(&state, pp, 0, 1, 1) == SC_OK);
    assert(state.party[0].moves[1] == 39 && state.party[0].moves[3] == 33);
    assert(state.party[0].pp[1] == 30 && state.party[0].max_pp[1] == 30);
    memset(&catalog, 0, sizeof(catalog)); memset(&encounter, 0, sizeof(encounter));
    catalog.pp = pp; catalog.encounters = &encounter; catalog.count = 1;
    state.party[0] = mon; state.phase = SC_REWARD; state.offered_slot = 0;
    state.offer.moves[0] = 45;
    before = state;
    assert(sc_transfer(&state, &catalog, 0, 0, 3, 0) == SC_PENDING);
    assert(!memcmp(&before, &state, sizeof(state)));
    assert(sc_transfer(&state, &catalog, 0, 0, 3, 1) == SC_OK);
#ifdef SC_STORY_PACKED_MOVES
    assert(state.party[0].moves[2] == 45 && state.party[0].pp[2] == 40);
    assert(state.party[0].pp[0] == 7 && state.party[0].pp[1] == 3);
    assert(!state.party[0].moves[3] && !state.party[0].pp[3]);
#else
    assert(state.party[0].moves[3] == 45);
#endif
    memset(species, 0, sizeof(species)); memset(&catalog, 0, sizeof(catalog));
    memset(&input, 0, sizeof(input));
    catalog.species = species; catalog.pp = pp;
    species[4].growth = RL_MEDIUM_SLOW;
    species[4].base[0] = 39; species[4].base[1] = 52; species[4].base[2] = 43;
    species[4].base[3] = 65; species[4].base[4] = 50;
    input.species = 4; input.level = 12; input.moves[0] = 10; input.moves[3] = 52;
    pp[10] = 35; pp[52] = 25;
    assert(sc_make_mon(&catalog, &input, 0x8888, &mon) == SC_OK);
#ifdef SC_STORY_PACKED_MOVES
    assert(mon.moves[1] == 52 && mon.pp[1] == 25 && !mon.moves[3]);
    input.moves[0] = 0;
    assert(sc_make_mon(&catalog, &input, 0x8888, &mon) == SC_OK);
    assert(mon.moves[0] == 52 && mon.pp[0] == 25);
#else
    assert(mon.moves[3] == 52 && !mon.moves[1]);
#endif
    return 0;
}
"""
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory)
            (path / "moves.c").write_text(fixture)
            for flags in ([], ["-DSTORY_SIX_PROBE"], ["-DSTORY_NATIVE_PROBE"],
                          ["-DSTORY_SIX_PROBE", "-DSTORY_SERVICE_CANDIDATE"]):
                subprocess.run(["gcc", "-std=gnu89", "-Wall", "-Wextra", "-Werror",
                                "-ffunction-sections", "-fdata-sections", "-I.",
                                "-Wl,--gc-sections", *flags, str(path / "moves.c"),
                                "src/story/sc_make.c", "src/story/sc_combat.c",
                                "src/story/sc_progression.c", "src/story/sc_core.c",
                                "src/story/sc_graph.c",
                                "-o", str(path / "moves")],
                               cwd=ROOT, check=True)
                subprocess.run([str(path / "moves")], check=True, capture_output=True)


if __name__ == "__main__":
    unittest.main()
