/* HOST only. Produces legal source checkpoints and a 32-bit layout candidate;
   this layout is not a substitute for binding the final native ELF. */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "tools/rltest/story_six_fixture.h"
#include "src/story/sc_native_campaign.h"
#include "src/story/sc_campaign_battle.h"
#include "src/story/sc_save.h"

#define FIELD(type, member) printf("\"" #member "\":%lu,", (unsigned long)offsetof(type, member))
#define SIZE(type) printf("\"size\":%lu}", (unsigned long)sizeof(type))

static void write_save(const char *directory, const char *name, const ScState *state,
                        unsigned interrupted) {
    char path[1024];
    ScSnapshot snapshot, decoded;
    RlU8 wire[SC_SAVE_SIZE];
    RlU32 generation;
    FILE *file;
    assert(strlen(directory) + strlen(name) + 2 < sizeof(path));
    snapshot.state = *state; snapshot.interrupted = (RlU8)interrupted;
    assert(sc_save_encode(&snapshot, &sc_catalog, 1, wire) == SC_SAVE_OK);
    assert(sc_save_decode(&decoded, &sc_catalog, &generation, wire, sizeof(wire)) == SC_SAVE_OK);
    assert(generation == 1 && decoded.interrupted == interrupted);
    assert(!memcmp(&decoded.state, state, sizeof(*state)));
    sprintf(path, "%s/%s", directory, name);
    file = fopen(path, "wb");
    assert(file && fwrite(wire, 1, sizeof(wire), file) == sizeof(wire));
    assert(!fclose(file));
}

static RlU8 note[SC_SAVE_NOTE_SIZE];
static int note_open(void *context, unsigned company, RlU32 game,
                     const RlU8 name[16], const RlU8 extension[4], unsigned size, int create) {
    (void)context;
    assert(company == SC_SAVE_COMPANY && game == SC_SAVE_GAME && (create == 0 || create == 1));
    assert(size == sizeof(note) && !memcmp(name, sc_save_name, 16) &&
           !memcmp(extension, sc_save_extension, 4));
    return SC_SAVE_OK;
}
static int note_read(void *context, unsigned offset, RlU8 *data, unsigned size) {
    (void)context; assert(offset + size <= sizeof(note));
    memcpy(data, note + offset, size); return SC_SAVE_OK;
}
static int note_write(void *context, unsigned offset, const RlU8 *data, unsigned size) {
    (void)context; assert(offset + size <= sizeof(note));
    memcpy(note + offset, data, size); return SC_SAVE_OK;
}
static void write_note(const char *directory, const char *name, const ScState *state,
                       unsigned interrupted) {
    ScSaveDevice device = {NULL, note_open, note_read, note_write};
    ScSnapshot snapshot, decoded;
    RlU32 generation;
    char path[1024];
    FILE *file;
    assert(strlen(directory) + strlen(name) + 2 < sizeof(path));
    memset(note, 0, sizeof(note));
    snapshot.state = *state; snapshot.interrupted = (RlU8)interrupted;
    assert(sc_storage_store(&device, &sc_catalog, &snapshot, 1) == SC_SAVE_OK);
    assert(sc_save_decode(&decoded, &sc_catalog, &generation, note, SC_SAVE_SIZE) == SC_SAVE_OK);
    assert(generation == 0 && decoded.interrupted == interrupted &&
           !memcmp(&decoded.state, state, sizeof(*state)));
    assert(sc_storage_load(&device, &sc_catalog, &decoded) == SC_SAVE_OK);
    assert(decoded.interrupted == interrupted && !memcmp(&decoded.state, state, sizeof(*state)));
    sprintf(path, "%s/%s", directory, name);
    file = fopen(path, "wb");
    assert(file && fwrite(note, 1, sizeof(note), file) == sizeof(note) && !fclose(file));
}

static void state_proof(const char *name, const ScState *state) {
    unsigned i, j;
    printf("\"%s\":{\"seed\":%lu,\"rng\":%lu,\"party\":[", name,
           (unsigned long)state->seed, (unsigned long)state->rng);
    for (i = 0; i < state->party_count; i++) {
        const RlMon *m = &state->party[i];
        printf("%s{\"species\":%u,\"level\":%u,\"xp\":%lu,\"moves\":[",
               i ? "," : "", m->species, m->level, (unsigned long)m->exp);
        for (j = 0; j < 4; j++) printf("%s%u", j ? "," : "", m->moves[j]);
        printf("]}");
    }
    printf("]}");
}

