#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "tools/rltest/story_six_fixture.h"
#include "src/story/sc_save.h"

static RlU8 note[SC_SAVE_NOTE_SIZE];
static int opened;

static int open_note(void *context, unsigned company, RlU32 game,
                     const RlU8 name[16], const RlU8 extension[4], unsigned size, int create) {
    (void)context;
    if (company != SC_SAVE_COMPANY || game != SC_SAVE_GAME || size != sizeof(note) ||
        memcmp(name, sc_save_name, 16) || memcmp(extension, sc_save_extension, 4))
        return SC_SAVE_INVALID;
    if (create) opened = 1;
    return opened ? SC_SAVE_OK : SC_SAVE_NOT_FOUND;
}

static int read_note(void *context, unsigned offset, RlU8 *data, unsigned size) {
    (void)context;
    if (offset > sizeof(note) || size > sizeof(note) - offset) return SC_SAVE_INVALID;
    memcpy(data, note + offset, size);
    return SC_SAVE_OK;
}

static int write_note(void *context, unsigned offset, const RlU8 *data, unsigned size) {
    (void)context;
    if (offset > sizeof(note) || size > sizeof(note) - offset) return SC_SAVE_INVALID;
    memcpy(note + offset, data, size);
    return SC_SAVE_OK;
}

static void to_be(void *field, unsigned size) {
    RlU32 value = size == 2 ? *(RlU16 *)field : *(RlU32 *)field;
    RlU8 *bytes = field;
    unsigned i;
    for (i = 0; i < size; i++) bytes[size - 1 - i] = (RlU8)(value >> (i * 8));
}

static void write_file(const char *path, const void *bytes, unsigned size) {
    FILE *file = fopen(path, "wb");
    assert(file);
    assert(fwrite(bytes, 1, size, file) == size);
    assert(fclose(file) == 0);
}

static void write_state(const char *path, const ScState *state) {
    ScState encoded = *state;
    unsigned i, j;
    assert(sizeof(encoded) == 648 && sizeof(ScSnapshot) == 652);
    to_be(&encoded.seed, 4); to_be(&encoded.rng, 4);
    to_be(&encoded.milestones, 4); to_be(&encoded.healing_used, 4);
    to_be(&encoded.encounter, 2);
    for (i = 0; i < 7; i++) {
        RlMon *mon = i == 6 ? &encoded.offer : &encoded.party[i];
        to_be(&mon->exp, 4); to_be(&mon->dvs, 2);
        to_be(&mon->cur_hp, 2); to_be(&mon->max_hp, 2);
        for (j = 0; j < 5; j++) {
            to_be(&mon->stat_exp[j], 2); to_be(&mon->current_stats[j], 2);
        }
    }
    write_file(path, &encoded, sizeof(encoded));
}

int main(int argc, char **argv) {
    ScState state;
    ScSnapshot snapshot;
    ScSaveDevice device = {NULL, open_note, read_note, write_note};
    unsigned enemy, slot;
    assert(argc == 4);
    assert(sc_six_checkpoint(&state, &sc_catalog, NULL, NULL) == SC_OK);
    assert(sc_begin(&state, &sc_catalog, 58, 0) == SC_OK);
    for (enemy = 0; enemy < 2; enemy++) {
        assert(sc_on_ko(&state, &sc_catalog, enemy, 1, 0) == SC_OK);
        while (state.learn_mask)
            for (slot = 0; slot < 6; slot++)
                if (state.learn_mask & (1U << slot))
                    assert(sc_learn(&state, &sc_catalog, slot, 3, 0) == SC_OK);
    }
    write_state(argv[3], &state);
    assert(sc_finish(&state, &sc_catalog, SC_WIN, 0) == SC_OK);
    write_state(argv[2], &state);
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.state = state;
    memset(note, 0, sizeof(note));
    assert(sc_storage_store(&device, &sc_catalog, &snapshot, 1) == SC_SAVE_OK);
    write_file(argv[1], note, sizeof(note));
    puts("Pure HOST current codec fixture, not native battle evidence");
    return 0;
}
