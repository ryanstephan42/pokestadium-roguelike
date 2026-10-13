/* HOST: drives every canonical campaign party through the real private AI service. */
#define main legacy_selector_main
#define SC_AI_HOST_MOVE_TABLE_EXTERNAL
#include "test_story_ai_selector.c"
#undef main
#include <stdio.h>
#include "decision-predicate.inc"

typedef struct SweepMon {
    unsigned encounter, variant, slot, count, kind, species;
    unsigned moves[4], pp[4], level, stats[5];
} SweepMon;
#include "party-table.inc"

/* Native AI indexes D_80072B00[move - 1] with move 0 when a mon has no damaging move
 * (func_84378B0C leaves *arg2 = 0; func_84380B40 -> func_8437865C -> func_84376C90).
 * Vanilla and candidate ELFs both place six zero bytes before the table (D_80072AFF
 * anchor); the runner verifies that and this layout reproduces it instead of reading
 * an unrelated host global. */
__asm__(".data\n.balign 8\n.globl sweep_move_zero_entry\nsweep_move_zero_entry: .zero 6\n"
        ".globl D_80072B00\nD_80072B00: .zero 990\n.balign 8\n.zero 8\n");
extern u8 sweep_move_zero_entry[6];
f32 D_84390120;
static ScAiDecision decision;
static unsigned checks, selections, switches, struggles, prepared;
#define VERIFY(x) do { checks++; if (!(x)) { \
    printf("sweep failure line %u row %u\n", __LINE__, current); fflush(NULL); abort(); } } while (0)
static unsigned current;

static void load_enemy(const SweepMon *team, unsigned count, unsigned fainted,
                       int empty_pp) {
    unsigned slot, k;
    unk_D_800AE540_0004 *group = &groups[1];
    fixture();
    VERIFY((u8 *)D_80072B00 == sweep_move_zero_entry + 6);
    memset(group->unk_01C, 0, sizeof(group->unk_01C));
    group->unk_002 = count;
    metadata[1].unk_002 = count;
    for (slot = 0; slot < count; slot++) {
        const SweepMon *row = &team[slot];
        unk_func_80026268_arg0 *mon = &group->unk_01C[slot];
        VERIFY(row->slot == slot && row->count == count);
        mon->unk_00.unk_00 = row->species;
        mon->unk_06 = D_80070FA0[row->species - 1].unk_06;
        mon->unk_07 = D_80070FA0[row->species - 1].unk_07;
        mon->unk_24 = row->level;
        mon->unk_26 = row->stats[0];
        mon->unk_02 = slot < fainted ? 0 : row->stats[0];
        mon->unk_28 = row->stats[1]; mon->unk_2A = row->stats[2];
        mon->unk_2C = row->stats[3]; mon->unk_2E = row->stats[4];
        for (k = 0; k < 4; k++) {
            mon->unk_09[k] = row->moves[k];
            mon->unk_20[k] = empty_pp ? 0 : row->pp[k];
        }
    }
    D_843C5550[0] = 4; D_843C5550[1] = team[0].species;
    initialize(1);
    if (fainted) sides[1].unk_654.unk_38.unk_0C = 0;
}

static void check_choice(const SweepMon *team, unsigned count, unsigned fainted,
                         const u8 mapping[6], const ScAiSelection *choice) {
    selections++;
    if (choice->kind == SC_AI_SWITCH) {
        switches++;
        VERIFY(choice->native_slot >= fainted && choice->native_slot < count);
        VERIFY(choice->native_slot != 0);
        VERIFY(groups[1].unk_01C[choice->native_slot].unk_02 != 0);
        VERIFY(choice->compact_slot == mapping[choice->native_slot]);
    } else if (choice->kind == SC_AI_MOVE) {
        VERIFY(!fainted);
        VERIFY(choice->move_slot < 4 && team[0].moves[choice->move_slot] != 0);
    } else {
        VERIFY(choice->kind == SC_AI_STRUGGLE && choice->move_slot == 4);
        struggles++;
    }
}

