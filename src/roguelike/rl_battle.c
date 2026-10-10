#ifdef ROGUELIKE
#include "src/roguelike/rl_battle.h"
#include "src/22630.h"
#include "src/232C0.h"
#include "src/2C1C0.h"
#include "src/373A0.h"
#include "src/memory.h"

RlLaunchResult gRlLastBattleResult;
volatile unsigned int gRlBattleCount;
int gRlBattleActive;
volatile int gRlBattleTestRequest;

static int rl_valid_team(const RlMon* team, int count) {
    int i;
    int j;
    int has_move;

    if ((team == NULL) || (count < 1) || (count > 6)) {
        return 0;
    }
    for (i = 0; i < count; i++) {
        has_move = 0;
        if ((team[i].species < 1) || (team[i].species > 151) ||
            (team[i].level < 1) || (team[i].level > 100)) {
            return 0;
        }
        for (j = 0; j < 4; j++) {
            if (team[i].moves[j] > 165) {
                return 0;
            }
            has_move |= team[i].moves[j] != 0;
        }
        if (!has_move) {
            return 0;
        }
        for (j = 0; j < 11; j++) {
            if (team[i].nickname[j] == 0) {
                break;
            }
            if ((unsigned char)team[i].nickname[j] > 127) {
                return 0;
            }
        }
        if (j == 11) {
            return 0;
        }
    }
    return 1;
}

static void rl_to_native(const RlMon* mon, unk_func_80026268_arg0* native, int use_state) {
    int i;
    int ups;
    int base;
    int maximum;

    bzero(native, sizeof(*native));
    func_8002B924(native, mon->species, mon->level);
    if (use_state && (mon->exp >= native->unk_10) &&
        ((mon->level == 100) || (mon->exp < func_800224B8(mon->species, mon->level + 1)))) {
        native->unk_10 = mon->exp;
    }
    native->unk_00.unk_01 = func_80022A38(mon->species);
    native->unk_1E = mon->dvs;
    native->unk_14 = mon->stat_exp[0];
    native->unk_16 = mon->stat_exp[1];
    native->unk_18 = mon->stat_exp[2];
    native->unk_1A = mon->stat_exp[3];
    native->unk_1C = mon->stat_exp[4];
    func_80022338(native);
    native->unk_02 = native->unk_26;
    for (i = 0; i < 4; i++) {
        native->unk_09[i] = mon->moves[i];
        if (mon->moves[i] == 0) {
            native->unk_20[i] = 0;
            continue;
        }
        base = D_80072B00[mon->moves[i] - 1].unk_05;
        maximum = base;
        for (ups = 0; ups < 3; ups++) {
            if (maximum >= mon->max_pp[i]) {
                break;
            }
            maximum += (base >= 40) ? 7 : base / 5;
        }
        native->unk_20[i] = (ups << 6) | (use_state ? MIN(mon->pp[i], maximum) : maximum);
    }
    if (use_state) {
        native->unk_02 = MIN(mon->cur_hp, native->unk_26);
        native->unk_05 = mon->status;
    }
    if (mon->nickname[0] != 0) {
        for (i = 0; (i < 10) && (mon->nickname[i] != 0); i++) {
            native->unk_30[i] = mon->nickname[i];
        }
        native->unk_30[i] = 0;
    }
    func_800228B0(native);
}

static void rl_from_native(const unk_func_80026268_arg0* native, RlMon* mon) {
    int i;
    int base;
    int ups;

    mon->species = native->unk_00.unk_00;
    mon->level = native->unk_24;
    mon->exp = native->unk_10;
    mon->cur_hp = native->unk_02;
    mon->max_hp = native->unk_26;
    mon->status = native->unk_05;
    mon->fainted = native->unk_02 == 0;
    mon->dvs = native->unk_1E;
    mon->stat_exp[0] = native->unk_14;
    mon->stat_exp[1] = native->unk_16;
    mon->stat_exp[2] = native->unk_18;
    mon->stat_exp[3] = native->unk_1A;
    mon->stat_exp[4] = native->unk_1C;
    mon->base_stats[0] = D_80070F84[mon->species].baseHP;
    mon->base_stats[1] = D_80070F84[mon->species].baseSpecial;
    mon->base_stats[2] = D_80070F84[mon->species].baseSpeed;
    mon->base_stats[3] = D_80070F84[mon->species].baseDefense;
    mon->base_stats[4] = D_80070F84[mon->species].baseAttack;
    mon->current_stats[0] = native->unk_26;
    mon->current_stats[1] = native->unk_28;
    mon->current_stats[2] = native->unk_2A;
    mon->current_stats[3] = native->unk_2C;
    mon->current_stats[4] = native->unk_2E;
    for (i = 0; i < 10; i++) {
        if (native->unk_30[i] == 0) {
            break;
        }
        mon->nickname[i] = (native->unk_30[i] < 128) ? native->unk_30[i] : '?';
    }
    mon->nickname[i] = 0;
    for (i = 0; i < 4; i++) {
        mon->moves[i] = native->unk_09[i];
        mon->pp[i] = native->unk_20[i] & 0x3F;
        mon->max_pp[i] = 0;
        if (mon->moves[i] != 0) {
            base = D_80072B00[mon->moves[i] - 1].unk_05;
            ups = native->unk_20[i] >> 6;
            mon->max_pp[i] = base + ups * ((base >= 40) ? 7 : base / 5);
        }
    }
}

