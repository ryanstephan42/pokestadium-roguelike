#define main legacy_selector_main
#include "test_story_ai_selector.c"
#undef main
#include <stdlib.h>
#include "decision-predicate.inc"

f32 D_84390120;
static ScAiDecision decision, peer;
static u8 map[6] = {0, 1, 2, 3, 4, 5};
static unsigned checks;
#define VERIFY(x) do { checks++; assert(x); } while (0)

static void start(unsigned battle) {
    fixture();
    VERIFY(sc_ai_decision_init(&decision, battle, &D_84390120) == SC_AI_OK);
    D_84390120 = 1.0f;
}

static unsigned route_for(const ScAiSelection *choice) {
    return choice->kind == SC_AI_SWITCH ? SC_AI_DE20 : SC_AI_DCD4;
}

static void baseline(void) {
    ScAiDecisionToken token, wrong, nested;
    ScAiSelection choice, spare;
    ScAiRequest request;
    unsigned owner, iteration, disabled, slot, old_rng;
    for (disabled = 0; disabled < 256; disabled++) {
        for (slot = 0; slot < 4; slot++) {
            fixture();
            memset(sides[0].unk_654.unk_38.unk_1F, 0, 4);
            memset(sides[0].unk_654.unk_38.unk_32, 0, 4);
            sides[0].unk_654.unk_38.unk_1F[slot] = 1;
            sides[0].unk_654.unk_38.unk_32[slot] = 1;
            sides[0].unk_654.unk_38.unk_52 = disabled;
            VERIFY(host_candidate_usable(&sides[0].unk_654.unk_38) ==
                   !host_native_d8c4(&sides[0]));
            VERIFY(!!host_decision_usable(&sides[0].unk_654.unk_38) ==
                   !host_native_d8c4(&sides[0]));
        }
    }
    for (iteration = 1; iteration <= 3; iteration++) {
        start(iteration);
        VERIFY(sc_ai_decision_init(&peer, iteration + 100, &D_84390120) == SC_AI_OK);
        VERIFY(sc_ai_decision_prepare(&decision, 2, SC_AI_DF60, map, 6,
                                      &token, &choice) == SC_AI_INVALID);
        VERIFY(!token.operation);
        for (owner = 0; owner < 2; owner++) {
            fixture();
            D_84390120 = 1.0f;
            VERIFY(sc_ai_bank_capture(&before, 1) == SC_AI_OK);
            player_before = decision.committed.player;
            old_rng = rng_calls;
            VERIFY(sc_ai_decision_prepare(&decision, owner, SC_AI_DF60, map, 6,
                                          &token, &choice) == SC_AI_OK);
            VERIFY(choice.kind == SC_AI_MOVE || choice.kind == SC_AI_SWITCH);
            VERIFY(decision.phase == SC_AI_DECISION_PENDING);
            VERIFY(decision.committed.lock_owner == owner + 1);
            VERIFY(memcmp(&player_before, &decision.committed.player, sizeof(player_before)) == 0);
            VERIFY(sc_ai_bank_capture(&after, 1) == SC_AI_OK);
            VERIFY(memcmp(&before, &after, sizeof(before)) == 0);
            VERIFY(rng_calls > old_rng);
            old_rng = rng_calls;
            VERIFY(sc_ai_decision_prepare(&peer, owner, SC_AI_DF60, map, 6,
                                          &nested, &spare) == SC_AI_NESTED);
            VERIFY(sc_ai_decision_init(&peer, iteration + 200, &D_84390120) == SC_AI_NESTED);
            VERIFY(sc_ai_decision_shutdown(&decision, iteration) == SC_AI_INVALID);
            wrong = token; wrong.owner = 1 - owner;
            VERIFY(sc_ai_decision_accept(&decision, &wrong, route_for(&choice)) == SC_AI_INVALID);
            wrong = token; wrong.operation++;
            VERIFY(sc_ai_decision_reject(&decision, &wrong, route_for(&choice)) == SC_AI_INVALID);
            wrong = token; wrong.battle++;
            VERIFY(sc_ai_decision_accept(&decision, &wrong, route_for(&choice)) == SC_AI_INVALID);
            wrong = token; wrong.domain = SC_AI_D9EC;
            VERIFY(sc_ai_decision_accept(&decision, &wrong, route_for(&choice)) == SC_AI_INVALID);
            wrong = token; wrong.mapping[0] = 5;
            VERIFY(sc_ai_decision_accept(&decision, &wrong, route_for(&choice)) == SC_AI_INVALID);
            VERIFY(sc_ai_decision_reject(&decision, &token, SC_AI_D9EC_STORE) == SC_AI_INVALID);
            VERIFY(sc_ai_decision_accept(&decision, &token, route_for(&choice)) == SC_AI_INVALID);
            VERIFY(sc_ai_decision_reject(&decision, &token, route_for(&choice)) == SC_AI_OK);
            VERIFY(!decision.committed.lock_owner && decision.phase == SC_AI_DECISION_IDLE);
            VERIFY(memcmp(&player_before, &decision.committed.player, sizeof(player_before)) == 0);
            VERIFY(rng_calls == old_rng);
            VERIFY(sc_ai_decision_accept(&decision, &token, route_for(&choice)) == SC_AI_INVALID);
            VERIFY(sc_ai_decision_reject(&decision, &token, route_for(&choice)) == SC_AI_INVALID);
            D_84390120 = 1.0f;
            VERIFY(sc_ai_decision_prepare(&decision, owner, SC_AI_DF60, map, 6,
                                          &token, &choice) == SC_AI_OK);
            D_84390120 = 90.0f;
            {
                unk_D_84390010_654 original = sides[owner].unk_654;
                unsigned active = sides[owner].unk_654.unk_08;
                sides[owner].unk_654.unk_08 = 1;
                VERIFY(sc_ai_decision_accept(&decision, &token, route_for(&choice)) ==
                       SC_AI_INVALID);
                VERIFY(sides[owner].unk_654.unk_14 == original.unk_14 &&
                       sides[owner].unk_654.unk_18 == original.unk_18);
                sides[owner].unk_654.unk_08 = active;
                sides[owner].unk_654.unk_38.unk_52 ^= 0x10;
                VERIFY(sc_ai_decision_accept(&decision, &token, route_for(&choice)) ==
                       SC_AI_INVALID);
                sides[owner].unk_654.unk_38.unk_52 ^= 0x10;
            }
            VERIFY(sc_ai_decision_accept(&decision, &token, route_for(&choice)) == SC_AI_OK);
            VERIFY(!decision.committed.lock_owner);
            if (owner == 0) VERIFY(decision.committed.player.valid);
            VERIFY(sc_ai_decision_accept(&decision, &token, route_for(&choice)) == SC_AI_INVALID);
        }
        VERIFY(sc_ai_decision_shutdown(&peer, iteration + 100) == SC_AI_OK);
        VERIFY(sc_ai_decision_shutdown(&decision, iteration) == SC_AI_OK);
        VERIFY(sc_ai_decision_accept(&decision, &token, SC_AI_DCD4) == SC_AI_INVALID);
    }
    /* Exact D9EC preparation boundary: native state10/frame20, not generic hook. */
    for (owner = 0; owner < 2; owner++) {
        start(50 + owner);
        sides[owner].unk_654.unk_38.unk_0C = 0;
        groups[owner].unk_01C[0].unk_02 = 0;
        sides[owner].unk_654.unk_2D = 0x10;
        D_84390120 = 20.0f;
        sides[owner].unk_654.unk_10 = 3;
        VERIFY(sc_ai_decision_prepare(&decision, owner, SC_AI_D9EC, map, 6,
                                      &token, &choice) == SC_AI_INVALID);
        sides[owner].unk_654.unk_10 = 0;
        VERIFY(sc_ai_decision_prepare(&decision, owner, SC_AI_D9EC, map, 6,
                                      &token, &choice) == SC_AI_OK);
        VERIFY(choice.kind == SC_AI_SWITCH);
        VERIFY(sc_ai_decision_accept(&decision, &token, SC_AI_DE20) == SC_AI_INVALID);
        VERIFY(sc_ai_decision_accept(&decision, &token, SC_AI_D9EC_STORE) == SC_AI_OK);
        VERIFY(sides[owner].unk_654.unk_18 == choice.native_slot);
        VERIFY(choice.native_slot != 0 && groups[owner].unk_01C[choice.native_slot].unk_02);
        VERIFY(sc_ai_decision_shutdown(&decision, 50 + owner) == SC_AI_OK);
    }
    for (owner = 0; owner < 2; owner++) {
        for (iteration = 0; iteration < 2; iteration++) {
            start(80 + owner * 2 + iteration);
            memset(sides[owner].unk_654.unk_38.unk_32, 0, 4);
            VERIFY(sc_ai_decision_prepare(&decision, owner, SC_AI_DF60, map, 6,
                                          &token, &choice) == SC_AI_OK);
            VERIFY(choice.kind == SC_AI_STRUGGLE && choice.move_slot == 4);
            D_84390120 = 90.0f;
            VERIFY(sc_ai_decision_accept(&decision, &token, SC_AI_DE20) == SC_AI_INVALID);
            VERIFY(sc_ai_decision_accept(&decision, &token, SC_AI_DCD4) == SC_AI_OK);
            VERIFY(sides[owner].unk_654.unk_14 == 4 && sides[owner].unk_654.unk_0C == 4);
            VERIFY(decision.committed.lock_owner == owner + 1);
            VERIFY(sc_ai_decision_shutdown(&decision, token.battle) == SC_AI_INVALID);
            wrong = token; wrong.owner = 1 - owner;
            VERIFY(sc_ai_decision_struggle(&decision, &wrong) == SC_AI_INVALID);
            sides[owner].unk_654.unk_38.unk_32[0] = 1;
            VERIFY(sc_ai_decision_struggle(&decision, &token) == SC_AI_INVALID);
            sides[owner].unk_654.unk_38.unk_32[0] = 0;
            if (iteration) sides[owner].unk_654.unk_38.unk_4C |= 0x20;
            VERIFY(sc_ai_decision_struggle(&decision, &token) == SC_AI_OK);
            if (iteration) VERIFY(sides[owner].unk_654.unk_0C == 4);
            else VERIFY(sides[owner].unk_654.unk_0C == 0 &&
                        sides[owner].unk_654.unk_38.unk_5A == 0xA5);
            VERIFY(sc_ai_decision_struggle(&decision, &token) == SC_AI_INVALID);
            VERIFY(sc_ai_decision_shutdown(&decision, token.battle) == SC_AI_OK);
        }
    }
    /* A forced Bide turn rejects its staged choice without changing native state. */
    start(92);
    sides[1].unk_654.unk_38.unk_4C = 1;
    VERIFY(sc_ai_bank_capture(&before, 1) == SC_AI_OK);
    VERIFY(sc_ai_decision_prepare(&decision, 1, SC_AI_DF60, map, 6,
                                  &token, &choice) == SC_AI_OK);
    D_84390120 = 30.0f;
    {
        unk_D_84390010_654 actor = sides[1].unk_654;
        VERIFY(sc_ai_decision_reject(&decision, &token, route_for(&choice)) == SC_AI_OK);
        VERIFY(memcmp(&actor, &sides[1].unk_654, sizeof(actor)) == 0);
    }
    VERIFY(sc_ai_bank_capture(&after, 1) == SC_AI_OK);
    VERIFY(memcmp(&before, &after, sizeof(before)) == 0);
    VERIFY(decision.phase == SC_AI_DECISION_IDLE && !decision.committed.lock_owner);
    VERIFY(sc_ai_decision_reject(&decision, &token, route_for(&choice)) == SC_AI_INVALID);
    VERIFY(sc_ai_decision_shutdown(&decision, 92) == SC_AI_OK);
    fixture();
    memset(&request, 0, sizeof(request));
    request.generation = 999; request.party_count = 6; request.domain = SC_AI_DF60;
    request.clock = &D_84390120;
    memcpy(request.mapping, map, 6);
    D_84390120 = 1;
    VERIFY(sc_ai_entry(SC_AI_INIT, &request) == SC_AI_OK);
    VERIFY(sc_ai_entry(SC_AI_SELECT, &request) == SC_AI_INVALID);
    VERIFY(sc_ai_entry(SC_AI_PREPARE, &request) == SC_AI_OK);
    request.route = route_for(&request.selection);
    VERIFY(sc_ai_entry(SC_AI_SHUTDOWN, &request) == SC_AI_INVALID);
    VERIFY(sc_ai_entry(SC_AI_REJECT, &request) == SC_AI_OK);
    VERIFY(sc_ai_entry(SC_AI_SHUTDOWN, &request) == SC_AI_OK);
}

