/* PURE */
#include "rl_meta.h"

static const RlU8 base_starter_species[] = {
    1, 4, 7, 10, 13, 16, 19, 21, 23, 25, 27, 29, 32, 37, 39, 41, 43,
    46, 48, 50, 52, 56, 60, 63, 66, 69, 74, 79, 84, 90, 104, 116, 118,
    129
};

static const RlU16 option_costs[RL_META_OPTION_COUNT] = {
    30, 30, 30, 30,
    50, 50, 50, 50,
    50, 0, 0,
    0, 0, 0,
    0, 0, 0, 0,
    0, 0, 0, 0
};

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

static int species_bit(const RlU8 *bits, unsigned species) {
    unsigned index;
    if (!bits || !species || species > 151) return 0;
    index = species - 1;
    return (bits[index >> 3] & (1U << (index & 7))) != 0;
}

static void set_species_bit(RlU8 *bits, unsigned species) {
    unsigned index;
    index = species - 1;
    bits[index >> 3] |= (RlU8)(1U << (index & 7));
}

static void clear_species_bit(RlU8 *bits, unsigned species) {
    unsigned index;
    index = species - 1;
    bits[index >> 3] &= (RlU8)~(1U << (index & 7));
}

static unsigned species_bit_count(const RlU8 *bits) {
    unsigned i, bit, count;
    count = 0;
    for (i = 0; i < RL_META_SPECIES_BYTES; i++)
        for (bit = 0; bit < 8; bit++)
            if (bits[i] & (1U << bit)) count++;
    return count;
}

static int base_species_unlocked(unsigned species) {
    unsigned i;
    for (i = 0; i < sizeof(base_starter_species); i++)
        if (base_starter_species[i] == species) return 1;
    return 0;
}

static int valid_meta_extension(const RlMeta *meta) {
    unsigned i, has_pending;
    if (!meta || (meta->options & ~RL_META_OPTIONS_MASK) ||
        meta->last_registration_run > meta->runs ||
        meta->pending_registration_count > 151 ||
        meta->last_registered_species > 151 ||
        (meta->last_registration_run == 0 &&
         meta->last_registered_species != 0)) return 0;
    if ((meta->registered_species[RL_META_SPECIES_BYTES - 1] & 0x80) ||
        (meta->pending_species[RL_META_SPECIES_BYTES - 1] & 0x80))
        return 0;
    if (meta->last_registered_species &&
        !species_bit(meta->registered_species,
                     meta->last_registered_species)) return 0;
    has_pending = 0;
    for (i = 0; i < RL_META_SPECIES_BYTES; i++) {
        if (meta->pending_species[i] & meta->registered_species[i])
            return 0;
        if (meta->pending_species[i]) has_pending = 1;
    }
    if ((meta->pending_registration_count != 0) != has_pending ||
        meta->pending_registration_count > species_bit_count(meta->pending_species))
        return 0;
    return 1;
}

static RlU32 add_saturated(RlU32 value, RlU32 amount) {
    return value > 0xffffffffU - amount ? 0xffffffffU : value + amount;
}

