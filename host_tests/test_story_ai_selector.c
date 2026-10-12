#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "src/story/sc_ai_bank.h"
#include "src/story/sc_ai_select.h"
#include "src/fragments/62/fragment62_361050.c"
#include "actual-tables.inc"
#include "native-dependencies.inc"
#include "src/story/sc_ai_overlay.h"

extern void abort(void);
extern int fflush(void *);
static void host_assertion_failed(const char *expression, unsigned line) {
    printf("selector assertion failed at line %u: %s\n", line, expression);
    fflush(NULL);
    abort();
}
#undef assert
#define assert(expression) \
    ((expression) ? (void)0 : host_assertion_failed(#expression, __LINE__))

typedef char HostContextSize[(sizeof(unk_D_843C5568) == 0x59C) ? 1 : -1];
typedef char HostRecordSize[(sizeof(unk_D_843C60AC) == 0x76) ? 1 : -1];
typedef char HostChoiceSize[(sizeof(unk_D_843C60C0) == 0x14) ? 1 : -1];
typedef char HostSpeciesSize[(sizeof(unk_D_80070F84) == 0x17) ? 1 : -1];

unk_D_80070F84 D_80070FA0[151];
#ifdef SC_AI_HOST_MOVE_TABLE_EXTERNAL
extern unk_D_80072B00 D_80072B00[165];
#else
unk_D_80072B00 D_80072B00[165];
#endif
u8 D_8438AC60[0x50], D_8438ACD4[0x1A], D_8438ACF0[0xF9];
unk_D_8438ACB8 D_8438ACB8[13];
static unk_D_84390010 sides[2];
unk_D_84390010 *D_84390010[2] = {&sides[0], &sides[1]};
static unk_D_84384588 aliases[2];
unk_D_84384588 *D_84384588[2] = {&aliases[0], &aliases[1]};
static unk_D_800AE540_1194 registrations[2];
static unk_D_800AE540_0004 groups[2];
static unk_D_800AE540_0874 metadata[2];
static unsigned rng_calls;
static ScAiCapsule capsule;
static ScAiBank before, after, player_before;
#ifdef SC_AI_HOST_RESTORE_FAULTS
unsigned host_restore_fail_on;
#include "disabled-comparison.inc"
#endif
static unsigned char immutable_before[20000], immutable_after[20000];
extern void sc_owned_8437B0CC(unk_D_843C60A4 *, unk_D_843C60A4 *, u8,
                             unk_func_843794CC *, u8);

static unsigned immutable_snapshot(unsigned char *destination) {
    unsigned size = 0;
#define SNAPSHOT(value) do { \
    assert(size + sizeof(value) <= sizeof(immutable_before)); \
    memcpy(destination + size, &(value), sizeof(value)); size += sizeof(value); \
} while (0)
    SNAPSHOT(sides); SNAPSHOT(aliases); SNAPSHOT(groups); SNAPSHOT(metadata);
    SNAPSHOT(registrations); SNAPSHOT(D_843C60F0);
    SNAPSHOT(D_843C5310); SNAPSHOT(D_843C5550);
    SNAPSHOT(D_80070FA0); SNAPSHOT(D_80072B00); SNAPSHOT(D_8438AC60);
    SNAPSHOT(D_8438ACB8); SNAPSHOT(D_8438ACD4); SNAPSHOT(D_8438ACF0);
#undef SNAPSHOT
    return size;
}

static int checked_select(unsigned owner, const u8 mapping[6],
                          unsigned count, ScAiSelection *result) {
    unsigned size = immutable_snapshot(immutable_before);
    int status = sc_ai_select(&capsule, owner, mapping, count, result);
    assert(immutable_snapshot(immutable_after) == size);
    assert(memcmp(immutable_before, immutable_after, size) == 0);
    return status;
}

u32 osGetCount(void) {
    rng_calls++;
    return 0x10;
}

void _bcopy(void *src, void *dst, s32 count) { memmove(dst, src, count); }
void _bzero(void *dst, size_t count) { memset(dst, 0, count); }

static void bind(unsigned owner) {
    D_843C5568[0].unk_000 = owner;
    D_843C60A4 = (void *)&D_843C5568[owner].unk_002;
    D_843C60A8 = (void *)&D_843C5568[1 - owner].unk_002;
    D_843C60AC = &D_843C5568[owner].unk_016[0];
    D_843C60B0 = &D_843C5568[1 - owner].unk_016[0];
    D_843C60B4 = &D_843C60AC->unk_12;
    D_843C60B8 = &D_843C60B0->unk_12;
    D_843C5564 = (void *)((u8 *)&D_843C60F0 + 0x20);
}

static void initialize(unsigned owner) {
    unsigned side, slot;
    memset(D_843C5568, 0, sizeof(D_843C5568));
    func_84378474(&groups[0], NULL, &groups[1], D_843C5568, 0);
    bind(owner);
    D_843C5568[0].unk_001 = 2;
    D_843C60A4->unk_13 = 1;
    for (side = 0; side < 2; side++) {
        unk_D_843C60A4 *team = (void *)&D_843C5568[side].unk_002;
        func_8437E974(&groups[side], NULL, team, 0);
        team->unk_09 = team->unk_06;
        team->unk_07[0] = team->unk_06;
        for (slot = 0; slot < team->unk_06; slot++)
            team->unk_0A[slot] = slot;
        sides[side].unk_654.unk_38 = team->unk_14[0].unk_12;
    }
    D_843C60BC = D_8438AFB0 = D_843C60E8 = 0;
    memset(D_843C60C0, 0, sizeof(D_843C60C0));
}

static void load_policy(unsigned index) {
    unsigned slot;
    const u8 *record = fixture__7820E0 + index * 0x48;
    assert(index < sizeof(fixture__7820E0) / 0x48);
    memcpy(&D_843C60F0, record, sizeof(D_843C60F0));
    for (slot = 0; slot < 3; slot++) {
        const u8 *bytes = record + 0xC + slot * 0x14;
        unk_D_843C60F0_00C *rule = (void *)((u8 *)&D_843C60F0 + 0xC + slot * 0x14);
        rule->unk_00 = ((u32)bytes[0] << 24) | ((u32)bytes[1] << 16) |
            ((u32)bytes[2] << 8) | bytes[3];
    }
}

static void fixture(void) {
    unsigned side, slot, k;
    memset(&capsule, 0, sizeof(capsule));
    memset(sides, 0, sizeof(sides));
    memset(groups, 0, sizeof(groups));
    memset(registrations, 0, sizeof(registrations));
    memset(metadata, 0, sizeof(metadata));
    memset(D_843C5550, 0, sizeof(D_843C5550));
    memset(D_843C5310, 0, sizeof(D_843C5310));
    memcpy(D_80070FA0, fixture_D_80070FA0, sizeof(D_80070FA0));
    memcpy(D_80072B00, fixture_D_80072B00, sizeof(D_80072B00));
    memcpy(D_8438AC60, fixture_D_8438AC60, sizeof(D_8438AC60));
    memcpy(D_8438ACB8, fixture_D_8438ACB8, sizeof(D_8438ACB8));
    memcpy(D_8438ACD4, fixture_D_8438ACD4, sizeof(D_8438ACD4));
    memcpy(D_8438ACF0, fixture_D_8438ACF0, sizeof(D_8438ACF0));
    load_policy(0);
    for (side = 0; side < 2; side++) {
        groups[side].unk_002 = side ? 2 : 6;
        groups[side].unk_214 = &metadata[side];
        metadata[side].unk_002 = groups[side].unk_002;
        registrations[side].unk_01 = 1;
        registrations[side].unk_08[0] = &groups[side];
        sides[side].unk_720 = &registrations[side];
        sides[side].unk_724 = &groups[side];
        for (slot = 0; slot < groups[side].unk_002; slot++) {
            unk_func_80026268_arg0 *mon = &groups[side].unk_01C[slot];
            mon->unk_00.unk_00 = side ? 74 : 4;
            mon->unk_02 = mon->unk_26 = 32;
            mon->unk_24 = 12;
            mon->unk_28 = mon->unk_2A = mon->unk_2C = mon->unk_2E = 20;
            mon->unk_06 = side ? 4 : 20;
            mon->unk_07 = side ? 5 : 20;
            for (k = 0; k < 4; k++) {
                mon->unk_09[k] = k ? 45 : 33;
                mon->unk_20[k] = 20;
            }
        }
    }
    /* Existing preparation filled species lookup; no appended duplicate identities. */
    D_843C5550[0] = 4; D_843C5550[1] = 74;
    initialize(1);
    func_843768D8(0x35);
    capsule.generation = 1;
    capsule.version = 1;
    capsule.size = sizeof(capsule);
}

static void admission_tests(void) {
    u8 mapping[6] = {0, 1, 2, 3, 4, 5};
    ScAiSelection result;
    unsigned mode, owner, slot, old_calls;
    for (mode = 0; mode < 3; mode++) {
        for (owner = 0; owner < 2; owner++) {
            fixture();
            for (slot = 0; slot < 3; slot++) {
                unk_D_843C60F0_00C *rule = (void *)((u8 *)&D_843C60F0 + 0xC + slot * 0x14);
                rule->unk_00 = mode == 1 ? 0x400000 : (mode == 2 ? 4 : 0);
            }
            assert(checked_select(owner, mapping, owner ? 2 : 6, &result) == SC_AI_OK);
            assert(result.kind == SC_AI_MOVE || result.kind == SC_AI_SWITCH);
            sides[owner].unk_654.unk_38.unk_0C = 0;
            groups[owner].unk_01C[0].unk_02 = 0;
            assert(checked_select(owner, mapping, owner ? 2 : 6, &result) == SC_AI_OK);
            assert(result.kind == SC_AI_SWITCH && result.native_slot > 0);
            assert(result.compact_slot == mapping[result.native_slot]);
            assert(groups[owner].unk_01C[result.native_slot].unk_02);
        }
    }
    fixture();
    for (slot = 0; slot < 4; slot++) sides[0].unk_654.unk_38.unk_32[slot] = 0;
    old_calls = rng_calls;
    assert(sc_ai_select(&capsule, 0, mapping, 6, &result) == SC_AI_OK);
    assert(result.kind == SC_AI_STRUGGLE && result.move_slot == 4 && old_calls == rng_calls);
    sides[0].unk_654.unk_38.unk_32[0] = 10;
    sides[0].unk_654.unk_38.unk_52 = 1;
    assert(sc_ai_select(&capsule, 0, mapping, 6, &result) == SC_AI_OK);
    assert(result.kind == SC_AI_STRUGGLE);
    fixture();
    for (owner = 0; owner < 2; owner++) {
        sides[owner].unk_654.unk_38.unk_0C = 0;
        sides[owner].unk_654.unk_2D = 0x12;
        for (slot = 0; slot < groups[owner].unk_002; slot++) groups[owner].unk_01C[slot].unk_02 = 0;
    }
    old_calls = rng_calls;
    assert(sc_ai_select(&capsule, 0, mapping, 6, &result) == SC_AI_OK);
    assert(result.kind == SC_AI_NATIVE_FINISH);
    assert(sc_ai_select(&capsule, 1, mapping, 2, &result) == SC_AI_OK);
    assert(result.kind == SC_AI_NATIVE_FINISH && old_calls == rng_calls);
    sides[0].unk_654.unk_2D = 0x10;
    assert(sc_ai_select(&capsule, 0, mapping, 6, &result) == SC_AI_INVALID);
    assert(result.kind == SC_AI_REFUSED);
    fixture();
    assert(sc_ai_bank_capture(&before, 1) == SC_AI_OK);
    mapping[1] = 0;
    old_calls = rng_calls;
    assert(sc_ai_select(&capsule, 0, mapping, 6, &result) == SC_AI_INVALID);
    assert(sc_ai_bank_capture(&after, 1) == SC_AI_OK);
    assert(memcmp(&before, &after, sizeof(before)) == 0 && old_calls == rng_calls);
    mapping[1] = 1;
    fixture();
    registrations[0].unk_01 = 2;
    assert(sc_ai_select(&capsule, 0, mapping, 6, &result) == SC_AI_INVALID);
    fixture();
    groups[0].unk_002 = 7;
    assert(sc_ai_select(&capsule, 0, mapping, 6, &result) == SC_AI_INVALID);
    fixture();
    assert(sc_ai_transaction_begin(&capsule, 0) == SC_AI_OK);
    metadata[0].unk_002 = 5;
    assert(sc_ai_player_init(&capsule) == SC_AI_INVALID);
    assert(sc_ai_transaction_end(&capsule, 0, 0) == SC_AI_INVALID);
    assert(capsule.lock_owner == 0 && !capsule.player.valid);
}

static void reentry_tests(void) {
    ScAiRequest request;
    static f32 clock = 1.0f;
    unsigned iteration;
    memset(&request, 0, sizeof(request));
    request.party_count = 6;
    request.clock = &clock;
    request.domain = SC_AI_DF60;
    for (iteration = 0; iteration < 6; iteration++) request.mapping[iteration] = iteration;
    for (iteration = 1; iteration <= 3; iteration++) {
        fixture();
        request.generation = iteration;
        assert(sc_ai_entry(SC_AI_INIT, &request) == SC_AI_OK);
        assert(sc_ai_entry(SC_AI_INIT, &request) == SC_AI_INVALID);
        assert(sc_ai_entry(SC_AI_SELECT, &request) == SC_AI_INVALID);
        assert(sc_ai_entry(SC_AI_PREPARE, &request) == SC_AI_OK);
        assert(request.selection.kind == SC_AI_MOVE || request.selection.kind == SC_AI_SWITCH);
        request.generation++;
        assert(sc_ai_entry(SC_AI_ACCEPT, &request) == SC_AI_INVALID);
        assert(sc_ai_entry(SC_AI_SHUTDOWN, &request) == SC_AI_INVALID);
        request.generation--;
        request.route = request.selection.kind == SC_AI_SWITCH ? SC_AI_DE20 : SC_AI_DCD4;
        assert(sc_ai_entry(SC_AI_REJECT, &request) == SC_AI_OK);
        assert(sc_ai_entry(SC_AI_SHUTDOWN, &request) == SC_AI_OK);
        assert(sc_ai_entry(SC_AI_SELECT, &request) == SC_AI_INVALID);
    }
}

#ifdef SC_AI_HOST_RESTORE_FAULTS
    static void restore_failure_tests(void) {
        ScAiRequest request;
        static f32 clock = 1.0f;
        ScAiSelection result;
        u8 mapping[6] = {0, 1, 2, 3, 4, 5};
        unsigned owner, disabled, slot;
        fixture();
        for (disabled = 0; disabled < 256; disabled++) {
            for (slot = 0; slot < 4; slot++) {
                unk_D_800FCB18 *live = &sides[0].unk_654.unk_38;
                memset(live->unk_1F, 0, sizeof(live->unk_1F));
                memset(live->unk_32, 0, sizeof(live->unk_32));
                live->unk_1F[slot] = 1;
                live->unk_32[slot] = 1;
                live->unk_52 = disabled;
                assert(host_candidate_usable(live) == !host_native_d8c4(&sides[0]));
            }
        }
        puts("actual D8C4/extracted candidate predicate: 1024 cases, zero differences");
        for (owner = 0; owner < 2; owner++) {
            fixture();
            assert(sc_ai_transaction_begin(&capsule, owner) == SC_AI_OK);
            capsule.saved_enemy.refs[0] = 0xFFFFFFFEU;
            assert(sc_ai_transaction_end(&capsule, owner, 0) == SC_AI_RESTORE_FAILED);
            assert(capsule.lock_owner == owner + 1);
            assert(sc_ai_transaction_begin(&capsule, 1 - owner) == SC_AI_RESTORE_FAILED);
            assert(sc_ai_transaction_end(&capsule, owner, 1) == SC_AI_RESTORE_FAILED);
            assert(capsule.lock_owner == owner + 1);
        }
        fixture();
        assert(sc_ai_bank_capture(&capsule.player, 1) == SC_AI_OK);
        capsule.player.refs[0] = 0xFFFFFFFEU;
        host_restore_fail_on = 2; /* Real invalid player restore, injected fallback error. */
        assert(sc_ai_transaction_begin(&capsule, 0) == SC_AI_RESTORE_FAILED);
        assert(capsule.lock_owner == 1 && capsule.error == SC_AI_RESTORE_FAILED);
        assert(sc_ai_transaction_begin(&capsule, 1) == SC_AI_RESTORE_FAILED);
        assert(sc_ai_transaction_end(&capsule, 0, 0) == SC_AI_RESTORE_FAILED);
        fixture();
        assert(sc_ai_bank_capture(&capsule.player, 1) == SC_AI_OK);
        capsule.player.refs[0] = 0xFFFFFFFEU;
        assert(sc_ai_transaction_begin(&capsule, 0) == SC_AI_INVALID);
        assert(capsule.lock_owner == 0 && capsule.error == SC_AI_INVALID);
        fixture();
        assert(sc_ai_transaction_begin(&capsule, 0) == SC_AI_OK);
        assert(sc_ai_player_init(&capsule) == SC_AI_OK);
        host_restore_fail_on = 1;
        assert(sc_ai_transaction_end(&capsule, 0, 1) == SC_AI_RESTORE_FAILED);
        assert(capsule.lock_owner == 1);
        fixture();
        assert(sc_ai_transaction_begin(&capsule, 1) == SC_AI_OK);
        host_restore_fail_on = 1;
        assert(sc_ai_transaction_end(&capsule, 1, 1) == SC_AI_RESTORE_FAILED);
        assert(capsule.lock_owner == 2);
        assert(sc_ai_transaction_end(&capsule, 0, 0) == SC_AI_INVALID);
        assert(capsule.lock_owner == 2);
        fixture();
        memset(D_843C5550, 0, sizeof(D_843C5550));
        host_restore_fail_on = 1;
        assert(checked_select(0, mapping, 6, &result) == SC_AI_RESTORE_FAILED);
        assert(capsule.lock_owner == 1 && result.kind == SC_AI_REFUSED);
        fixture();
        host_restore_fail_on = 1;
        assert(checked_select(0, mapping, 6, &result) == SC_AI_RESTORE_FAILED);
        assert(result.kind == SC_AI_REFUSED && capsule.lock_owner == 1);
        fixture();
        memset(&request, 0, sizeof(request));
        request.generation = 1234;
        request.clock = &clock;
        request.domain = SC_AI_DF60;
        request.party_count = 6;
        memcpy(request.mapping, mapping, sizeof(mapping));
        assert(sc_ai_entry(SC_AI_INIT, &request) == SC_AI_OK);
        host_restore_fail_on = 1;
        assert(sc_ai_entry(SC_AI_PREPARE, &request) == SC_AI_RESTORE_FAILED);
        assert(sc_ai_entry(SC_AI_SHUTDOWN, &request) == SC_AI_INVALID);
        assert(sc_ai_entry(SC_AI_INIT, &request) != SC_AI_OK);
        request.owner = 1;
        assert(sc_ai_entry(SC_AI_PREPARE, &request) != SC_AI_OK);
        assert(sc_ai_entry(SC_AI_SHUTDOWN, &request) == SC_AI_INVALID);
        puts("restore faults: real invalid refs plus explicitly injected restore errors; holder/reentry/shutdown retention passed");
    }
    #endif

static void transform_dependency_tests(void) {
    unk_func_843794CC input, original_score, owned_score;
    s32 seed;
    u32 shift;
    unsigned owner;
    for (owner = 0; owner < 2; owner++) {
        fixture();
        initialize(owner);
        bind(owner);
        load_policy(11);
        D_843C60A4->unk_10 = D_843C60A8->unk_10 = 0;
        D_843C60B4->unk_1F[0] = 144;
        D_843C60B4->unk_32[0] = 10;
        D_843C60B8->unk_4E |= 8;
        sides[owner].unk_654.unk_BE = 4;
        sides[1 - owner].unk_654.unk_BE = 132;
        memset(&input, 0, sizeof(input));
        func_843794CC(D_843C60AC, D_843C60B0, 0, 0, 1, &input);
        assert(input.unk_00 == 144 && input.unk_03 == 0x39);
        seed = D_843C6150; shift = D_843C6154;
        original_score = input;
        func_8437B0CC(D_843C60A4, D_843C60A8, 1, &original_score, 0);
        D_843C6150 = seed; D_843C6154 = shift;
        owned_score = input;
        sc_owned_8437B0CC(D_843C60A4, D_843C60A8, 1, &owned_score, 0);
        printf("Transform owner %u: original=%d owned=%d input04=%u weight=%u "
               "transformed=%u opponent_pointer=%u\n", owner, original_score.unk_28,
               owned_score.unk_28, input.unk_04, D_843C5564->unk_10,
               D_843C60B8->unk_4E, &D_843C60A8->unk_14[0].unk_12 == D_843C60B8);
        if (owner)
            assert(memcmp(&original_score, &owned_score, sizeof(owned_score)) == 0);
        else
            assert(original_score.unk_28 != owned_score.unk_28);
    }
}

static void registered_domain_tests(void) {
    const u8 mapping[6] = {5, 4, 3, 2, 1, 0};
    ScAiSelection result;
    unsigned count, active, owner;
    for (count = 1; count <= 6; count++) {
        fixture();
        groups[0].unk_002 = metadata[0].unk_002 = count;
        initialize(1);
        for (active = 0; active < count; active++) {
            sides[0].unk_654.unk_08 = active;
            for (owner = 0; owner < 2; owner++) {
                assert(checked_select(owner, mapping, 6, &result) == SC_AI_OK);
                assert(result.kind == SC_AI_MOVE || result.kind == SC_AI_SWITCH);
                if (result.kind == SC_AI_SWITCH)
                    assert(result.native_slot < groups[owner].unk_002 &&
                           result.native_slot != sides[owner].unk_654.unk_08 &&
                           result.compact_slot == mapping[result.native_slot]);
            }
        }
    }
    fixture();
    sides[0].unk_654.unk_08 = 2;
    sides[0].unk_654.unk_38.unk_0C = groups[0].unk_01C[2].unk_02 = 0;
    assert(checked_select(0, mapping, 6, &result) == SC_AI_OK);
    assert(result.kind == SC_AI_SWITCH && result.native_slot != 2 &&
           result.compact_slot == 5 - result.native_slot);
    fixture();
    metadata[1].unk_002 = 3;
    assert(checked_select(0, mapping, 6, &result) == SC_AI_INVALID);
    fixture();
    assert(checked_select(0, mapping, 6, &result) == SC_AI_OK);
    player_before = capsule.player;
    D_843C60F0.unk_00 ^= 1;
    assert(sc_ai_bank_capture(&before, 1) == SC_AI_OK);
    assert(checked_select(0, mapping, 6, &result) == SC_AI_INVALID);
    assert(sc_ai_bank_capture(&after, 1) == SC_AI_OK);
    assert(memcmp(&before, &after, sizeof(before)) == 0);
    assert(memcmp(&player_before, &capsule.player, sizeof(player_before)) == 0);
}

#ifdef SC_AI_HOST_ACCEPTANCE_TRACE
static void acceptance_boundary_trace(void) {
    ScAiSelection selection;
    u8 mapping[6] = {0, 1, 2, 3, 4, 5};
    unsigned calls;
    fixture();
    assert(!capsule.player.valid && !capsule.lock_owner);
    assert(sc_ai_bank_capture(&before, 1) == SC_AI_OK);
    calls = rng_calls;
    assert(checked_select(0, mapping, 6, &selection) == SC_AI_OK);
    assert(selection.kind == SC_AI_MOVE || selection.kind == SC_AI_SWITCH);
    assert(capsule.player.valid && !capsule.lock_owner && !capsule.error);
    assert(rng_calls > calls);
    assert(sc_ai_bank_capture(&after, 1) == SC_AI_OK);
    assert(memcmp(&before, &after, sizeof(before)) == 0);
    player_before = capsule.player;
    calls = rng_calls;
    /* Caller refusal after SELECT cannot close an already ended transaction. */
    assert(sc_ai_transaction_end(&capsule, 0, 0) == SC_AI_INVALID);
    assert(memcmp(&player_before, &capsule.player, sizeof(player_before)) == 0);
    assert(rng_calls == calls);
    puts("ACCEPTANCE TRACE: SELECT commits player and clears holder before return; late reject cannot undo bank");
}
#endif

int main(void) {
    int choice, prior_choice;
    unsigned calls, k;
    fixture();
    assert(sc_ai_bank_capture(&before, 1) == SC_AI_OK);
    calls = rng_calls;
    prior_choice = func_84383B5C(1);
    assert(prior_choice >= 0 && prior_choice < 4 && rng_calls > calls);
    assert(sc_ai_bank_restore(&before) == SC_AI_OK);
    assert(sc_ai_transaction_begin(&capsule, 0) == SC_AI_OK);
    assert(sc_ai_player_init(&capsule) == SC_AI_OK);
    calls = rng_calls;
    choice = sc_ai_owned_select(0);
    assert(choice >= 0 && choice < 4 && rng_calls > calls);
    assert(sc_ai_transaction_end(&capsule, 0, 1) == SC_AI_OK);
    assert(sc_ai_bank_capture(&after, 1) == SC_AI_OK);
    assert(memcmp(&before, &after, sizeof(before)) == 0);
    player_before = capsule.player;
    assert(sc_ai_transaction_begin(&capsule, 1) == SC_AI_OK);
    choice = func_84383B5C(1);
    assert(choice >= 0 && choice < 4);
    assert(sc_ai_transaction_end(&capsule, 1, 1) == SC_AI_OK);
    assert(memcmp(&player_before, &capsule.player, sizeof(player_before)) == 0);
    assert(sc_ai_bank_capture(&before, 1) == SC_AI_OK);
    sides[0].unk_654.unk_38.unk_0C = 0;
    groups[0].unk_01C[0].unk_02 = 0;
    assert(sc_ai_transaction_begin(&capsule, 0) == SC_AI_OK);
    /* Real native scoring, switching and RNG; no mocked selector return. */
    choice = sc_ai_owned_select(0);
    assert(choice >= 4 && choice < 10);
    assert(sc_ai_transaction_end(&capsule, 0, 1) == SC_AI_OK);
    assert(sc_ai_bank_capture(&after, 1) == SC_AI_OK);
    assert(memcmp(&before, &after, sizeof(before)) == 0);
    player_before = capsule.player;
    assert(sc_ai_transaction_begin(&capsule, 0) == SC_AI_OK);
    assert(sc_ai_transaction_begin(&capsule, 1) == SC_AI_NESTED);
    assert(capsule.lock_owner == 1);
    assert(sc_ai_transaction_end(&capsule, 0, 1) == SC_AI_INVALID);
    assert(memcmp(&player_before, &capsule.player, sizeof(player_before)) == 0);
    assert(sc_ai_bank_capture(&after, 1) == SC_AI_OK);
    assert(memcmp(&before, &after, sizeof(before)) == 0);
    capsule.error = 0;
    for (k = 0; k < 7; k++) {
        after = player_before;
        after.refs[k] = 0xFFFFFFFEU;
        assert(sc_ai_bank_restore(&after) == SC_AI_INVALID);
    }
    after = player_before;
    after.refs[3] = 0x59C + 0x16 + 2 * 0x76;
    after.refs[5] = after.refs[3] + 0x12;
    assert(sc_ai_bank_restore(&after) == SC_AI_INVALID);
    after = player_before;
    after.refs[2] = ~0U;
    assert(sc_ai_bank_restore(&after) == SC_AI_INVALID);
    printf("actual selector/scoring/RNG: healthy both sides, player forced switch %d, interleaving/persistence, nested refusal and seven rebases passed (%u RNG calls)\n",
           choice, rng_calls);
    admission_tests();
    reentry_tests();
    transform_dependency_tests();
    registered_domain_tests();
#ifdef SC_AI_HOST_ACCEPTANCE_TRACE
    acceptance_boundary_trace();
#endif
#ifdef SC_AI_HOST_RESTORE_FAULTS
    restore_failure_tests();
#endif
    puts("authenticated policy weights, immutable table/identity/party snapshots, "
         "typed reentry and actual Transform ownership regression passed");
    puts("real scoring policy400000/prediction4, forced both sides, duplicates, allfainted finish, Struggle/disabledPP, invalid group/count/map and init refusal passed");
    return 0;
}
