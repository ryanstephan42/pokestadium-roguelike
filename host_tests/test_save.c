#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "rl_pak.h"
#include "rl_meta.h"

typedef struct MemoryDevice {
    RlU8 bytes[RL_STORAGE_NOTE_SIZE];
    int exists, budget, writes, reads, fail_read;
    RlSaveStatus open_error;
} MemoryDevice;

static RlSaveStatus mem_open(void *p, int create) {
    MemoryDevice *m = p;
    if (m->open_error) return m->open_error;
    if (!m->exists && !create) return RL_SAVE_NOT_FOUND;
    if (!m->exists) {
        memset(m->bytes, 0, sizeof(m->bytes));
        m->exists = 1;
    }
    return RL_SAVE_OK;
}

static RlSaveStatus mem_read(void *p, unsigned offset, RlU8 *data, unsigned n) {
    MemoryDevice *m = p;
    assert(!(offset % 32) && !(n % 32) && offset + n <= sizeof(m->bytes));
    if (m->fail_read && ++m->reads == m->fail_read) return RL_SAVE_IO;
    memcpy(data, m->bytes + offset, n);
    return RL_SAVE_OK;
}

static RlSaveStatus mem_write(void *p, unsigned offset, const RlU8 *data, unsigned n) {
    MemoryDevice *m = p;
    unsigned i;
    assert(!(offset % 32) && !(n % 32) && offset + n <= sizeof(m->bytes));
    m->writes++;
    for (i = 0; i < n; i++) {
        if (m->budget == 0) return RL_SAVE_IO;
        if (m->budget > 0) m->budget--;
        m->bytes[offset + i] = data[i];
    }
    return RL_SAVE_OK;
}

static void le32(RlU8 *p, RlU32 n) {
    unsigned i;
    for (i = 0; i < 4; i++) p[i] = (RlU8)(n >> (i * 8));
}

static unsigned make_v3_fixture(const RlU8 *v4, unsigned v4_length,
                                int suspended, RlU8 *v3) {
    unsigned old_length, prefix, run_offset, run_length;
    if (suspended) {
        if (v4_length != 972) return 0;
        old_length = 917;
        run_offset = 412;
        run_length = 556;
    } else {
        if (v4_length != 416) return 0;
        old_length = 361;
        run_offset = v4_length;
        run_length = 0;
    }
    prefix = 357;
    memcpy(v3, v4, prefix);
    if (run_length) memcpy(v3 + prefix, v4 + run_offset, run_length);
    v3[4] = RL_SAVE_LEGACY_VERSION;
    v3[5] = 0;
    le32(v3 + old_length - 4, rl_save_crc32(v3, old_length - 4));
    return old_length;
}

static void pack_storage_slot(MemoryDevice *memory, const RlU8 *payload,
                              unsigned length) {
    memset(memory, 0, sizeof(*memory));
    memory->exists = 1;
    memory->budget = -1;
    le32(memory->bytes, 0x4b504c52U);
    le32(memory->bytes + 4, 1);
    le32(memory->bytes + 8, 4);
    le32(memory->bytes + 12, length);
    le32(memory->bytes + 16, rl_save_crc32(payload, length));
    le32(memory->bytes + 28, rl_save_crc32(memory->bytes, 28));
    memcpy(memory->bytes + RL_STORAGE_HEADER_SIZE, payload, length);
}