static void sequential_checkpoint(ScState *state) {
    static const char *route[] = {
        "OaksLab/rival-lab",
        "ViridianForest/TEXT_VIRIDIANFOREST_YOUNGSTER2",
        "ViridianForest/TEXT_VIRIDIANFOREST_YOUNGSTER3",
        "ViridianForest/TEXT_VIRIDIANFOREST_YOUNGSTER4",
        "PewterGym/TEXT_PEWTERGYM_COOLTRAINER_M"
    };
    ScOptions options = {0, SC_STANDARD, SC_STARTER_TRIO, 7};
    unsigned i, j, encounter, heal = SC_NO_ENCOUNTER, variant, slot, count;
    assert(sc_new(state, &sc_catalog, &options, 42) == SC_OK);
    for (i = 0; i < sc_catalog.graph->healing_count; i++)
        if (!strcmp(sc_catalog.graph->healing[i].id, "heal:ViridianPokecenter")) heal = i;
    assert(heal != SC_NO_ENCOUNTER);
    for (i = 0; i < sizeof(route) / sizeof(route[0]); i++) {
        encounter = SC_NO_ENCOUNTER;
        for (j = 0; j < sc_catalog.count; j++)
            if (!strcmp(sc_catalog.encounters[j].id, route[i])) encounter = j;
        assert(encounter != SC_NO_ENCOUNTER);
        variant = sc_catalog.encounters[encounter].kind == SC_RIVAL ? state->rival_branch : 0;
        assert(sc_begin(state, &sc_catalog, encounter, variant) == SC_OK);
        count = sc_catalog.encounters[encounter].teams[variant].count;
        for (slot = 0; slot < count; slot++) {
            assert(sc_on_ko(state, &sc_catalog, slot, 1, variant) == SC_OK);
            for (j = 0; j < state->party_count; j++)
                if (state->learn_mask & (1U << j))
                    assert(sc_learn(state, &sc_catalog, j, 3, 1) == SC_OK);
        }
        assert(sc_finish(state, &sc_catalog, SC_WIN, variant) == SC_OK);
        assert(sc_recruit(state, &sc_catalog, RL_NO_SLOT, 1) == SC_OK);
        assert(sc_location_heal(state, &sc_catalog, heal) == SC_OK);
    }
    assert(state->party_count == 6 && state->party[0].species == 7);
    assert(state->party[0].moves[2] == 145 && sc_moves_packed(&state->party[0]));
}

