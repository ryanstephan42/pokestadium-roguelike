#include "rl_save.h"
#include "rl_save_probe.h"

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
        }
    } else if (command == 2 || command == 3) {
        meta.runs = gRlSaveProbe.runs;
        meta.wins = gRlSaveProbe.wins;
        meta.best_floor = gRlSaveProbe.best_floor;
        if (command == 3) rl_run_new(&run, gRlSaveProbe.seed, 0);
        status = rl_save_store(gRlSaveProbe.controller, &meta,
                                command == 3 ? &run : 0, gRlSaveProbe.create);
    } else status = RL_SAVE_INVALID;
    gRlSaveProbe.status = status;
    gRlSaveProbe.command = 0;
    return 1;
}
