/* PURE */
#include "rl_save_format.h"
#define RL_SAVE_MAGIC 0x56534c52U

typedef struct RlWriter {
    RlU8 *data;
    unsigned cap, pos, failed;
} RlWriter;
typedef struct RlReader {
    const RlU8 *data;
    unsigned len, pos, failed;
} RlReader;

static void copy_bytes(void *target, const void *source, unsigned size) {
    volatile unsigned char *to;
    const volatile unsigned char *from;
    unsigned i;
    to = (volatile unsigned char *)target;
    from = (const volatile unsigned char *)source;
    for (i = 0; i < size; i++) to[i] = from[i];
}

static void put8(RlWriter *w, RlU8 value) {
    if (w->pos >= w->cap) { w->failed = 1; return; }
    w->data[w->pos++] = value;
}

static void put16(RlWriter *w, RlU16 value) {
    put8(w, (RlU8)value);
    put8(w, (RlU8)(value >> 8));
}

static void put32(RlWriter *w, RlU32 value) {
    put16(w, (RlU16)value);
    put16(w, (RlU16)(value >> 16));
}

static RlU8 get8(RlReader *r) {
    if (r->pos >= r->len) { r->failed = 1; return 0; }
    return r->data[r->pos++];
}

static RlU16 get16(RlReader *r) {
    RlU16 value;
    value = get8(r);
    value |= (RlU16)((RlU16)get8(r) << 8);
    return value;
}

static RlU32 get32(RlReader *r) {
    RlU32 value;
    value = get16(r);
    value |= (RlU32)get16(r) << 16;
    return value;
}

static void write_mon(RlWriter *w, const RlMon *mon) {
    unsigned i;
    put8(w, mon->species); put8(w, mon->level);
    put8(w, mon->growth); put8(w, mon->fainted);
    put32(w, mon->exp); put16(w, mon->dvs);
    for (i = 0; i < 5; i++) put16(w, mon->stat_exp[i]);
    for (i = 0; i < 5; i++) put8(w, mon->base_stats[i]);
    for (i = 0; i < 4; i++) put8(w, mon->moves[i]);
    for (i = 0; i < 4; i++) put8(w, mon->pp[i]);
    for (i = 0; i < 4; i++) put8(w, mon->max_pp[i]);
    put16(w, mon->cur_hp); put16(w, mon->max_hp);
    put8(w, mon->status); put8(w, mon->held_relic);
    for (i = 0; i < 11; i++) put8(w, (RlU8)mon->nickname[i]);
}

static void read_mon(RlReader *r, RlMon *mon) {
    unsigned i;
    for (i = 0; i < 5; i++) mon->current_stats[i] = 0;
    mon->species = get8(r); mon->level = get8(r);
    mon->growth = get8(r); mon->fainted = get8(r);
    mon->exp = get32(r); mon->dvs = get16(r);
    for (i = 0; i < 5; i++) mon->stat_exp[i] = get16(r);
    for (i = 0; i < 5; i++) mon->base_stats[i] = get8(r);
    for (i = 0; i < 4; i++) mon->moves[i] = get8(r);
    for (i = 0; i < 4; i++) mon->pp[i] = get8(r);
    for (i = 0; i < 4; i++) mon->max_pp[i] = get8(r);
    mon->cur_hp = get16(r); mon->max_hp = get16(r);
    mon->status = get8(r); mon->held_relic = get8(r);
    for (i = 0; i < 11; i++) mon->nickname[i] = (char)get8(r);
    if (mon->species && mon->level) {
        RlU16 max_hp;
        max_hp = mon->max_hp;
        if (rl_mon_stat_value(mon, RL_HP, mon->level) != max_hp)
            r->failed = 1;
        rl_mon_recalculate_stats(mon, 0);
        if (mon->max_hp != max_hp) r->failed = 1;
    }
}

static void write_run(RlWriter *w, const RunState *run) {
    unsigned i, j;
    put32(w, run->seed); put32(w, run->rng);
    put8(w, run->act); put8(w, run->row); put8(w, run->lane);
    put8(w, run->phase); put8(w, run->ascension); put8(w, run->party_count);
    for (i = 0; i < RL_ROWS; i++) {
        put8(w, run->map.lanes[i]);
        for (j = 0; j < RL_LANES; j++) {
            put8(w, run->map.nodes[i][j].type);
            put8(w, run->map.nodes[i][j].edges);
            put8(w, run->map.nodes[i][j].target_species);
        }
    }
    for (i = 0; i < RL_PARTY; i++) write_mon(w, &run->party[i]);
    for (i = 0; i < RL_ITEM_COUNT; i++) put16(w, run->bag[i]);
    put32(w, run->money); put32(w, run->relics); put32(w, run->consumed_relics);
    put32(w, run->stats.battles); put32(w, run->stats.wins);
    put32(w, run->stats.nodes); put32(w, run->stats.recruits);
    put8(w, run->tm_count);
    for (i = 0; i < RL_TM_CAPACITY; i++) put8(w, run->tm_moves[i]);
    write_mon(w, &run->daycare);
    put8(w, run->daycare_nodes); put8(w, run->daycare_active);
}

