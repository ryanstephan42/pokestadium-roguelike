#include "src/roguelike/rl_native_relics.h"
#include "src/roguelike/rl_battle.h"

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)

int gRlBattleActive;

int main(void) {
    RlBattleSide player = { 0 };
    RlBattleSide enemy = { 0 };
    unk_D_800AE540_0004 party = { 0 };
    RlBattleMon* live = rl_battle_mon(&player);
    RlBattleMon* opponent = rl_battle_mon(&enemy);
    unsigned int mask = (1U << RL_BOULDER) | (1U << RL_THUNDER) |
        (1U << RL_MARSH) | (1U << RL_CASCADE) | (1U << RL_VOLCANO) |
        (1U << RL_GHOST) | (1U << RL_FOCUS) | (1U << RL_SOUL) |
        (1U << RL_SCOPE_LENS) | (1U << RL_LEFTOVERS);

    CHECK(sizeof(RlBattleRules) == 12);
    CHECK(sizeof(RlRelicTraceEvent) == 32);
    player.unk_724 = &party;
    live->unk_0B = 25;
    live->unk_0C = 50;
    live->unk_28 = 160;
    live->unk_3A = live->unk_2C = 100;
    live->unk_3C = live->unk_2E = 101;
    live->unk_3E = live->unk_30 = 999;
    rl_native_relics_reset(mask);
    rl_native_relics_bind(&player, &enemy);
    rl_native_relics_stats(&player);
    CHECK(live->unk_2C == 100 && gRlRelicTrace.count == 0);

    gRlBattleActive = 1;
    rl_native_relics_bind(&player, &enemy);
    rl_native_relics_stats(&player);
    CHECK(live->unk_2C == 110 && live->unk_3A == 110);
    CHECK(live->unk_2E == 111 && live->unk_3C == 111);
    CHECK(live->unk_30 == 999 && live->unk_3E == 999);
    CHECK(rl_native_relics_damage(live, opponent, 100, 0x15, 0) == 115);
    CHECK(rl_native_relics_damage(live, opponent, 100, 0x14, 0) == 115);
    CHECK(rl_native_relics_damage(live, opponent, 100, 8, 0) == 115);
    CHECK(rl_native_relics_damage(live, opponent, 100, 0, 0) == 100);
    CHECK(rl_native_relics_damage(opponent, live, 50, 0, 1) == 50);
    CHECK(rl_native_relics_damage(opponent, live, 50, 0, 0) == 49);
    CHECK(rl_native_relics_damage(opponent, live, 50, 0, 0) == 50);
    CHECK(rl_native_relics_crit(live, 100) == 200);
    CHECK(rl_native_relics_crit(live, 200) == 255);
    CHECK(rl_native_relics_crit(opponent, 100) == 100);
    CHECK(rl_native_relics_block_status(live, 8, 92));
    CHECK(!rl_native_relics_block_status(opponent, 8, 92));
    CHECK(!rl_native_relics_finish_round(1));
    CHECK(live->unk_0C == 50);
    rl_native_relics_open_round(&enemy);
    CHECK(!rl_native_relics_finish_round(1));
    CHECK(live->unk_0C == 60 && party.unk_01C[0].unk_02 == 60);
    CHECK(!rl_native_relics_finish_round(1));
    CHECK(live->unk_0C == 60);
    CHECK(rl_native_relics_block_status(live, 0x40, 86));
    rl_native_relics_open_round(&enemy);
    CHECK(!rl_native_relics_finish_round(0));
    CHECK(!rl_native_relics_block_status(live, 0x40, 86));
    /* Fainted-mon replacement sends neither tick nor advance the Soul window. */
    enemy.unk_654.unk_2D = RL_BATTLE_SWITCH_FAINTED;
    {
        unsigned int before = gRlRelicTrace.count;
        rl_native_relics_open_round(&enemy);
        CHECK(gRlRelicTrace.count == before + 1);
        CHECK(gRlRelicTrace.events[before].kind == 8 && gRlRelicTrace.events[before].e == 0);
        CHECK(gRlRelicTrace.events[before].d == RL_BATTLE_SWITCH_FAINTED);
    }
    CHECK(!rl_native_relics_finish_round(0));
    CHECK(live->unk_0C == 70);
    CHECK(!rl_native_relics_block_status(live, 0x40, 86));
    enemy.unk_654.unk_2D = 0;
    player.unk_654.unk_2D = RL_BATTLE_SWITCH_FAINTED;
    rl_native_relics_open_round(&enemy);
    CHECK(!rl_native_relics_finish_round(0));
    CHECK(live->unk_0C == 70);
    /* A voluntary switch is a real turn. */
    player.unk_654.unk_2D = RL_BATTLE_SWITCH_VOLUNTARY;
    rl_native_relics_open_round(&enemy);
    CHECK(gRlRelicTrace.events[gRlRelicTrace.count - 1].e == 1);
    CHECK(!rl_native_relics_finish_round(0));
    CHECK(live->unk_0C == 80);
    player.unk_654.unk_2D = 0;
    /* An unclosed turn is reported, not silently merged. */
    rl_native_relics_open_round(&enemy);
    rl_native_relics_open_round(&enemy);
    CHECK(gRlRelicTrace.events[gRlRelicTrace.count - 2].kind == 9);
    CHECK(!rl_native_relics_finish_round(0));
    CHECK(live->unk_0C == 90);
    CHECK(!rl_native_relics_finish_round(0));
    CHECK(live->unk_0C == 90);
    rl_native_relics_end(1);
    CHECK(rl_native_relics_damage(live, opponent, 100, 0x15, 0) == 100);
    CHECK(!rl_native_relics_block_status(live, 8, 92));

    rl_native_relics_reset(1U << RL_CURSED_POWER);
    rl_native_relics_bind(&player, &enemy);
    live->unk_0C = 1;
    CHECK(rl_native_relics_damage(live, opponent, 100, 0, 0) == 110);
    rl_native_relics_open_round(&enemy);
    CHECK(rl_native_relics_finish_round(1));
    CHECK(live->unk_0C == 0 && party.unk_01C[0].unk_02 == 0);
    rl_native_relics_open_round(&enemy);
    CHECK(!rl_native_relics_finish_round(1));
    CHECK(live->unk_0C == 0);
    return 0;
}