int rl_prepare_mon(RlMon* mon, int full_heal) {
    unk_func_80026268_arg0 native;

    if (!rl_valid_team(mon, 1)) {
        osSyncPrintf("rl_prepare_mon: invalid Pokemon\n");
        return -1;
    }
    rl_to_native(mon, &native, !full_heal);
    rl_from_native(&native, mon);
    switch (D_80070F84[mon->species].unk_13) {
        case 0: mon->growth = RL_MEDIUM_FAST; break;
        case 3: mon->growth = RL_MEDIUM_SLOW; break;
        case 4: mon->growth = RL_FAST; break;
        case 5: mon->growth = RL_SLOW; break;
    }
    return 0;
}

int rl_start_battle(const RlMon* party, int party_n, const RlMon* enemy, int enemy_n,
                   const RlBattleRules* rules, int return_state, RlLaunchResult* out) {
    unk_D_800AE540_0004* player;
    unk_D_800AE540_0004* opponent;
    unk_func_80026268_arg0 native;
    RlLaunchResult* result = &gRlLastBattleResult;
    int i;
    int count;

    if (gRlBattleActive || !rl_valid_team(party, party_n) || !rl_valid_team(enemy, enemy_n) ||
        (rules == NULL) || (rules->stadium > 8) || (rules->gym > 9) ||
        (rules->player_count > 6)) {
        osSyncPrintf("rl_start_battle: invalid request\n");
        return -1;
    }
    count = 0;
    for (i = 0; i < party_n; i++) {
        if (!rules->use_state || (!party[i].fainted && (party[i].cur_hp != 0))) {
            count++;
        }
    }
    if (count == 0) {
        osSyncPrintf("rl_start_battle: no usable player Pokemon\n");
        return -1;
    }
    bzero(result, sizeof(*result));
    func_8002B5EC(0xA, rules->stadium, rules->gym);
    player = func_8002B700(rules->player_cpu ? -1 : 0, 0, "1P", func_8002311C(1));
    opponent = func_8002B700(-1, 0, "COM", func_8002311C(3));
    player->unk_018 = rules->ai_level;
    opponent->unk_018 = rules->ai_level;
    func_8002B840(0, player);
    func_8002B840(1, opponent);
    for (i = 0; i < party_n; i++) {
        if (rules->use_state && (party[i].fainted || (party[i].cur_hp == 0))) {
            continue;
        }
        if (rules->player_count && (player->unk_002 >= rules->player_count)) {
            break;
        }
        result->player_index[player->unk_002] = i;
        result->player[player->unk_002] = party[i];
        rl_to_native(&party[i], &native, rules->use_state);
        func_8002B888(player, &native);
    }
    for (i = 0; i < enemy_n; i++) {
        result->enemy[i] = enemy[i];
        rl_to_native(&enemy[i], &native, 0);
        func_8002B888(opponent, &native);
    }
    gRlBattleActive = 1;
    main_pool_push_state(0x524C4254);
    if (rules->show_intro) {
        FRAGMENT_LOAD_AND_CALL(fragment63, 0, 0);
    }
    func_800367A0(0xA0, 0xA, 0);
    FRAGMENT_LOAD(fragment34);
    FRAGMENT_LOAD_AND_CALL(fragment62, 0, (u32)&D_800AE540);
    main_pool_pop_state(0x524C4254);
    result->outcome = RL_LAUNCH_DRAW;
    if (D_800AE540.unk_1194[0].unk_1C != D_800AE540.unk_1194[1].unk_1C) {
        result->outcome = D_800AE540.unk_1194[0].unk_1C ? RL_BATTLE_WIN : RL_BATTLE_LOSS;
    }
    result->player_count = player->unk_002;
    result->enemy_count = opponent->unk_002;
    for (i = 0; i < player->unk_002; i++) {
        rl_from_native(&player->unk_01C[i], &result->player[i]);
    }
    for (i = 0; i < opponent->unk_002; i++) {
        rl_from_native(&opponent->unk_01C[i], &result->enemy[i]);
    }
    if (out != NULL) {
        *out = *result;
    }
    gRlBattleActive = 0;
    gRlBattleCount++;
    gCurrentGameState = return_state;
    return result->outcome;
}

void rl_battle_test_entry(void) {
    RlMon party[3];
    RlMon enemy[3];
    RlBattleRules rules;
    int i;

    bzero(party, sizeof(party));
    bzero(enemy, sizeof(enemy));
    bzero(&rules, sizeof(rules));
    for (i = 0; i < 3; i++) {
        party[i].species = 150;
        party[i].level = 100;
        party[i].dvs = 0xFFFF;
        party[i].moves[0] = 94;
        enemy[i].species = 10 + i;
        enemy[i].level = 5;
        enemy[i].moves[0] = 33;
    }
    rules.stadium = 3;
    rules.player_count = 3;
    rules.player_cpu = 1;
    rules.ai_level = 64;
    for (i = 0; i < 3; i++) {
        rl_prepare_mon(&party[i], 1);
        party[i].cur_hp = 71 + i;
        party[i].pp[0] = 8 + i;
        party[i].status = 0x40;
        party[i].held_relic = i + 1;
    }
    party[0].fainted = 1;
    party[0].cur_hp = 0;
    rules.use_state = 1;
    rules.player_count = 2;
    rl_start_battle(party, 3, enemy, 3, &rules, STATE_TITLE_SCREEN, NULL);
}
#endif