static void read_run(RlReader *r, RunState *run) {
    unsigned i, j;
    run->seed = get32(r); run->rng = get32(r);
    run->act = get8(r); run->row = get8(r); run->lane = get8(r);
    run->phase = get8(r); run->ascension = get8(r); run->party_count = get8(r);
    for (i = 0; i < RL_ROWS; i++) {
        run->map.lanes[i] = get8(r);
        for (j = 0; j < RL_LANES; j++) {
            run->map.nodes[i][j].type = get8(r);
            run->map.nodes[i][j].edges = get8(r);
            run->map.nodes[i][j].target_species = get8(r);
        }
    }
    for (i = 0; i < RL_PARTY; i++) read_mon(r, &run->party[i]);
    for (i = 0; i < RL_ITEM_COUNT; i++) run->bag[i] = get16(r);
    run->money = get32(r); run->relics = get32(r);
    run->consumed_relics = get32(r);
    run->stats.battles = get32(r); run->stats.wins = get32(r);
    run->stats.nodes = get32(r); run->stats.recruits = get32(r);
    run->tm_count = get8(r);
    for (i = 0; i < RL_TM_CAPACITY; i++) run->tm_moves[i] = get8(r);
    read_mon(r, &run->daycare);
    run->daycare_nodes = get8(r); run->daycare_active = get8(r);
}

static int valid_mon(const RlMon *mon, int required) {
    unsigned i;
    if (!mon) return 0;
    if (!mon->species) {
        if (required || mon->level || mon->exp || mon->dvs ||
            mon->cur_hp || mon->max_hp || mon->fainted || mon->growth ||
            mon->status || mon->held_relic) return 0;
        for (i = 0; i < 5; i++)
            if (mon->stat_exp[i] || mon->base_stats[i] ||
                mon->current_stats[i]) return 0;
        for (i = 0; i < 4; i++)
            if (mon->moves[i] || mon->pp[i] || mon->max_pp[i]) return 0;
        for (i = 0; i < 11; i++)
            if (mon->nickname[i]) return 0;
        return 1;
    }
    if (mon->species > 151 || !mon->level || mon->level > 100 ||
        mon->growth > RL_MEDIUM_SLOW || mon->cur_hp > mon->max_hp ||
        !mon->max_hp || mon->fainted > 1 ||
        mon->fainted != (mon->cur_hp == 0) || mon->nickname[10] != '\0' ||
        (mon->held_relic != RL_NO_SLOT &&
         mon->held_relic >= RL_RELIC_COUNT)) return 0;
    for (i = 0; i < 4; i++)
        if (mon->pp[i] > mon->max_pp[i]) return 0;
    if (rl_mon_stat_value(mon, RL_HP, mon->level) != mon->max_hp) return 0;
    return 1;
}

static int valid_run(const RunState *run) {
    unsigned i;
    if (!run || run->act > 3 || run->phase > RL_LOST ||
        run->ascension > 10 || run->party_count > RL_PARTY ||
        run->daycare_active > 2 || run->tm_count > RL_TM_CAPACITY ||
        (run->consumed_relics & ~run->relics) ||
        (run->relics >> RL_RELIC_COUNT) || !rl_map_valid(&run->map))
        return 0;
    for (i = 0; i < RL_ITEM_COUNT; i++)
        if (run->bag[i] > 999) return 0;
    for (i = 0; i < RL_TM_CAPACITY; i++) {
        if (i < run->tm_count) {
            if (!run->tm_moves[i] || run->tm_moves[i] > 165) return 0;
        } else if (run->tm_moves[i]) return 0;
    }
    if ((run->act == 3) != (run->map.lanes[0] == 1)) return 0;
    if (run->row == RL_NO_SLOT) {
        if (run->lane != RL_NO_SLOT) return 0;
    } else if (run->row >= rl_map_rows(run->act) ||
               run->lane >= run->map.lanes[run->row]) return 0;
    for (i = 0; i < RL_PARTY; i++)
        if (!valid_mon(&run->party[i], i < run->party_count)) return 0;
    if (run->daycare_active) {
        if (!valid_mon(&run->daycare, 1)) return 0;
        if ((run->daycare_active == 1 && run->daycare_nodes > 1) ||
            (run->daycare_active == 2 && run->daycare_nodes != 2)) return 0;
    } else if (run->daycare_nodes || !valid_mon(&run->daycare, 0)) return 0;
    return 1;
}