int main(int argc, char **argv) {
    ScState normal, transform;
    unsigned i, target = SC_NO_ENCOUNTER;
    assert(argc == 2 && sizeof(void *) == 4 && sizeof(unsigned) == 4);
    sequential_checkpoint(&normal);
    assert(sc_six_transform_checkpoint(&transform, &sc_catalog, NULL, NULL) == SC_OK);
    for (i = 0; i < normal.party_count; i++) sc_compact_moves(&normal.party[i]);
    for (i = 0; i < transform.party_count; i++) sc_compact_moves(&transform.party[i]);
    for (i = 0; i < sc_catalog.count; i++)
        if (!strcmp(sc_catalog.encounters[i].id, "PewterGym/TEXT_PEWTERGYM_BROCK")) target = i;
    assert(target != SC_NO_ENCOUNTER && sc_available(&normal, &sc_catalog, target));
    assert(sc_available(&transform, &sc_catalog, target) && transform.party_count == 6);
    assert(transform.party[0].species == 132 && transform.party[0].moves[0] == 144);
    assert(sc_state_valid(&normal, &sc_catalog) && sc_state_valid(&transform, &sc_catalog));
    {
        ScState gapped = normal;
        assert(normal.party[0].moves[2] == 145 && !normal.party[0].moves[3]);
        gapped.party[0].moves[3] = gapped.party[0].moves[2];
        gapped.party[0].pp[3] = gapped.party[0].pp[2];
        gapped.party[0].max_pp[3] = gapped.party[0].max_pp[2];
        gapped.party[0].moves[2] = gapped.party[0].pp[2] = gapped.party[0].max_pp[2] = 0;
        assert(!sc_state_valid(&gapped, &sc_catalog));
        sc_compact_moves(&gapped.party[0]);
        assert(!memcmp(&gapped, &normal, sizeof(normal)));
    }
    write_save(argv[1], "normal-route.sc01", &normal, 0);
    write_save(argv[1], "normal-interrupted.sc01", &normal, 1);
    write_save(argv[1], "transform-route.sc01", &transform, 0);
    write_note(argv[1], "normal-route.note", &normal, 0);
    write_note(argv[1], "normal-interrupted.note", &normal, 1);
    write_note(argv[1], "transform-route.note", &transform, 0);
    printf("{\"schema\":1,\"pointer_size\":4,\"scope\":\"HOST-layout-requires-native-ELF-binding\",");
    printf("\"control\":{");
    FIELD(ScCampaignControl, sequential_request); FIELD(ScCampaignControl, sequential_state);
    FIELD(ScCampaignControl, sequential_ledger); FIELD(ScCampaignControl, sequential_result);
    FIELD(ScCampaignControl, sequential_active); FIELD(ScCampaignControl, sequential_result_ready);
    FIELD(ScCampaignControl, sequential_outcome);
    printf("\"ui_pointer\":%lu,", (unsigned long)(offsetof(ScCampaignControl, lifetime) +
        offsetof(ScNativeLifetime, loop) + offsetof(ScNativeLoop, backing) +
        offsetof(ScNativeCampaignBacking, ui)));
    SIZE(ScCampaignControl);
    printf(",\"ui\":{");
    FIELD(ScUi, state); FIELD(ScUi, postbattle_ledger); FIELD(ScUi, postbattle_combat);
    FIELD(ScUi, screen); FIELD(ScUi, pending_save); FIELD(ScUi, postbattle_active);
    FIELD(ScUi, operation_id); FIELD(ScUi, list); FIELD(ScUi, ids);
    FIELD(ScUi, labels); FIELD(ScUi, operation); SIZE(ScUi);
    printf(",\"list\":{");
    FIELD(RlUiList, count); FIELD(RlUiList, cursor); SIZE(RlUiList);
    printf(",\"state\":{");
    FIELD(ScState, party); FIELD(ScState, party_count); FIELD(ScState, phase);
    FIELD(ScState, encounter); FIELD(ScState, encounter_variant); SIZE(ScState);
    printf(",\"request\":{");
    FIELD(ScUiBattleRequest, operation_id); FIELD(ScUiBattleRequest, encounter);
    FIELD(ScUiBattleRequest, variant); FIELD(ScUiBattleRequest, party_count);
    FIELD(ScUiBattleRequest, enemy_count); SIZE(ScUiBattleRequest);
    printf(",\"ledger\":{");
    FIELD(ScKoLedger, events); FIELD(ScKoLedger, operation_id); FIELD(ScKoLedger, encounter);
    FIELD(ScKoLedger, variant); FIELD(ScKoLedger, party_ids); FIELD(ScKoLedger, progression_ids);
    FIELD(ScKoLedger, count); FIELD(ScKoLedger, next); FIELD(ScKoLedger, enemy_count);
    FIELD(ScKoLedger, party_count); FIELD(ScKoLedger, bound); SIZE(ScKoLedger);
    printf(",\"run\":{");
    FIELD(ScCampaignRun, stage); FIELD(ScCampaignRun, error);
    FIELD(ScCampaignRun, launched); SIZE(ScCampaignRun);
    printf(",\"result\":{");
    FIELD(RlLaunchResult, player); FIELD(RlLaunchResult, player_index);
    FIELD(RlLaunchResult, player_count); FIELD(RlLaunchResult, enemy_count);
    FIELD(RlLaunchResult, outcome); SIZE(RlLaunchResult);
    printf(",\"mon\":{");
    FIELD(RlMon, species); FIELD(RlMon, level); FIELD(RlMon, growth);
    FIELD(RlMon, fainted); FIELD(RlMon, exp); FIELD(RlMon, dvs);
    FIELD(RlMon, stat_exp); FIELD(RlMon, base_stats); FIELD(RlMon, current_stats);
    FIELD(RlMon, moves); FIELD(RlMon, pp); FIELD(RlMon, max_pp);
    FIELD(RlMon, cur_hp); FIELD(RlMon, max_hp); FIELD(RlMon, status);
    FIELD(RlMon, held_relic); FIELD(RlMon, nickname); SIZE(RlMon);
    printf(",\"event\":{");
    FIELD(ScKoEvent, enemy_slot); FIELD(ScKoEvent, participants);
    FIELD(ScKoEvent, recipients); SIZE(ScKoEvent);
    printf(",\"mon_size\":%lu,\"event_size\":%lu,\"target\":{\"index\":%u,"
           "\"id\":\"PewterGym/TEXT_PEWTERGYM_BROCK\",\"variant\":0},\"checkpoints\":{",
           (unsigned long)sizeof(RlMon), (unsigned long)sizeof(ScKoEvent), target);
    state_proof("normal", &normal); printf(",");
    state_proof("transform", &transform); printf("}}\n");
    return 0;
}
