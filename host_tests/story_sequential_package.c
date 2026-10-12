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

int main(int argc, char **argv) {
    ScState normal, transform;
    unsigned i, target = SC_NO_ENCOUNTER;
    assert(argc == 2 && sizeof(void *) == 4 && sizeof(unsigned) == 4);
    assert(sc_six_checkpoint(&normal, &sc_catalog, NULL, NULL) == SC_OK);
    assert(sc_six_transform_checkpoint(&transform, &sc_catalog, NULL, NULL) == SC_OK);
    for (i = 0; i < sc_catalog.count; i++)
        if (!strcmp(sc_catalog.encounters[i].id, "PewterGym/TEXT_PEWTERGYM_BROCK")) target = i;
    assert(target != SC_NO_ENCOUNTER && sc_available(&normal, &sc_catalog, target));
    assert(sc_available(&transform, &sc_catalog, target) && transform.party_count == 6);
    assert(transform.party[0].species == 132 && transform.party[0].moves[0] == 144);
    assert(sc_state_valid(&normal, &sc_catalog) && sc_state_valid(&transform, &sc_catalog));
    write_save(argv[1], "normal-route.sc01", &normal, 0);
    write_save(argv[1], "normal-interrupted.sc01", &normal, 1);
    write_save(argv[1], "transform-route.sc01", &transform, 0);
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
    FIELD(ScUi, operation_id); SIZE(ScUi);
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
    printf(",\"mon_size\":%lu,\"event_size\":%lu,\"target\":{\"index\":%u,"
           "\"id\":\"PewterGym/TEXT_PEWTERGYM_BROCK\",\"variant\":0},\"checkpoints\":{",
           (unsigned long)sizeof(RlMon), (unsigned long)sizeof(ScKoEvent), target);
    state_proof("normal", &normal); printf(",");
    state_proof("transform", &transform); printf("}}\n");
    return 0;
}
