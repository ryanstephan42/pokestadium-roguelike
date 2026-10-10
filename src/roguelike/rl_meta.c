/* PURE */
#include "rl_meta.h"

static void clear_mon(RlMon *mon) {
    volatile RlU8 *bytes;
    unsigned i;
    bytes = (volatile RlU8 *)mon;
    for (i = 0; i < sizeof(*mon); i++) bytes[i] = 0;
}

static int valid_hall_mon(const RlMon *mon) {
    unsigned i;
    if (!mon || !mon->species || mon->species > 151 ||
        !mon->level || mon->level > 100 ||
        mon->growth > RL_MEDIUM_SLOW || !mon->max_hp ||
        mon->cur_hp > mon->max_hp ||
        mon->fainted > 1 ||
        mon->fainted != (mon->cur_hp == 0) ||
        mon->nickname[10] != '\0' ||
        (mon->held_relic != RL_NO_SLOT &&
         mon->held_relic >= RL_RELIC_COUNT)) return 0;
    for (i = 0; i < 4; i++)
        if (mon->pp[i] > mon->max_pp[i]) return 0;
    return rl_mon_stat_value(mon, RL_HP, mon->level) == mon->max_hp;
}

static int unlocked(RlU32 bits, unsigned id, unsigned count) {
    if (id >= count) return 0;
    return (bits & ((RlU32)1U << id)) != 0;
}

static RlU32 catalog_mask(unsigned count) {
    if (count >= 32) return 0xffffffffU;
    return ((RlU32)1U << count) - 1U;
}

static void unlock_content(RlMeta *meta) {
    unsigned id;
    for (id = RL_META_INITIAL_RELICS; id < RL_RELIC_COUNT; id++) {
        if (meta->wins >= id - RL_META_INITIAL_RELICS + 1)
            meta->unlocked_relics |= (RlU32)1U << id;
    }
    for (id = RL_META_INITIAL_EVENTS; id < RL_EVENT_COUNT; id++) {
        if (meta->wins >= id - RL_META_INITIAL_EVENTS + 1)
            meta->unlocked_events |= (RlU32)1U << id;
    }
    if (meta->wins >= 1)
        meta->unlocked_pools |= (RlU32)1U << RL_META_POOL_PRIME_CUP;
    if (meta->wins >= 5)
        meta->unlocked_pools |= (RlU32)1U << RL_META_POOL_LITTLE_CUP;
}

void rl_meta_init(RlMeta *meta) {
    unsigned i;
    if (!meta) return;
    meta->unlocked_pools = (RlU32)1U << RL_META_POOL_STARTERS;
    meta->unlocked_relics = (((RlU32)1U << RL_META_INITIAL_RELICS) - 1U);
    meta->unlocked_events = (((RlU32)1U << RL_META_INITIAL_EVENTS) - 1U);
    meta->runs = 0;
    meta->wins = 0;
    meta->best_floor = 0;
    meta->ascension = 0;
    meta->hall_count = 0;
    for (i = 0; i < RL_PARTY; i++) clear_mon(&meta->hall[i]);
}

RlU32 rl_meta_permitted_pools(const RlMeta *meta) {
    if (!meta) return 0;
    return meta->unlocked_pools & catalog_mask(RL_META_POOL_COUNT);
}

RlU32 rl_meta_permitted_relics(const RlMeta *meta) {
    if (!meta) return 0;
    return meta->unlocked_relics & catalog_mask(RL_RELIC_COUNT);
}

RlU32 rl_meta_permitted_events(const RlMeta *meta) {
    if (!meta) return 0;
    return meta->unlocked_events & catalog_mask(RL_EVENT_COUNT);
}

int rl_meta_starter_unlocked(const RlMeta *meta, unsigned pool) {
    if (!meta || pool >= RL_META_POOL_COUNT) return 0;
    return unlocked(meta->unlocked_pools, pool, RL_META_POOL_COUNT);
}

int rl_meta_relic_unlocked(const RlMeta *meta, unsigned relic) {
    if (!meta) return 0;
    return unlocked(meta->unlocked_relics, relic, RL_RELIC_COUNT);
}

int rl_meta_event_unlocked(const RlMeta *meta, unsigned event) {
    if (!meta) return 0;
    return unlocked(meta->unlocked_events, event, RL_EVENT_COUNT);
}

int rl_meta_choose_event(const RlMeta *meta, RlU32 *rng, RlU8 *event) {
    unsigned candidates[RL_EVENT_COUNT];
    unsigned i, count, selected;
    if (!meta || !rng || !event) return RL_INVALID;
    count = 0;
    for (i = 0; i < RL_EVENT_COUNT; i++)
        if (rl_meta_event_unlocked(meta, i)) candidates[count++] = i;
    if (!count) return RL_EMPTY;
    selected = rl_random_bounded(rng, count);
    *event = (RlU8)candidates[selected];
    return RL_OK;
}

int rl_meta_select_ascension(const RlMeta *meta, unsigned requested,
                             RlU8 *selected) {
    if (!meta || !selected || requested > 10 ||
        requested > meta->ascension || meta->ascension > 10)
        return RL_INVALID;
    *selected = (RlU8)requested;
    return RL_OK;
}

int rl_meta_record_run(RlMeta *meta, const RunState *run) {
    unsigned floor, i;
    if (!meta || !run || meta->hall_count > RL_PARTY ||
        meta->ascension > 10 ||
        (run->phase != RL_WON && run->phase != RL_LOST) ||
        run->act > 3 || run->ascension > 10 ||
        run->ascension > meta->ascension ||
        run->party_count > RL_PARTY ||
        !rl_map_valid(&run->map) ||
        ((run->act == 3) != (run->map.lanes[0] == 1)) ||
        (run->row != RL_NO_SLOT && run->row >= rl_map_rows(run->act)) ||
        (run->row == RL_NO_SLOT && run->lane != RL_NO_SLOT) ||
        (run->row != RL_NO_SLOT &&
         run->lane >= run->map.lanes[run->row]) ||
        (run->phase == RL_WON && (run->act != 3 || run->row != 0)) ||
        (run->phase == RL_WON && !run->party_count))
        return RL_INVALID;
    if (run->phase == RL_WON) {
        for (i = 0; i < run->party_count; i++)
            if (!valid_hall_mon(&run->party[i])) return RL_INVALID;
    }

    if (meta->runs != 0xffffffffU) meta->runs++;
    floor = run->act * RL_ROWS;
    if (run->row != RL_NO_SLOT) floor += (unsigned)run->row + 1;
    if (floor > meta->best_floor) meta->best_floor = floor;

    if (run->phase == RL_WON) {
        if (meta->wins != 0xffffffffU) meta->wins++;
        unlock_content(meta);
        if (run->ascension < 10 &&
            run->ascension == meta->ascension)
            meta->ascension = (RlU8)(run->ascension + 1);
        meta->hall_count = run->party_count;
        for (i = 0; i < RL_PARTY; i++) {
            if (i < run->party_count) meta->hall[i] = run->party[i];
            else clear_mon(&meta->hall[i]);
        }
    }
    return RL_OK;
}
