#include <string.h>
#include <stddef.h>
#define ROGUELIKE
#include "src/roguelike/rl_battle.c"
#include "src/roguelike/rl_rosters.c"

typedef char rules_size_check[(sizeof(RlBattleRules) == 12) ? 1 : -1];
typedef char rules_relic_offset_check[(offsetof(RlBattleRules, relics) == 8) ? 1 : -1];

#undef assert
#define assert(condition) do { if (!(condition)) return __LINE__; } while (0)

PokemonStats D_80070F84[152];
unk_D_80072B00 D_80072B00[165];

u32 func_800224B8(s32 species, u16 level) {
    return level * level * level;
}

void func_80022338(unk_func_80026268_arg0* mon) {
    int level;
    for (level = 1; level < 100; level++) {
        if (mon->unk_10 < func_800224B8(mon->unk_00.unk_00, level + 1)) {
            break;
        }
    }
    mon->unk_24 = level;
    mon->unk_26 = 200;
    mon->unk_28 = 201;
    mon->unk_2A = 202;
    mon->unk_2C = 203;
    mon->unk_2E = 204;
}

unk_func_80026268_arg0* func_8002B924(unk_func_80026268_arg0* mon, u8 species, s16 level) {
    mon->unk_00.unk_00 = species;
    mon->unk_10 = func_800224B8(species, level);
    mon->unk_26 = 200;
    return mon;
}

u8 func_80022A38(s32 species) {
    return 0x83;
}

void func_800228B0(unk_func_80026268_arg0* mon) {
}

void osSyncPrintf(const char* fmt, ...) {
}