static void test_v3_migration(void) {
    RlMeta meta, loaded;
    RunState run, restored, reloaded_run;
    RlU8 v4[RL_SAVE_MAX_SIZE], v3[RL_SAVE_MAX_SIZE], has_run, loaded_has_run;
    MemoryDevice memory;
    RlSaveDevice device;
    unsigned v4_length, v3_length, i;

    memset(&meta, 0, sizeof(meta));
    meta.unlocked_pools = 0x15;
    meta.unlocked_relics = 0x13579;
    meta.unlocked_events = 0x2468;
    meta.runs = 17;
    meta.wins = 6;
    meta.best_floor = 19;
    meta.ascension = 4;
    meta.hall_count = 1;
    meta.hall[0] = rl_placeholder_pool.mons[2];
    assert(rl_save_encode(&meta, NULL, v4, sizeof(v4), &v4_length) == RL_OK);
    assert(v4_length == 416);
    v3_length = make_v3_fixture(v4, v4_length, 0, v3);
    assert(v3_length == 361);
    assert(rl_save_decode(&loaded, NULL, &has_run, v3, v3_length) == RL_OK);
    assert(!has_run && loaded.unlocked_pools == 0x15);
    assert(loaded.unlocked_relics == 0x13579);
    assert(loaded.unlocked_events == 0x2468);
    assert(loaded.runs == 17 && loaded.wins == 6);
    assert(loaded.best_floor == 19 && loaded.ascension == 4);
    assert(loaded.hall_count == 1 && loaded.hall[0].species == 7);
    assert(loaded.currency == 0 && loaded.options == 0);

    rl_run_new(&run, 0xdeadbeefU, 2);
    run.phase = RL_MAP;
    run.party_count = 1;
    run.party[0] = rl_placeholder_pool.mons[2];
    run.party[0].cur_hp--;
    run.party[0].pp[0]--;
    run.party[0].status = 3;
    run.money = 731;
    assert(rl_save_encode(&meta, &run, v4, sizeof(v4), &v4_length) == RL_OK);
    assert(v4_length == 972);
    assert(((v4_length + 31) & ~31U) <= RL_STORAGE_SLOT_SIZE - 32);
    v3_length = make_v3_fixture(v4, v4_length, 1, v3);
    assert(v3_length == 917);
    assert(rl_save_decode(&loaded, &restored, &has_run, v3, v3_length) == RL_OK);
    assert(has_run);
    assert(loaded.unlocked_pools == 0x15);
    assert(loaded.unlocked_relics == 0x13579);
    assert(loaded.unlocked_events == 0x2468);
    assert(loaded.runs == 17 && loaded.wins == 6);
    assert(loaded.best_floor == 19 && loaded.ascension == 4);
    assert(loaded.hall_count == 1 && loaded.hall[0].species == 7);
    assert(loaded.currency == 0 && loaded.options == 0);
    assert(loaded.last_registration_run == 0);
    assert(loaded.pending_registration_count == 0);
    assert(loaded.last_registered_species == 0);
    for (i = 0; i < RL_META_SPECIES_BYTES; i++)
        assert(!loaded.registered_species[i] && !loaded.pending_species[i]);
    assert(restored.seed == run.seed && restored.rng == run.rng);
    assert(restored.act == run.act && restored.row == run.row);
    assert(restored.lane == run.lane && restored.phase == run.phase);
    assert(restored.ascension == run.ascension);
    assert(restored.party_count == run.party_count);
    assert(!memcmp(&restored.map, &run.map, sizeof(run.map)));
    assert(restored.party[0].species == run.party[0].species);
    assert(restored.party[0].cur_hp == run.party[0].cur_hp);
    assert(restored.party[0].max_hp == run.party[0].max_hp);
    assert(restored.party[0].pp[0] == run.party[0].pp[0]);
    assert(restored.party[0].max_pp[0] == run.party[0].max_pp[0]);
    assert(restored.party[0].status == run.party[0].status);
    assert(restored.money == run.money);

    assert(rl_save_encode(&loaded, &restored, v4, sizeof(v4), &v4_length) == RL_OK);
    assert(v4_length == 972);
    assert(rl_save_decode(&meta, &reloaded_run, &loaded_has_run,
                          v4, v4_length) == RL_OK);
    assert(loaded_has_run && reloaded_run.seed == run.seed);
    assert(reloaded_run.rng == run.rng);
    assert(reloaded_run.party[0].cur_hp == run.party[0].cur_hp);
    assert(reloaded_run.party[0].pp[0] == run.party[0].pp[0]);
    assert(reloaded_run.party[0].status == run.party[0].status);

    pack_storage_slot(&memory, v3, v3_length);
    device.context = &memory;
    device.open = mem_open;
    device.read = mem_read;
    device.write = mem_write;
    assert(rl_storage_load(&device, &loaded, &restored, &loaded_has_run) ==
           RL_SAVE_OK);
    assert(loaded_has_run && loaded.runs == 17 && restored.seed == run.seed);
    assert(rl_storage_save(&device, &loaded, &restored, 0) == RL_SAVE_OK);
    assert(rl_storage_load(&device, &meta, &reloaded_run, &loaded_has_run) ==
           RL_SAVE_OK);
    assert(loaded_has_run && meta.unlocked_events == 0x2468);
    assert(reloaded_run.seed == run.seed && reloaded_run.rng == run.rng);
    assert(reloaded_run.party[0].cur_hp == run.party[0].cur_hp);
}