static RlU32 multiply_saturated(RlU32 value, unsigned factor) {
    if (factor && value > 0xffffffffU / factor) return 0xffffffffU;
    return value * factor;
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
    meta->currency = 0;
    meta->options = 0;
    meta->last_registration_run = 0;
    meta->pending_registration_count = 0;
    meta->last_registered_species = 0;
    for (i = 0; i < RL_META_SPECIES_BYTES; i++) {
        meta->registered_species[i] = 0;
        meta->pending_species[i] = 0;
    }
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

int rl_meta_species_unlocked(const RlMeta *meta, unsigned species) {
    if (!meta || !species || species > 151) return 0;
    return base_species_unlocked(species) ||
        species_bit(meta->registered_species, species);
}

int rl_meta_option_available(unsigned option) {
    return option <= RL_META_KIT_1 &&
        option < RL_META_OPTION_COUNT;
}

int rl_meta_option_unlocked(const RlMeta *meta, unsigned option) {
    if (!meta || !rl_meta_option_available(option)) return 0;
    return unlocked(meta->options, option, RL_META_OPTION_COUNT);
}

unsigned rl_meta_option_cost(unsigned option) {
    if (!rl_meta_option_available(option)) return 0;
    return option_costs[option];
}

int rl_meta_purchase_option(RlMeta *meta, unsigned option) {
    unsigned cost;
    if (!meta || !rl_meta_option_available(option) ||
        !valid_meta_extension(meta)) return RL_INVALID;
    if (rl_meta_option_unlocked(meta, option)) return RL_OK;
    cost = rl_meta_option_cost(option);
    if (meta->currency < cost) return RL_FUNDS;
    meta->currency -= cost;
    meta->options |= (RlU32)1U << option;
    return RL_OK;
}

int rl_meta_choose_species(const RlMeta *meta, RlU32 *rng,
                           const RlU8 *species, const RlU16 *bst,
                           unsigned count, RlU8 *selected) {
    unsigned i, j, weight, total, roll;
    if (!meta || !rng || !species || !bst || !selected ||
        !count || count > 151) return RL_INVALID;
    total = 0;
    for (i = 0; i < count; i++) {
        if (!species[i] || species[i] > 151 || !bst[i])
            return RL_INVALID;
        for (j = 0; j < i; j++) {
            if (species[j] == species[i] && bst[j] != bst[i])
                return RL_INVALID;
        }
        if (!rl_meta_species_unlocked(meta, species[i])) continue;
        for (j = 0; j < i; j++)
            if (species[j] == species[i]) break;
        if (j != i) continue;
        weight = 1000U / bst[i];
        if (!weight) weight = 1;
        total += weight;
    }
    if (!total) return RL_EMPTY;
    roll = rl_random_bounded(rng, total);
    for (i = 0; i < count; i++) {
        if (!rl_meta_species_unlocked(meta, species[i])) continue;
        for (j = 0; j < i; j++)
            if (species[j] == species[i]) break;
        if (j != i) continue;
        weight = 1000U / bst[i];
        if (!weight) weight = 1;
        if (roll < weight) {
            *selected = species[i];
            return RL_OK;
        }
        roll -= weight;
    }
    return RL_INVALID;
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

int rl_meta_register_species(RlMeta *meta, unsigned species) {
    unsigned index;
    if (!meta || !species || species > 151)
        return RL_INVALID;
    if (!valid_meta_extension(meta)) return RL_INVALID;
    if (species_bit(meta->registered_species, species)) return RL_OK;
    if (!meta->pending_registration_count) return RL_INVALID;
    index = species - 1;
    if (!(meta->pending_species[index >> 3] &
          (1U << (index & 7)))) return RL_INVALID;
    if (rl_meta_species_unlocked(meta, species)) return RL_INVALID;
    set_species_bit(meta->registered_species, species);
    meta->last_registration_run = meta->runs;
    meta->last_registered_species = (RlU8)species;
    clear_species_bit(meta->pending_species, species);
    meta->pending_registration_count--;
    if (!meta->pending_registration_count) {
        for (index = 0; index < RL_META_SPECIES_BYTES; index++)
            meta->pending_species[index] = 0;
    }
    return RL_OK;
}

int rl_meta_skip_species_registration(RlMeta *meta) {
    unsigned i;
    if (!meta || !valid_meta_extension(meta)) return RL_INVALID;
    if (!meta->pending_registration_count) return RL_INVALID;
    meta->last_registration_run = meta->runs;
    meta->last_registered_species = 0;
    meta->pending_registration_count--;
    if (!meta->pending_registration_count)
        for (i = 0; i < RL_META_SPECIES_BYTES; i++)
            meta->pending_species[i] = 0;
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
    unsigned floor, i, payout;
    int added_pending_species;
    if (!meta || !run || meta->hall_count > RL_PARTY ||
        meta->ascension > 10 ||
        !valid_meta_extension(meta) ||
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
    if (run->stats.wins > run->stats.battles) return RL_INVALID;
    for (i = 0; i < run->party_count; i++)
        if (!valid_hall_mon(&run->party[i])) return RL_INVALID;

    added_pending_species = 0;
    for (i = 0; i < run->party_count; i++) {
        unsigned species, index;
        species = run->party[i].species;
        if (!species || species > 151 ||
            rl_meta_species_unlocked(meta, species)) continue;
        index = species - 1;
        if (meta->pending_species[index >> 3] &
            (1U << (index & 7))) continue;
        added_pending_species = 1;
    }
    if (added_pending_species &&
        meta->pending_registration_count >= 151) return RL_FULL;

    if (meta->runs != 0xffffffffU) meta->runs++;
    floor = run->act * RL_ROWS;
    if (run->row != RL_NO_SLOT) floor += (unsigned)run->row + 1;
    if (floor > meta->best_floor) meta->best_floor = floor;

    payout = multiply_saturated(run->stats.wins,
                                RL_META_WIN_ENCOUNTER_COINS);
    if (run->stats.battles > run->stats.wins)
        payout = add_saturated(payout,
            multiply_saturated(run->stats.battles - run->stats.wins,
                               RL_META_LOSS_ENCOUNTER_COINS));
    if (run->phase == RL_WON)
        payout = add_saturated(payout, RL_META_CLEAR_BONUS_COINS);
    meta->currency = add_saturated(meta->currency, payout);

    for (i = 0; i < run->party_count; i++) {
        unsigned species, index;
        species = run->party[i].species;
        if (!species || species > 151 ||
            rl_meta_species_unlocked(meta, species)) continue;
        index = species - 1;
        if (meta->pending_species[index >> 3] &
            (1U << (index & 7))) continue;
        meta->pending_species[index >> 3] |=
            (RlU8)(1U << (index & 7));
    }
    if (added_pending_species) meta->pending_registration_count++;

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