int main(void) {
    RlMon source;
    RlMon returned;
    unk_func_80026268_arg0 native;
    RlMon team[6];
    RlMetaRentalLoadout meta_catalog[2][RL_META_MAX_CATALOG_RECORDS];
    RlMetaMoveVariant variants[RL_META_MAX_CATALOG_RECORDS];
    RlMeta meta;
    RunState run;
    static const unsigned int meta_sets[RL_META_RENTAL_SOURCE_COUNT] = {
        RL_RENTAL_SET_POKE, RL_RENTAL_SET_PRIME,
        RL_RENTAL_SET_PETIT, RL_RENTAL_SET_PRIME_R2
    };
    static const unsigned int meta_options[RL_META_RENTAL_SOURCE_COUNT] = {
        RL_META_MOVESET_1, RL_META_MOVESET_2,
        RL_META_MOVESET_3, RL_META_MOVESET_4
    };
    int act, lane, round, count, i, source_index;
    unsigned char ai;

    memset(&run, 0, sizeof(run));
    run.seed = 12345;
    for (i = 0; i < 165; i++) D_80072B00[i].unk_05 = 15;
    for (i = 1; i <= 151; i++) {
        D_80070F84[i].baseHP = 80;
        D_80070F84[i].baseSpecial = 80;
        D_80070F84[i].baseSpeed = 80;
        D_80070F84[i].baseDefense = 80;
        D_80070F84[i].baseAttack = 80;
    }
    for (round = 1; round <= 2; round++) {
        run.ascension = round == 2 ? 8 : 0;
        for (act = 0; act < 4; act++) {
            run.act = act;
            run.row = act == 3 ? 0 : 6;
            run.map.lanes[run.row] = act == 3 ? 1 : 4;
            for (lane = 0; lane < run.map.lanes[run.row]; lane++) {
                run.lane = lane;
                run.map.nodes[run.row][lane].type = RL_BOSS;
                count = rl_load_encounter(&run, team, 6, &ai);
                assert(count == (act == 3 ? 1 : 6));
                for (i = 0; i < count; i++) {
                    assert(team[i].level ==
                        rl_enemy_level(act, run.row, RL_BOSS, run.ascension));
                    assert(team[i].dvs == 0xffff);
                    assert(team[i].cur_hp == team[i].max_hp);
                    assert(team[i].held_relic == RL_NO_SLOT);
                }
                if (act == 3) assert(team[0].species == 150);
                assert(rl_load_encounter(&run, team, 0, &ai) == -1);
            }
        }
    }

    memset(&source, 0, sizeof(source));
    source.species = 25;
    source.level = 50;
    source.moves[0] = 85;
    source.moves[1] = 45;
    source.max_pp[0] = 24;
    source.max_pp[1] = 61;
    source.pp[0] = 3;
    source.pp[1] = 9;
    source.cur_hp = 71;
    source.status = 0x40;
    source.exp = 125001;
    D_80072B00[84].unk_05 = 15;
    D_80072B00[44].unk_05 = 40;
    strcpy(source.nickname, "CUSTOM");
    assert(rl_valid_team(&source, 1));
    rl_to_native(&source, &native, 1);
    assert(native.unk_00.unk_01 == 0x83);
    assert(native.unk_02 == 71);
    assert(native.unk_05 == 0x40);
    assert(native.unk_1E == 0);
    assert(native.unk_14 == 0);
    assert(native.unk_20[0] == 0xC3);
    assert(native.unk_20[1] == 0xC9);
    assert(native.unk_20[2] == 0);
    assert(native.unk_10 == 125001);
    assert(strcmp((char*)native.unk_30, "CUSTOM") == 0);
    memset(&returned, 0, sizeof(returned));
    rl_from_native(&native, &returned);
    assert(returned.pp[0] == 3 && returned.max_pp[0] == 24);
    assert(returned.pp[1] == 9 && returned.max_pp[1] == 61);
    assert(returned.cur_hp == 71 && returned.max_hp == 200);
    assert(returned.current_stats[0] == 200 && returned.current_stats[1] == 201);
    assert(returned.current_stats[2] == 202 && returned.current_stats[3] == 203);
    assert(returned.current_stats[4] == 204);
    assert(returned.status == 0x40 && !returned.fainted);
    native.unk_02 = 0;
    rl_from_native(&native, &returned);
    assert(returned.fainted);
    source.cur_hp = 300;
    source.pp[0] = 255;
    rl_to_native(&source, &native, 1);
    assert(native.unk_02 == 200);
    assert((native.unk_20[0] & 0x3F) == 24);
    rl_to_native(&source, &native, 0);
    assert(native.unk_02 == 200 && native.unk_05 == 0);
    assert((native.unk_20[1] & 0x3F) == 61);
    source.dvs = 0x1234;
    rl_to_native(&source, &native, 1);
    assert(GetPokemonDV(STAT_HP, native.unk_1E) == 10);
    assert(GetPokemonDV(STAT_SPECIAL, native.unk_1E) == 1); /* Combat Attack. */
    assert(GetPokemonDV(STAT_SPEED, native.unk_1E) == 2); /* Combat Defense. */
    assert(GetPokemonDV(STAT_DEFENSE, native.unk_1E) == 3); /* Combat Speed. */
    assert(GetPokemonDV(STAT_ATTACK, native.unk_1E) == 4); /* Combat Special. */
    source.moves[0] = 166;
    assert(!rl_valid_team(&source, 1));
    assert(!rl_valid_team(&source, 7));
    assert(!rl_valid_team(NULL, 1));
    assert(rl_load_trainer(RL_TRAINER_SET_MEWTWO, 0, team, 6, &ai) == 1);
    assert(rl_trainer_count(RL_TRAINER_SET_POKE_POKE) == 8);
    assert(rl_trainer_count(RL_NUM_TRAINER_SETS) == -1);
    assert(team[0].species == 150 && team[0].level == 100);
    assert(team[0].cur_hp == team[0].max_hp && !team[0].fainted);
    assert(team[0].current_stats[0] == 200 && team[0].current_stats[1] == 201);
    assert(team[0].current_stats[2] == 202 && team[0].current_stats[3] == 203);
    assert(team[0].current_stats[4] == 204);
    assert(rl_load_trainer(RL_TRAINER_SET_ELITE_FOUR, 0, team, 6, &ai) == 6);
    assert(rl_load_trainer(RL_NUM_TRAINER_SETS, 0, team, 6, &ai) == -1);
    assert(rl_load_trainer(RL_TRAINER_SET_ELITE_FOUR, 0, team, 1, &ai) == -1);
    assert(rl_load_rental_pool(RL_RENTAL_SET_POKE, team, 6) == -1);
    rl_meta_init(&meta);
    meta.currency = 1000;
    for (source_index = 0; source_index < RL_META_RENTAL_SOURCE_COUNT;
         source_index++) {
        count = rl_load_meta_rental_catalog(source_index, meta_catalog[0],
                                            RL_META_MAX_CATALOG_RECORDS);
        assert(count > 0 && (unsigned)count ==
               gRlRentalSets[meta_sets[source_index]].count);
        assert(rl_meta_purchase_option(&meta, meta_options[source_index]) ==
               RL_OK);
        for (i = 0; i < count; i++) {
            assert(meta_catalog[0][i].species ==
                   gRlRentalSets[meta_sets[source_index]].mons[i].species);
            assert(meta_catalog[0][i].source == source_index);
            assert(!memcmp(meta_catalog[0][i].moves,
                gRlRentalSets[meta_sets[source_index]].mons[i].moves, 4));
        }
    }
    assert(rl_load_meta_rental_catalog(RL_META_RENTAL_SOURCE_COUNT,
        meta_catalog[0], RL_META_MAX_CATALOG_RECORDS) == -1);
    for (source_index = 0; source_index < RL_META_RENTAL_SOURCE_COUNT;
         source_index++) {
        int target_count, current_count, current_source, found;
        target_count = rl_load_meta_rental_catalog(source_index,
            meta_catalog[0], RL_META_MAX_CATALOG_RECORDS);
        found = 0;
        for (current_source = 0;
             current_source < RL_META_RENTAL_SOURCE_COUNT && !found;
             current_source++) {
            if (current_source == source_index) continue;
            current_count = rl_load_meta_rental_catalog(current_source,
                meta_catalog[1], RL_META_MAX_CATALOG_RECORDS);
            for (i = 0; i < current_count && !found; i++) {
                int j;
                for (j = 0; j < target_count; j++) {
                    int variant_count, k, matching;
                    if (meta_catalog[1][i].species !=
                        meta_catalog[0][j].species ||
                        !memcmp(meta_catalog[1][i].moves,
                                meta_catalog[0][j].moves, 4)) continue;
                    variant_count = rl_meta_list_move_variants(&meta,
                        meta_options[source_index],
                        meta_catalog[1][i].species,
                        meta_catalog[1][i].moves, meta_catalog[0],
                        target_count, variants, RL_META_MAX_CATALOG_RECORDS);
                    assert(variant_count > 0);
                    matching = 0;
                    for (k = 0; k < variant_count; k++)
                        if (!memcmp(variants[k].moves,
                                    meta_catalog[0][j].moves, 4))
                            matching = 1;
                    assert(matching);
                    found = 1;
                    break;
                }
            }
        }
        assert(found);
    }
    return 0;
}