static void test_terminal_registration_cold_reload(void) {
    MemoryDevice memory;
    RlSaveDevice device;
    RlMeta meta, loaded;
    RunState run;
    RlU8 has_run;

    memset(&memory, 0, sizeof(memory));
    memory.budget = -1;
    device.context = &memory;
    device.open = mem_open;
    device.read = mem_read;
    device.write = mem_write;
    rl_meta_init(&meta);
    rl_run_new(&run, 0x12345678U, 0);
    run.act = 2;
    rl_map_generate(&run.map, &run.rng, 2, 0);
    run.row = 6;
    run.lane = 0;
    run.phase = RL_LOST;
    run.party_count = 1;
    run.party[0] = rl_placeholder_pool.mons[2];
    run.party[0].species = 8;
    run.stats.battles = 4;
    run.stats.wins = 3;
    assert(rl_meta_record_run(&meta, &run) == RL_OK);
    assert(meta.currency == 35 && meta.pending_registration_count == 1);
    assert(rl_storage_save(&device, &meta, NULL, 1) == RL_SAVE_OK);

    memset(&loaded, 0x55, sizeof(loaded));
    assert(rl_storage_load(&device, &loaded, NULL, &has_run) == RL_SAVE_OK);
    assert(!has_run && loaded.currency == 35);
    assert(loaded.pending_registration_count == 1);
    assert(loaded.pending_species[(8 - 1) >> 3] &
           (1U << ((8 - 1) & 7)));

    rl_run_new(&run, 0x87654321U, 0);
    run.act = 2;
    rl_map_generate(&run.map, &run.rng, 2, 0);
    run.row = 6;
    run.lane = 0;
    run.phase = RL_LOST;
    run.party_count = 1;
    run.party[0] = rl_placeholder_pool.mons[2];
    run.party[0].species = 9;
    run.stats.battles = 2;
    run.stats.wins = 1;
    assert(rl_meta_record_run(&loaded, &run) == RL_OK);
    assert(loaded.currency == 50 && loaded.runs == 2);
    assert(loaded.pending_registration_count == 2);
    assert(rl_storage_save(&device, &loaded, NULL, 0) == RL_SAVE_OK);

    memset(&meta, 0, sizeof(meta));
    assert(rl_storage_load(&device, &meta, NULL, &has_run) == RL_SAVE_OK);
    assert(!has_run && meta.currency == 50 && meta.runs == 2);
    assert(meta.pending_registration_count == 2);
    assert(rl_meta_register_species(&meta, 8) == RL_OK);
    assert(meta.pending_registration_count == 1);
    memory.fail_read = 6;
    memory.reads = 0;
    assert(rl_storage_save(&device, &meta, NULL, 0) == RL_SAVE_IO);
    memory.fail_read = 0;

    memset(&loaded, 0, sizeof(loaded));
    assert(rl_storage_load(&device, &loaded, NULL, &has_run) == RL_SAVE_OK);
    assert(!has_run && loaded.currency == 50 && loaded.runs == 2);
    assert(loaded.pending_registration_count == 1);
    assert(rl_meta_species_unlocked(&loaded, 8));
    assert(loaded.pending_species[(9 - 1) >> 3] &
           (1U << ((9 - 1) & 7)));
    assert(rl_meta_register_species(&loaded, 8) == RL_OK);
    assert(loaded.pending_registration_count == 1);
    assert(rl_meta_register_species(&loaded, 9) == RL_OK);
    assert(loaded.currency == 50 && loaded.runs == 2);
    assert(loaded.pending_registration_count == 0);
    assert(rl_storage_save(&device, &loaded, NULL, 0) == RL_SAVE_OK);

    memset(&meta, 0, sizeof(meta));
    assert(rl_storage_load(&device, &meta, NULL, &has_run) == RL_SAVE_OK);
    assert(!has_run && meta.currency == 50 && meta.runs == 2);
    assert(meta.pending_registration_count == 0);
    assert(rl_meta_species_unlocked(&meta, 8));
    assert(rl_meta_species_unlocked(&meta, 9));
}

static void be16(RlU8 *p, unsigned n) {
    p[0] = (RlU8)(n >> 8); p[1] = (RlU8)n;
}

static void inode_sum(RlU8 *m) {
    unsigned i, sum = 0;
    for (i = 266; i < 512; i++) sum += m[i];
    m[257] = (RlU8)sum;
    memcpy(m + 512, m + 256, 256);
}