static void sweep_team(const SweepMon *team) {
    unsigned count = team[0].count, fainted, i;
    u8 identity[6] = {0, 1, 2, 3, 4, 5}, reversed[6] = {0, 1, 2, 3, 4, 5};
    ScAiSelection choice;
    ScAiDecisionToken token;
    for (i = 0; i < count; i++) reversed[i] = count - 1 - i;
    for (fainted = 0; fainted < count; fainted++) {
        const u8 *mapping = fainted & 1 ? reversed : identity;
        load_enemy(team, count, fainted, 0);
        VERIFY(checked_select(1, mapping, count, &choice) == SC_AI_OK);
        check_choice(team, count, fainted, mapping, &choice);
        if (fainted) VERIFY(choice.kind == SC_AI_SWITCH);
    }
    load_enemy(team, count, 0, 1);
    VERIFY(checked_select(1, identity, count, &choice) == SC_AI_OK);
    check_choice(team, count, 0, identity, &choice);
    VERIFY(choice.kind == SC_AI_STRUGGLE || choice.kind == SC_AI_SWITCH);
    /* Whole decision service: prepare/accept for the live party, then shutdown. */
    load_enemy(team, count, 0, 0);
    VERIFY(sc_ai_decision_init(&decision, current + 1, &D_84390120) == SC_AI_OK);
    D_84390120 = 1.0f;
    VERIFY(sc_ai_bank_capture(&before, 1) == SC_AI_OK);
    VERIFY(sc_ai_decision_prepare(&decision, 1, SC_AI_DF60, identity, count,
                                  &token, &choice) == SC_AI_OK);
    VERIFY(choice.kind == SC_AI_MOVE || choice.kind == SC_AI_SWITCH);
    VERIFY(token.party_count == count);
    VERIFY(sc_ai_bank_capture(&after, 1) == SC_AI_OK);
    VERIFY(memcmp(&before, &after, sizeof(before)) == 0);
    D_84390120 = 90.0f;
    VERIFY(sc_ai_decision_accept(&decision, &token,
           choice.kind == SC_AI_SWITCH ? SC_AI_DE20 : SC_AI_DCD4) == SC_AI_OK);
    VERIFY(!decision.committed.lock_owner && sides[1].unk_654.unk_10 == 3);
    if (choice.kind == SC_AI_SWITCH)
        VERIFY(sides[1].unk_654.unk_18 == choice.native_slot && sides[1].unk_654.unk_04 == 2);
    else
        VERIFY(sides[1].unk_654.unk_14 == choice.move_slot && sides[1].unk_654.unk_04 == 1);
    VERIFY(sc_ai_decision_shutdown(&decision, current + 1) == SC_AI_OK);
    prepared++;
}

int main(void) {
    unsigned row = 0, teams = 0, kinds[5] = {0, 0, 0, 0, 0}, counts[7] = {0};
    unsigned species_seen[152] = {0}, distinct = 0, i;
    while (row < SWEEP_ROWS) {
        const SweepMon *team = &sweep_mons[row];
        current = row;
        VERIFY(team->slot == 0 && team->count >= 1 && team->count <= 6);
        VERIFY(row + team->count <= SWEEP_ROWS);
        for (i = 0; i < team->count; i++) {
            VERIFY(team[i].species >= 1 && team[i].species <= 151);
            if (!species_seen[team[i].species]++) distinct++;
        }
        sweep_team(team);
        kinds[team->kind < 5 ? team->kind : 0]++;
        counts[team->count]++;
        teams++;
        row += team->count;
    }
    VERIFY(teams == SWEEP_TEAMS);
    for (i = 0; i < 6; i++) VERIFY(sweep_move_zero_entry[i] == 0);
    printf("story AI party sweep: encounters=%u teams=%u mons=%u distinct_species=%u\n",
           SWEEP_ENCOUNTERS, teams, SWEEP_ROWS, distinct);
    printf("kinds trainer=%u rival=%u static=%u gift=%u ghost=%u\n",
           kinds[0], kinds[1], kinds[2], kinds[3], kinds[4]);
    printf("party sizes 1..6: %u %u %u %u %u %u\n",
           counts[1], counts[2], counts[3], counts[4], counts[5], counts[6]);
    printf("selections=%u switches=%u struggles=%u prepared=%u checks=%u OK\n",
           selections, switches, struggles, prepared, checks);
    return 0;
}
