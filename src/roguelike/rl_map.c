/* PURE */
#include "rl_core.h"

static void rl_zero_bytes(void *target, unsigned size) {
    volatile unsigned char *bytes;
    unsigned i;
    bytes = (volatile unsigned char *)target;
    for (i = 0; i < size; i++) bytes[i] = 0;
}

unsigned rl_map_rows(unsigned act) {
    return act == 3 ? 1U : (act < 3 ? RL_ROWS : 0U);
}

void rl_map_generate(RlMap *map, RlU32 *rng, unsigned act, unsigned ascension) {
    unsigned row, lane, center_row, center_lane, next, type;
    RlAscension asc;
    if (!map || !rng) return;
    if (act > 3) act = 3;
    rl_zero_bytes(map, sizeof(*map));
    if (act == 3) {
        map->lanes[0] = 1;
        map->nodes[0][0].type = RL_BOSS;
        map->nodes[0][0].target_species = 150;
        return;
    }
    asc = rl_ascension(ascension);
    center_row = 2 + rl_random_bounded(rng, 4);
    for (row = 0; row < RL_ROWS; row++) {
        map->lanes[row] = (RlU8)(2 + rl_random_bounded(rng, 3));
        for (lane = 0; lane < map->lanes[row]; lane++) {
            type = rl_random_bounded(rng, RL_BOSS);
            if (row < 2 && type == RL_ELITE) type = RL_BATTLE;
            if (asc.fewer_centers && type == RL_CENTER) type = RL_BATTLE;
            map->nodes[row][lane].type = (RlU8)type;
        }
    }
    center_lane = rl_random_bounded(rng, map->lanes[center_row]);
    map->nodes[center_row][center_lane].type = RL_CENTER;
    for (lane = 0; lane < map->lanes[RL_ROWS - 1]; lane++)
        map->nodes[RL_ROWS - 1][lane].type = RL_BOSS;
    for (row = 0; row < RL_ROWS - 1; row++) {
        next = map->lanes[row + 1];
        for (lane = 0; lane < map->lanes[row]; lane++) {
            map->nodes[row][lane].edges = (RlU8)(1U << (lane % next));
            map->nodes[row][lane].edges |=
                (RlU8)(1U << rl_random_bounded(rng, next));
        }
        for (lane = 0; lane < next; lane++)
            map->nodes[row][lane % map->lanes[row]].edges |=
                (RlU8)(1U << lane);
        if (row + 1 == center_row) {
            for (lane = 0; lane < map->lanes[row]; lane++)
                map->nodes[row][lane].edges |= (RlU8)(1U << center_lane);
        }
    }
}

int rl_map_valid(const RlMap *map) {
    unsigned row, lane, reachable, next, center, rows;
    if (!map) return 0;
    rows = map->lanes[0] == 1 ? 1U : RL_ROWS;
    for (row = 0; row < RL_ROWS; row++) {
        if (row >= rows && map->lanes[row]) return 0;
        for (lane = row < rows ? map->lanes[row] : 0; lane < RL_LANES; lane++)
            if (map->nodes[row][lane].type || map->nodes[row][lane].edges ||
                map->nodes[row][lane].target_species) return 0;
    }
    if (rows == 1)
        return map->nodes[0][0].type == RL_BOSS &&
            !map->nodes[0][0].edges &&
            map->nodes[0][0].target_species == 150;
    reachable = 15;
    center = 0;
    for (row = 0; row < RL_ROWS; row++) {
        if (map->lanes[row] < 2 || map->lanes[row] > RL_LANES) return 0;
        if ((reachable & ((1U << map->lanes[row]) - 1)) !=
            ((1U << map->lanes[row]) - 1)) return 0;
        next = 0;
        for (lane = 0; lane < map->lanes[row]; lane++) {
            const RlNode *node;
            node = &map->nodes[row][lane];
            if (node->type > RL_BOSS) return 0;
            if (node->target_species) return 0;
            if ((row == RL_ROWS - 1) != (node->type == RL_BOSS)) return 0;
            if (row < 2 && node->type == RL_ELITE) return 0;
            if (node->type == RL_CENTER && row < RL_ROWS - 1) center = 1;
            if (row == RL_ROWS - 1) {
                if (node->edges) return 0;
            } else {
                if (!node->edges || (node->edges >> map->lanes[row + 1])) return 0;
                next |= node->edges;
            }
        }
        reachable = next;
    }
    return (int)center;
}