static void fault(unsigned mode) {
    ScAiDecisionToken token, other;
    ScAiSelection choice, spare;
    unsigned owner = mode == 4 || (mode >= 10 && mode <= 13) || mode == 15 ? 1 : 0;
    start(700 + mode);
    VERIFY(sc_ai_decision_init(&peer, 800, &D_84390120) == SC_AI_OK);
    if (mode >= 14) {
        ScAiBank changed;
        unk_D_84390010_654 live_before;
        unsigned calls;
        memset(sides[owner].unk_654.unk_38.unk_32, 0, 4);
        VERIFY(sc_ai_decision_prepare(&decision, owner, SC_AI_DF60, map, 6,
                                      &token, &choice) == SC_AI_OK);
        VERIFY(choice.kind == SC_AI_STRUGGLE);
        D_84390120 = 90;
        VERIFY(sc_ai_decision_accept(&decision, &token, SC_AI_DCD4) == SC_AI_OK);
        D_843C60BC ^= 0x1234;
        VERIFY(sc_ai_bank_capture(&changed, 1) == SC_AI_OK);
        live_before = sides[owner].unk_654;
        calls = rng_calls;
        VERIFY(sc_ai_decision_struggle(&decision, &token) == SC_AI_RESTORE_FAILED);
        VERIFY(sc_ai_bank_capture(&after, 1) == SC_AI_OK);
        VERIFY(memcmp(&changed, &after, sizeof(changed)) == 0);
        VERIFY(memcmp(&live_before, &sides[owner].unk_654, sizeof(live_before)) == 0);
        VERIFY(rng_calls == calls);
        VERIFY(decision.phase == SC_AI_DECISION_FAILED &&
               decision.committed.lock_owner == owner + 1);
        VERIFY(sc_ai_decision_struggle(&decision, &token) == SC_AI_RESTORE_FAILED);
        VERIFY(sc_ai_decision_shutdown(&decision, 700 + mode) == SC_AI_INVALID);
        VERIFY(sc_ai_decision_prepare(&peer, 0, SC_AI_DF60, map, 6,
                                      &other, &spare) == SC_AI_NESTED);
        return;
    }
    if (mode == 1) host_restore_fail_on = 1;
    if (mode == 2) host_restore_fail_on = 2;
    if (mode == 1 || mode == 2) {
        VERIFY(sc_ai_decision_prepare(&decision, owner, SC_AI_DF60, map, 6,
                                      &token, &choice) == SC_AI_RESTORE_FAILED);
    } else {
        VERIFY(sc_ai_decision_prepare(&decision, owner, SC_AI_DF60, map, 6,
                                      &token, &choice) == SC_AI_OK);
        if (mode >= 6) {
            ScAiBank changed;
            unk_D_84390010_654 live_before;
            unsigned calls;
            unsigned mutation = mode >= 10 ? mode - 4 : mode;
            if (mutation == 6 || mutation == 7) D_843C60BC ^= 0x1234;
            if (mutation == 6 || mutation == 8) sides[owner].unk_654.unk_08 = 1;
            if (mutation == 6 || mutation == 9) sides[owner].unk_654.unk_38.unk_32[0]--;
            VERIFY(sc_ai_bank_capture(&changed, 1) == SC_AI_OK);
            live_before = sides[owner].unk_654;
            calls = rng_calls;
            D_84390120 = 90;
            other = token; other.operation++;
            VERIFY(sc_ai_decision_reject(&decision, &other, route_for(&choice)) ==
                   SC_AI_INVALID);
            other = token; other.owner = 1 - owner;
            VERIFY(sc_ai_decision_reject(&decision, &other, route_for(&choice)) ==
                   SC_AI_INVALID);
            VERIFY(sc_ai_decision_accept(&decision, &token, route_for(&choice)) ==
                   SC_AI_INVALID);
            VERIFY(sc_ai_decision_reject(&decision, &token, route_for(&choice)) ==
                   SC_AI_RESTORE_FAILED);
            VERIFY(sc_ai_bank_capture(&after, 1) == SC_AI_OK);
            VERIFY(memcmp(&changed, &after, sizeof(changed)) == 0);
            VERIFY(memcmp(&live_before, &sides[owner].unk_654, sizeof(live_before)) == 0);
            VERIFY(rng_calls == calls);
            VERIFY(sc_ai_decision_reject(&decision, &token, route_for(&choice)) ==
                   SC_AI_RESTORE_FAILED);
        } else if (mode == 5) {
            D_843C60F0.unk_00 ^= 1;
            D_84390120 = 90;
            VERIFY(sc_ai_decision_accept(&decision, &token, route_for(&choice)) ==
                   SC_AI_INVALID);
            VERIFY(sc_ai_decision_reject(&decision, &token, route_for(&choice)) ==
                   SC_AI_RESTORE_FAILED);
        } else if (mode == 3) {
            decision.original_enemy.refs[0] = ~0U - 1;
            VERIFY(sc_ai_decision_reject(&decision, &token, route_for(&choice)) ==
                   SC_AI_RESTORE_FAILED);
        } else {
            D_84390120 = 90;
            host_restore_fail_on = 1;
            VERIFY(sc_ai_decision_accept(&decision, &token, route_for(&choice)) ==
                   SC_AI_RESTORE_FAILED);
        }
    }
    VERIFY(decision.phase == SC_AI_DECISION_FAILED &&
           decision.committed.lock_owner == owner + 1);
    VERIFY(sc_ai_decision_shutdown(&decision, 700 + mode) == SC_AI_INVALID);
    VERIFY(sc_ai_decision_prepare(&peer, 0, SC_AI_DF60, map, 6,
                                  &other, &spare) == SC_AI_NESTED);
    VERIFY(sc_ai_decision_shutdown(&peer, 800) == SC_AI_INVALID);
}

int main(int argc, char **argv) {
    unsigned mode = argc == 2 ? (unsigned)atoi(argv[1]) : 0;
    if (mode) fault(mode);
    else {
        admission_tests();
        transform_dependency_tests();
        registered_domain_tests();
        baseline();
    }
    printf("HOST TWO PHASE PASSED: mode %u, %u assertions\n", mode, checks);
    return 0;
}