RlU32 rl_save_crc32(const RlU8 *bytes, unsigned length) {
    RlU32 crc;
    unsigned i, bit;
    if (!bytes && length) return 0;
    crc = 0xffffffffU;
    for (i = 0; i < length; i++) {
        crc ^= bytes[i];
        for (bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320U : 0);
    }
    return crc ^ 0xffffffffU;
}

int rl_save_encode(const RlMeta *meta, const RunState *run, RlU8 *buffer,
                   unsigned capacity, unsigned *written) {
    RlWriter w;
    unsigned i;
    if (!meta || !buffer || !written || capacity < 32 ||
        meta->hall_count > RL_PARTY ||
        meta->ascension > 10) return RL_INVALID;
    for (i = 0; i < RL_PARTY; i++)
        if (!valid_mon(&meta->hall[i], i < meta->hall_count)) return RL_INVALID;
    if (run && !valid_run(run)) return RL_INVALID;
    w.data = buffer;
    w.cap = capacity > RL_SAVE_MAX_SIZE ? RL_SAVE_MAX_SIZE : capacity;
    w.pos = 0; w.failed = 0;
    put32(&w, RL_SAVE_MAGIC); put16(&w, RL_SAVE_VERSION);
    put8(&w, run ? 1 : 0); put8(&w, meta->ascension);
    put32(&w, meta->unlocked_pools); put32(&w, meta->unlocked_relics);
    put32(&w, meta->unlocked_events); put32(&w, meta->runs);
    put32(&w, meta->wins); put32(&w, meta->best_floor);
    put8(&w, meta->hall_count);
    for (i = 0; i < RL_PARTY; i++) write_mon(&w, &meta->hall[i]);
    if (run) write_run(&w, run);
    if (w.failed || w.pos + 4 > w.cap) return RL_FULL;
    put32(&w, rl_save_crc32(buffer, w.pos));
    if (w.failed) return RL_FULL;
    *written = w.pos;
    if (!run && w.pos > RL_META_MAX_SIZE) return RL_FULL;
    return RL_OK;
}

int rl_save_decode(RlMeta *meta, RunState *run, RlU8 *has_run,
                   const RlU8 *buffer, unsigned length) {
    RlReader r;
    unsigned i, flag;
    RlU32 stored_crc;
    RlMeta decoded_meta;
    RunState decoded_run;
    if (!meta || !has_run || !buffer || length < 32 ||
        length > RL_SAVE_MAX_SIZE) return RL_INVALID;
    stored_crc = (RlU32)buffer[length - 4] |
        ((RlU32)buffer[length - 3] << 8) |
        ((RlU32)buffer[length - 2] << 16) |
        ((RlU32)buffer[length - 1] << 24);
    if (stored_crc != rl_save_crc32(buffer, length - 4)) return RL_CORRUPT;
    r.data = buffer; r.len = length - 4; r.pos = 0; r.failed = 0;
    if (get32(&r) != RL_SAVE_MAGIC || get16(&r) != RL_SAVE_VERSION)
        return RL_CORRUPT;
    flag = get8(&r);
    decoded_meta.ascension = get8(&r);
    decoded_meta.unlocked_pools = get32(&r);
    decoded_meta.unlocked_relics = get32(&r);
    decoded_meta.unlocked_events = get32(&r);
    decoded_meta.runs = get32(&r); decoded_meta.wins = get32(&r);
    decoded_meta.best_floor = get32(&r);
    decoded_meta.hall_count = get8(&r);
    if (flag > 1 || decoded_meta.ascension > 10 ||
        decoded_meta.hall_count > RL_PARTY) return RL_CORRUPT;
    for (i = 0; i < RL_PARTY; i++) read_mon(&r, &decoded_meta.hall[i]);
    for (i = 0; i < RL_PARTY; i++)
        if (!valid_mon(&decoded_meta.hall[i], i < decoded_meta.hall_count))
            return RL_CORRUPT;
    if (flag) {
        if (!run) return RL_INVALID;
        read_run(&r, &decoded_run);
        if (!valid_run(&decoded_run)) return RL_CORRUPT;
    }
    if (r.failed || r.pos != r.len) return RL_CORRUPT;
    copy_bytes(meta, &decoded_meta, sizeof(*meta));
    if (flag) copy_bytes(run, &decoded_run, sizeof(*run));
    *has_run = (RlU8)flag;
    return RL_OK;
}