static void pak_fixture(RlU8 *m) {
    unsigned i, sum = 0, inverse = 0;
    memset(m, 0, RL_PAK_METADATA_SIZE);
    m[32 + 25] = 1; m[32 + 26] = 1;
    for (i = 32; i < 60; i += 2) {
        unsigned n = ((unsigned)m[i] << 8) | m[i + 1];
        sum += n; inverse += (~n) & 65535U;
    }
    be16(m + 60, sum); be16(m + 62, inverse);
    memcpy(m + 96, m + 32, 32);
    memcpy(m + 128, m + 32, 32);
    memcpy(m + 192, m + 32, 32);
    for (i = 5; i < 128; i++) be16(m + 256 + 2 * i, 3);
    inode_sum(m);
}

static void filesystem_tests(void) {
    RlU8 m[RL_PAK_METADATA_SIZE], copy[RL_PAK_METADATA_SIZE];
    RlPakLayout layout;
    unsigned i;
    pak_fixture(m);
    assert(rl_pak_validate(m, &layout) == RL_SAVE_NOT_FOUND);
    assert(layout.free_pages == 123 && layout.free_notes == 16);
    memcpy(copy, m, sizeof(m));
    m[96]++;
    assert(rl_pak_validate(m, &layout) == RL_SAVE_BAD_DATA);
    memcpy(m, copy, sizeof(m));
    m[257]++;
    assert(rl_pak_validate(m, &layout) == RL_SAVE_BAD_DATA);
    memcpy(m, copy, sizeof(m));
    for (i = 0; i < RL_PAK_PAGES; i++)
        be16(m + 256 + (5 + i) * 2, i + 1 == RL_PAK_PAGES ? 1 : 6 + i);
    inode_sum(m);
    memcpy(m + 768, "RL01", 4);
    be16(m + 772, RL_PAK_COMPANY_CODE);
    be16(m + 774, 5);
    memcpy(m + 780, gRlPakExtension, 4);
    memcpy(m + 784, gRlPakName, 16);
    assert(rl_pak_validate(m, &layout) == RL_SAVE_OK);
    assert(layout.note == 0 && layout.free_pages == 113);
    for (i = 0; i < RL_PAK_PAGES; i++) assert(layout.pages[i] == i + 5);
    memcpy(copy, m, sizeof(m));
    memcpy(m + 800, m + 768, 32);
    assert(rl_pak_validate(m, &layout) == RL_SAVE_BAD_DATA);
    memcpy(m, copy, sizeof(m));
    be16(m + 266, 5); inode_sum(m);
    assert(rl_pak_validate(m, &layout) == RL_SAVE_BAD_DATA);
    memcpy(m, copy, sizeof(m));
    be16(m + 266, 1); inode_sum(m);
    assert(rl_pak_validate(m, &layout) == RL_SAVE_BAD_DATA);
    memset(m, 0x80, sizeof(m));
    assert(rl_pak_validate(m, &layout) == RL_SAVE_UNSUPPORTED);
}

