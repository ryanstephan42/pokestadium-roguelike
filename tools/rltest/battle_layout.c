#include "rl_battle.h"
#include <stddef.h>

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)
#define CHECK_OFFSET(type, field, offset) CHECK(offsetof(type, field) == (offset))

int main(void) {
    RlBattleSide side = { 0 };
    unk_D_800AE540_0004 party = { 0 };
    RlBattleMon* mon = rl_battle_mon(&side);
    RlBattlePartyMon* record;
    s32 i;

    CHECK(sizeof(void*) == 4);
    CHECK(sizeof(RlBattleMon) == 0x64);
    CHECK(sizeof(RlBattlePartyMon) == 0x54);
    CHECK_OFFSET(RlBattleSide, unk_654.unk_38, RL_BATTLE_MON_OFFSET);
    CHECK_OFFSET(RlBattleSide, unk_724, 0x724);
    CHECK_OFFSET(RlBattleSide, unk_654.unk_08, 0x65C);
    CHECK_OFFSET(RlBattleSide, unk_654.unk_18, 0x66C);
    CHECK_OFFSET(RlBattleSide, unk_654.unk_2B, 0x67F);
    CHECK_OFFSET(RlBattleSide, unk_654.unk_2C, 0x680);
    CHECK_OFFSET(unk_D_84390240_000, unk_40, 0x40);
    CHECK_OFFSET(RlBattleMon, unk_0B, 0x0B);
    CHECK_OFFSET(RlBattleMon, unk_0C, RL_BATTLE_MON_HP_OFFSET);
    CHECK_OFFSET(RlBattleMon, unk_15, 0x15);
    CHECK_OFFSET(RlBattleMon, unk_1F, 0x1F);
    CHECK_OFFSET(RlBattleMon, unk_26, 0x26);
    CHECK_OFFSET(RlBattleMon, unk_28, 0x28);
    CHECK_OFFSET(RlBattleMon, unk_2A, 0x2A);
    CHECK_OFFSET(RlBattleMon, unk_2C, 0x2C);
    CHECK_OFFSET(RlBattleMon, unk_2E, 0x2E);
    CHECK_OFFSET(RlBattleMon, unk_30, 0x30);
    CHECK_OFFSET(RlBattleMon, unk_38, 0x38);
    CHECK_OFFSET(RlBattleMon, unk_3A, 0x3A);
    CHECK_OFFSET(RlBattleMon, unk_3C, 0x3C);
    CHECK_OFFSET(RlBattleMon, unk_3E, 0x3E);
    CHECK_OFFSET(RlBattleMon, unk_32, RL_BATTLE_MON_PP_OFFSET);
    CHECK_OFFSET(RlBattleMon, unk_5C, 0x5C);
    CHECK_OFFSET(RlBattlePartyMon, unk_02, 2);
    CHECK_OFFSET(RlBattlePartyMon, unk_05, 5);
    CHECK_OFFSET(RlBattlePartyMon, unk_09, 9);
    CHECK_OFFSET(RlBattlePartyMon, unk_20, 0x20);
    CHECK_OFFSET(RlBattlePartyMon, unk_24, 0x24);
    CHECK_OFFSET(RlBattlePartyMon, unk_26, 0x26);

    side.unk_724 = &party;
    rl_battle_active_index(&side) = 2;
    record = rl_battle_party_mon(&side, rl_battle_active_index(&side));
    CHECK(record == &party.unk_01C[2]);
    rl_battle_hp(mon) = 37;
    rl_battle_max_hp(mon) = 100;
    rl_battle_species(mon) = 25;
    rl_battle_level(mon) = 50;
    rl_battle_status(mon) = 0x40;
    CHECK(mon->unk_0C == 37 && mon->unk_28 == 100);
    CHECK(mon->unk_0B == 25 && mon->unk_26 == 50 && mon->unk_15 == 0x40);
    CHECK(!rl_battle_fainted(mon));
    rl_battle_hp(mon) = 0;
    CHECK(rl_battle_fainted(mon));
    for (i = 0; i < 4; i++) {
        rl_battle_move(mon, i) = 10 + i;
        rl_battle_packed_pp(mon, i) = 0xC0 | (20 + i);
        CHECK(rl_battle_pp(mon, i) == 20 + i);
        CHECK(mon->unk_1F[i] == 10 + i);
        rl_battle_party_packed_pp(record, i) = 0x80 | (5 + i);
        CHECK(record->unk_20[i] == (0x80 | (5 + i)));
    }
    rl_battle_attack(mon) = 101;
    rl_battle_defense(mon) = 102;
    rl_battle_speed(mon) = 103;
    rl_battle_special(mon) = 104;
    CHECK(mon->unk_2A == 101 && mon->unk_2C == 102);
    CHECK(mon->unk_2E == 103 && mon->unk_30 == 104);
    rl_battle_base_attack(mon) = 201;
    rl_battle_base_defense(mon) = 202;
    rl_battle_base_speed(mon) = 203;
    rl_battle_base_special(mon) = 204;
    CHECK(mon->unk_38 == 201 && mon->unk_3A == 202);
    CHECK(mon->unk_3C == 203 && mon->unk_3E == 204);
    rl_battle_incoming_index(&side) = 3;
    rl_battle_trainer_index(&side) = 0;
    rl_battle_incoming_trainer_index(&side) = 1;
    rl_battle_move_slot(&side) = 2;
    CHECK(side.unk_654.unk_18 == 3 && side.unk_654.unk_2B == 0);
    CHECK(side.unk_654.unk_2C == 1 && side.unk_654.unk_0C == 2);
    rl_battle_party_species(record) = 25;
    rl_battle_party_level(record) = 50;
    rl_battle_party_max_hp(record) = 100;
    rl_battle_party_hp(record) = 37;
    rl_battle_party_status(record) = RL_BATTLE_STATUS_PARALYSIS;
    CHECK(record->unk_00.unk_00 == 25 && record->unk_24 == 50);
    CHECK(record->unk_26 == 100 && record->unk_02 == 37 && record->unk_05 == 0x40);
    i = 0;
    rl_battle_move(mon, i++) = 10;
    CHECK(i == 1);
    i = 0;
    CHECK(rl_battle_pp(mon, i++) == 20);
    CHECK(i == 1);
    for (i = 0; i < 6; i++) {
        rl_battle_stage(mon, i) = RL_BATTLE_STAGE_NEUTRAL;
        CHECK(mon->unk_5C[i] == 7);
    }
    return 0;
}
