#include "rl_save.h"
#include "rl_save_probe.h"
#include "rl_meta.h"

volatile RlSaveProbe gRlSaveProbe;

int rl_save_probe_poll(void) {
    static RlMeta meta;
    static RunState run;
    RlU8 suspended;
    RlSaveStatus status;
    RlU8 *bytes;
    unsigned i, command;
    command = gRlSaveProbe.command;
    if (!command) return 0;
    bytes = (RlU8 *)&meta;
    for (i = 0; i < sizeof(meta); i++) bytes[i] = 0;
    if (command == 1) {
        status = rl_save_load(gRlSaveProbe.controller, &meta, &run, &suspended);
        if (status == RL_SAVE_OK || status == RL_SAVE_RECOVERED) {
            gRlSaveProbe.runs = meta.runs;
            gRlSaveProbe.wins = meta.wins;
            gRlSaveProbe.best_floor = meta.best_floor;
            gRlSaveProbe.suspended = suspended;
            gRlSaveProbe.seed = suspended ? run.seed : 0;
            gRlSaveProbe.pools = meta.unlocked_pools;
            gRlSaveProbe.relics = meta.unlocked_relics;
            gRlSaveProbe.events = meta.unlocked_events;
            gRlSaveProbe.ascension = meta.ascension;
        }
    } else if (command == 2 || command == 3) {
        meta.runs = gRlSaveProbe.runs;
        meta.wins = gRlSaveProbe.wins;
        meta.best_floor = gRlSaveProbe.best_floor;
        if (command == 3) rl_run_new(&run, gRlSaveProbe.seed, 0);
        status = rl_save_store(gRlSaveProbe.controller, &meta,
                                command == 3 ? &run : 0, gRlSaveProbe.create);
    } else if (command == 4) {
        /* Explicit host fixture profile, never a run-state or gameplay override. */
        rl_meta_init(&meta);
        meta.unlocked_pools = (1U << RL_META_POOL_COUNT) - 1;
        meta.unlocked_relics = (1U << RL_RELIC_COUNT) - 1;
        meta.unlocked_events = (1U << RL_EVENT_COUNT) - 1;
        meta.ascension = 10;
        status = rl_save_store(gRlSaveProbe.controller, &meta, 0,
                                gRlSaveProbe.create);
    } else status = RL_SAVE_INVALID;
    gRlSaveProbe.status = status;
    gRlSaveProbe.command = 0;
    return 1;
}