int main(void) {
    MemoryDevice memory, saved;
    RlSaveDevice device;
    RlMeta meta, output, sentinel;
    RunState run, loaded;
    RlU8 has_run;
    RlSaveStatus status;
    unsigned i;
    int cut;
    memset(&memory, 0, sizeof(memory));
    memset(&meta, 0, sizeof(meta));
    memset(&sentinel, 0x55, sizeof(sentinel));
    memory.budget = -1;
    device.context = &memory;
    device.open = mem_open; device.read = mem_read; device.write = mem_write;
    output = sentinel; has_run = 99;
    assert(rl_storage_load(&device, &output, NULL, &has_run) == RL_SAVE_NOT_FOUND);
    assert(!memcmp(&output, &sentinel, sizeof(output)) && has_run == 99);
    assert(rl_storage_save(&device, &meta, NULL, 0) == RL_SAVE_NOT_FOUND);
    for (i = RL_SAVE_NO_DEVICE; i <= RL_SAVE_STORAGE_FULL; i++) {
        memory.open_error = (RlSaveStatus)i;
        assert(rl_storage_save(&device, &meta, NULL, 1) == (RlSaveStatus)i);
        assert(!memory.writes);
    }
    memory.open_error = RL_SAVE_OK;
    meta.runs = 42;
    assert(rl_storage_save(&device, &meta, NULL, 1) == RL_SAVE_OK);
    assert(memory.bytes[12] == (416 & 255) && memory.bytes[13] == (416 >> 8));
    assert(rl_storage_load(&device, &output, NULL, &has_run) == RL_SAVE_OK);
    assert(output.runs == 42 && !has_run);
    meta.runs = 43;
    assert(rl_storage_save(&device, &meta, NULL, 0) == RL_SAVE_OK);
    saved = memory;
    /* Every byte cut across invalidation, padded payload, and commit header. */
    meta.runs = 44;
    for (cut = 0; cut <= 480; cut++) {
        memory = saved;
        memory.budget = cut;
        status = rl_storage_save(&device, &meta, NULL, 0);
        assert(status == (cut < 480 ? RL_SAVE_IO : RL_SAVE_OK));
        memory.budget = -1;
        status = rl_storage_load(&device, &output, NULL, &has_run);
        assert(status == RL_SAVE_OK || status == RL_SAVE_RECOVERED);
        assert(output.runs == 43 || output.runs == 44);
        if (cut < 480) assert(output.runs == 43);
    }
    memory = saved; memory.bytes[32] ^= 1;
    assert(rl_storage_load(&device, &output, NULL, &has_run) == RL_SAVE_RECOVERED);
    assert(output.runs == 43);
    assert(rl_storage_save(&device, &meta, NULL, 1) == RL_SAVE_BAD_CRC);
    memory = saved; memory.bytes[RL_STORAGE_SLOT_SIZE + 32] ^= 1;
    assert(rl_storage_load(&device, &output, NULL, &has_run) == RL_SAVE_RECOVERED);
    assert(output.runs == 42);
    memory.bytes[32] ^= 1; output = sentinel;
    assert(rl_storage_load(&device, &output, NULL, &has_run) == RL_SAVE_BAD_CRC);
    assert(!memcmp(&output, &sentinel, sizeof(output)));
    memory = saved;
    le32(memory.bytes + 4, 2);
    le32(memory.bytes + 28, rl_save_crc32(memory.bytes, 28));
    assert(rl_storage_load(&device, &output, NULL, &has_run) == RL_SAVE_BAD_VERSION);
    assert(rl_storage_save(&device, &meta, NULL, 1) == RL_SAVE_BAD_VERSION);
    memory = saved; memory.fail_read = 1; memory.reads = 0;
    assert(rl_storage_load(&device, &output, NULL, &has_run) == RL_SAVE_IO);
    for (cut = 1; cut <= 7; cut++) {
        memory = saved; memory.fail_read = cut; memory.reads = 0;
        assert(rl_storage_save(&device, &meta, NULL, 0) == RL_SAVE_IO);
        memory.fail_read = 0;
        assert(rl_storage_load(&device, &output, NULL, &has_run) == RL_SAVE_OK);
        assert(output.runs == (cut <= 5 ? 43U : 44U));
    }
    memory = saved;
    le32(memory.bytes + 8, 0xfffffffeU);
    le32(memory.bytes + 28, rl_save_crc32(memory.bytes, 28));
    le32(memory.bytes + RL_STORAGE_SLOT_SIZE + 8, 0xffffffffU);
    le32(memory.bytes + RL_STORAGE_SLOT_SIZE + 28,
         rl_save_crc32(memory.bytes + RL_STORAGE_SLOT_SIZE, 28));
    assert(rl_storage_save(&device, &meta, NULL, 0) == RL_SAVE_OK);
    assert(rl_storage_load(&device, &output, NULL, &has_run) == RL_SAVE_OK);
    assert(output.runs == 44);
    memory = saved;
    le32(memory.bytes + 8, 1);
    le32(memory.bytes + 28, rl_save_crc32(memory.bytes, 28));
    assert(rl_storage_save(&device, &meta, NULL, 0) == RL_SAVE_BAD_DATA);
    memset(&memory, 0, sizeof(memory)); memory.budget = -1;
    rl_run_new(&run, 1234, 0);
    assert(rl_storage_save(&device, &meta, &run, 1) == RL_SAVE_OK);
    assert(rl_storage_load(&device, &output, &loaded, &has_run) == RL_SAVE_OK);
    assert(has_run && loaded.seed == 1234);
    assert(rl_storage_load(&device, &output, NULL, &has_run) == RL_SAVE_INVALID);
    filesystem_tests();
    test_v3_migration();
    test_terminal_registration_cold_reload();
    puts("save fixtures: OK (481 interruption points, framing, failures, filesystem)");
    return 0;
}
