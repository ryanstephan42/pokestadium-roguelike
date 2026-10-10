#include "rl_content.h"
#include "rl_save_format.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void roundtrip(const RunState *run, RunState *restored) {
    RlMeta meta, loaded;
    RlU8 bytes[RL_SAVE_MAX_SIZE], has_run;
    unsigned size;
    memset(&meta, 0, sizeof(meta));
    assert(rl_save_encode(&meta, run, bytes, sizeof(bytes), &size) == RL_OK);
    assert(size == 917);
    assert(rl_save_decode(&loaded, restored, &has_run, bytes, size) == RL_OK);
    assert(has_run && restored->rng == run->rng);
    assert(!memcmp(&restored->map, &run->map, sizeof(run->map)));
}

static void test_seed_sweep(void) {
    RunState run, restored;
    RlEncounter a, b;
    RlU32 seed, before;
    unsigned act, row, lane, asc, centers;
    for (seed = 0; seed < 10000; seed++) {
        asc = seed % 11;
        rl_run_new(&run, seed, asc);
        assert(rl_party_recruit(&run, &rl_placeholder_pool.mons[0]) == RL_OK);
        run.phase = RL_MAP;
        for (act = 0; act < 4; act++) {
            run.act = (RlU8)act;
            run.row = RL_NO_SLOT;
            run.lane = RL_NO_SLOT;
            rl_map_generate(&run.map, &run.rng, act, asc);
            assert(rl_map_valid(&run.map));
            roundtrip(&run, &restored);
            centers = 0;
            for (row = 0; row < rl_map_rows(act); row++) {
                for (lane = 0; lane < run.map.lanes[row]; lane++) {
                    unsigned type;
                    type = run.map.nodes[row][lane].type;
                    if (type == RL_CENTER) centers++;
                    if (type != RL_BATTLE && type != RL_ELITE &&
                        type != RL_BOSS) continue;
                    run.row = restored.row = (RlU8)row;
                    run.lane = restored.lane = (RlU8)lane;
                    before = run.rng;
                    assert(rl_encounter_select(&run, &a) == RL_OK);
                    assert(rl_encounter_select(&restored, &b) == RL_OK);
                    assert(!memcmp(&a, &b, sizeof(a)));
                    assert(run.rng == before);
                    assert(a.round == (asc >= 8 ? 2 : 1));
                    assert(a.roster < RL_ROSTER_COUNT);
                    if (act == 3) {
                        assert(a.roster == RL_ROSTER_MEWTWO && !a.trainer);
                    } else if (type == RL_BOSS && act == 2) {
                        assert(a.roster == RL_ROSTER_RIVAL && a.trainer < 7);
                    } else if (type == RL_BOSS && (act == 0 || !(lane & 1))) {
                        assert(a.roster >= RL_ROSTER_PEWTER &&
                               a.roster <= RL_ROSTER_VIRIDIAN && a.trainer == 3);
                    } else if (a.roster == RL_ROSTER_ELITE_FOUR) {
                        assert(a.trainer < 4);
                    } else {
                        assert(a.trainer < 8);
                    }
                }
            }
            assert(act == 3 || centers);
        }
    }
}

static void test_complete_runs(void) {
    RunState run, restored;
    unsigned asc, act, row, lane, type, next, i;
    RlU32 old_money, old_exp;
    for (asc = 0; asc <= 10; asc++) {
        rl_run_new(&run, 54321, asc);
        assert(rl_party_recruit(&run, &rl_placeholder_pool.mons[0]) == RL_OK);
        run.phase = RL_MAP;
        for (act = 0; act < 4; act++) {
            for (row = 0; row < rl_map_rows(act); row++) {
                next = run.row == RL_NO_SLOT ? 15U :
                    run.map.nodes[run.row][run.lane].edges;
                for (lane = 0; !(next & (1U << lane)); lane++) {}
                assert(rl_run_advance(&run, lane) == RL_OK);
                type = run.map.nodes[row][lane].type;
                old_money = run.money;
                old_exp = run.party[0].exp;
                if (type == RL_BATTLE || type == RL_ELITE || type == RL_BOSS) {
                    assert(rl_run_on_battle_result(&run, 1, 1) == RL_OK);
                    assert(run.money == old_money +
                        rl_battle_money(act, row, type, asc, 0));
                    assert(run.party[0].exp > old_exp);
                    assert(rl_run_on_battle_result(&run, 1, 1) == RL_INVALID);
                }
                roundtrip(&run, &restored);
                assert(rl_run_finish_node(&run) == RL_OK);
            }
        }
        assert(run.phase == RL_WON && run.stats.nodes == 22);
        roundtrip(&run, &restored);
        for (i = 0; i < run.party_count; i++)
            assert(restored.party[i].level == run.party[i].level);
    }
}

static void test_scaling_and_money(void) {
    RunState run;
    RlMon team[6];
    unsigned asc, act, row, type, i, prev;
    rl_run_new(&run, 123, 0);
    for (act = 0; act < 4; act++) {
        run.act = (RlU8)act;
        for (row = 0; row < rl_map_rows(act); row++) {
            run.row = (RlU8)row;
            for (type = RL_BATTLE; type <= RL_BOSS; type++) {
                if (type != RL_BATTLE && type != RL_ELITE && type != RL_BOSS)
                    continue;
                prev = 0;
                for (asc = 0; asc <= 10; asc++) {
                    run.ascension = (RlU8)asc;
                    for (i = 0; i < 6; i++) {
                        team[i] = rl_placeholder_pool.mons[i % 4];
                        team[i].pp[0] = 0;
                    }
                    assert(rl_enemy_scale(&run, type, team, 6) == RL_OK);
                    assert(team[0].current_stats[RL_HP] >= prev);
                    prev = team[0].current_stats[RL_HP];
                    assert(team[0].pp[0] == team[0].max_pp[0]);
                    assert(team[0].level == rl_enemy_level(act,row,type,asc));
                    assert(rl_battle_money(act,row,type,asc,0) > 0);
                }
            }
        }
    }
    assert(rl_battle_money(0, 0, RL_BATTLE, 0, 0) == 100);
    assert(rl_battle_money(0, 0, RL_BATTLE, 10, 1U << RL_AMULET) == 75);
    assert(rl_battle_money(0, 6, RL_BOSS, 0, 0) == 570);
    assert(!rl_battle_money(3, 1, RL_BOSS, 0, 0));
}

int main(void) {
    test_seed_sweep();
    test_complete_runs();
    test_scaling_and_money();
    puts("Content: 10000 seeds x 4 acts, v3 roundtrips, ascensions 0-10: OK");
    return 0;
}
