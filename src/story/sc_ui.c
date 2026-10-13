/* PURE. Presentation state never supplies story gates or native service authority. */
#include "sc_ui.h"
#include "sc_graph.h"

static void text(char *out, const char *in) {
    unsigned i;
    for (i = 0; i < 71 && in[i]; i++) out[i] = in[i];
    out[i] = 0;
}

static int equal(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static void region_name(char *out, const char *id) {
    unsigned i;
    for (i = 0; i < 71 && id[i] && id[i] != '/'; i++) out[i] = id[i];
    out[i] = 0;
}

static void number(char *out, unsigned n) {
    out[0] = (char)('0' + n / 100 % 10);
    out[1] = (char)('0' + n / 10 % 10);
    out[2] = (char)('0' + n % 10);
}

static void mon_label(char *out, const RlMon *mon) {
    text(out, "No.000 L000 HP 000/000");
    number(out + 3, mon->species);
    number(out + 8, mon->level);
    number(out + 15, mon->cur_hp);
    number(out + 19, mon->max_hp);
}

const char *sc_ui_status_text(int status) {
    switch (status) {
        case SC_OK: return "Ready";
        case SC_INVALID: return "Invalid campaign action";
        case SC_UNAVAILABLE: return "Locked or unavailable";
        case SC_FULL: return "Party full: choose a replacement";
        case SC_DUPLICATE: return "That move is already known";
        case SC_PENDING: return "Resolve the pending choice first";
        case SC_UNREVIEWED: return "Campaign catalog is not reviewed";
        default: return "Unknown campaign error";
    }
}

const char *sc_ui_save_text(int status) {
    switch (status) {
        case SC_SAVE_OK: return "Campaign saved";
        case SC_SAVE_NOT_FOUND: return "No campaign save found";
        case SC_SAVE_NO_DEVICE: return "Insert the campaign Controller Pak";
        case SC_SAVE_FULL: return "Controller Pak is full";
        case SC_SAVE_IO: return "Save I/O failed; retry with original Pak";
        case SC_SAVE_BAD_DATA: return "Invalid campaign save data";
        case SC_SAVE_BAD_VERSION: return "Unsupported campaign save version";
        case SC_SAVE_BAD_CRC: return "Campaign save checksum failed";
        case SC_SAVE_INVALID: return "Invalid campaign save request";
        case SC_SAVE_RECOVERED: return "Recovered older journal slot";
        case SC_SAVE_DEVICE_CHANGED: return "Reinsert original Controller Pak";
        default: return "Unknown campaign save error";
    }
}

static int report(ScUi *ui, int status) {
    ui->status = status;
    ui->message = sc_ui_status_text(status);
    return SC_UI_STAY;
}

static void row(ScUi *ui, const char *label, unsigned id, int disabled) {
    unsigned i = (unsigned)ui->list.count;
    if (i >= SC_ENCOUNTER_CAPACITY) {
        ui->status = SC_INVALID;
        ui->message = "Campaign menu exceeds selection capacity";
        return;
    }
    text(ui->labels[i], label);
    ui->items[i].text = ui->labels[i];
    ui->items[i].disabled = disabled;
    ui->ids[i] = (RlU16)id;
    ui->list.count++;
}

static int done(const ScState *state, unsigned i) {
    return (state->completed[i / 8] >> (i % 8)) & 1;
}

static int open_node(const ScUi *ui, unsigned i) {
    if (i < ui->catalog->count) return sc_available(&ui->state, ui->catalog, i);
    return ui->state.phase == SC_ROUTE && !done(&ui->state, i) &&
        sc_graph_open(&ui->state, ui->catalog, i);
}

static void show(ScUi *ui, unsigned screen);

static void sync(ScUi *ui) {
    if (ui->state.phase == SC_LEARNING) {
        unsigned i;
        for (i = 0; i < ui->state.party_count; i++)
            if (ui->state.learn_mask & (1U << i)) break;
        ui->slot = i;
        show(ui, SC_UI_LEARN);
    } else if (ui->state.phase == SC_BATTLE) show(ui, SC_UI_BATTLE);
    else if (ui->state.phase == SC_REWARD) show(ui, SC_UI_REWARD);
    else if (ui->state.phase == SC_LOST || ui->state.phase == SC_FINISHED)
        show(ui, SC_UI_TERMINAL);
    else show(ui, SC_UI_REGIONS);
}

static void show(ScUi *ui, unsigned screen) {
    static const char *titles[] = {
        "Red / Blue Story", "Create campaign", "Regions", "Encounters",
        "Party (no PC)", "Party member", "Canon healing sites", "Choose variant",
        "Confirm choice", "Encounter reward", "Replace party member",
        "Choose donor move", "Choose recipient", "Replace move", "Learn move",
        "Evolution", "Battle service", "Campaign summary", "Save failed"
    };
    static const char *difficulty[] = {
        "Healing: Standard (reusable)", "Healing: Limited (once/site)", "Healing: Brutal (none)"
    };
    static const char *pools[] = {
        "Pool: canon trio", "Pool: unevolved random", "Pool: all species random"
    };
    static const char *starters[] = {
        "Starter: random", "Starter: Bulbasaur", "Starter: Charmander", "Starter: Squirtle"
    };
    unsigned i, j, count;
    char label[72], area[72];
    const RlMon *mon;
    ScState trial;
    int status;
    ui->screen = screen;
    ui->list.count = 0;
    ui->list.cursor = ui->list.scroll = 0;
    ui->list.visible = 7;
    ui->list.items = ui->items;
    ui->list.title = titles[screen];
    switch (screen) {
        case SC_UI_MENU:
            row(ui, "Continue campaign", 0, 0);
            row(ui, "New campaign", 1, 0);
            row(ui, "Return to mode menu", 2, 0);
            break;
        case SC_UI_CREATE:
            row(ui, ui->options.version ? "Version: Blue" : "Version: Red", 0, 0);
            row(ui, difficulty[ui->options.difficulty], 1, 0);
            row(ui, pools[ui->options.starter_pool], 2, 0);
            i = ui->options.starter == 1 ? 1 : ui->options.starter == 4 ? 2 :
                ui->options.starter == 7 ? 3 : 0;
            row(ui, starters[i], 3, 0);
            row(ui, "Seed: left/right to change", 4, 0);
            row(ui, "Create (locks healing preset)", 5, 0);
            break;
        case SC_UI_REGIONS:
            row(ui, "Party", 0, 0);
            row(ui, "Healing", 1, 0);
            row(ui, "Story actions", 2, 0);
            row(ui, "Save and return to campaign menu", 3, 0);
            row(ui, "Finish campaign (after Champion)", 4,
                !done(&ui->state, ui->catalog->graph->champion));
            for (i = 0; i < ui->catalog->count; i++) {
                if (!open_node(ui, i)) continue;
                region_name(area, ui->catalog->encounters[i].id);
                for (j = 5; j < (unsigned)ui->list.count; j++)
                    if (equal(area, ui->labels[j])) break;
                if (j == (unsigned)ui->list.count) row(ui, area, i + 5, 0);
            }
            break;
        case SC_UI_ENCOUNTERS:
            count = equal(ui->region, "Story actions") ?
                ui->catalog->graph->count : ui->catalog->count;
            for (i = 0; i < count; i++) {
                if (!open_node(ui, i)) continue;
                if (equal(ui->region, "Story actions")) {
                    if (i < ui->catalog->count) continue;
                } else {
                    region_name(area, ui->catalog->encounters[i].id);
                    if (!equal(area, ui->region)) continue;
                }
                row(ui, ui->catalog->graph->nodes[i].id, i, 0);
            }
            if (!ui->list.count) row(ui, "No available encounters", 0, 1);
            break;
        case SC_UI_PARTY: case SC_UI_REPLACE: case SC_UI_TARGET:
            for (i = 0; i < ui->state.party_count; i++) {
                mon_label(label, &ui->state.party[i]);
                row(ui, label, i, 0);
            }
            break;
        case SC_UI_MON:
            row(ui, "View moves / HP / PP / status", 0, 1);
            row(ui, "Evolution choices", 1, 0);
            break;
        case SC_UI_HEAL:
            for (i = 0; i < ui->catalog->graph->healing_count; i++) {
                trial = ui->state;
                row(ui, ui->catalog->graph->healing[i].id, i,
                    sc_location_heal(&trial, ui->catalog, i) != SC_OK);
            }
            break;
        case SC_UI_VARIANT:
            count = ui->catalog->graph->nodes[ui->node].variants;
            for (i = 0; i < count; i++) {
                text(label, "Choice 000");
                number(label + 7, i + 1);
                if (ui->node == ui->catalog->graph->fossil_action)
                    text(label, i ? "Dome fossil / Kabuto" : "Helix fossil / Omanyte");
                else if (ui->node < ui->catalog->count) {
                    text(label, "Offer No.000 L000");
                    number(label + 9, ui->catalog->encounters[ui->node].teams[i].mons[0].species);
                    number(label + 14, ui->catalog->encounters[ui->node].teams[i].mons[0].level);
                }
                row(ui, label, i, 0);
            }
            break;
        case SC_UI_CONFIRM:
            row(ui, "Cancel", 0, 0);
            row(ui, "Confirm", 1, 0);
            break;
        case SC_UI_REWARD:
            row(ui, ui->state.party_count == 6 ? "Recruit: replace one of six" : "Recruit into party", 0, 0);
            row(ui, "Transfer an actual donor move", 1, 0);
            row(ui, "Decline reward", 2, 0);
            break;
        case SC_UI_DONOR: case SC_UI_MOVE: case SC_UI_LEARN:
            mon = screen == SC_UI_DONOR ? &ui->state.offer : &ui->state.party[ui->slot];
            for (i = 0; i < 4; i++) {
                text(label, "Move 000 PP 000/000");
                number(label + 5, mon->moves[i]);
                number(label + 12, mon->pp[i]);
                number(label + 16, mon->max_pp[i]);
                row(ui, label, i, (screen == SC_UI_DONOR && !mon->moves[i])
#ifdef SC_STORY_PACKED_MOVES
                    || (screen == SC_UI_LEARN && !mon->moves[3] &&
                        (mon->moves[i] || (i && !mon->moves[i - 1])))
#endif
                    );
            }
            if (screen == SC_UI_LEARN) row(ui, "Decline learning", 4, 0);
            break;
        case SC_UI_EVOLVE:
            mon = &ui->state.party[ui->slot];
            count = ui->catalog->species[mon->species].evolution_count;
            for (i = 0; i < count; i++) {
                const ScEvolution *evo = &ui->catalog->species[mon->species].evolution[i];
                text(label, "Evolve to No.000");
                number(label + 13, evo->species);
                trial = ui->state;
                status = ui->postbattle_active && ui->state.phase == SC_BATTLE ?
                    sc_evolve_earned(&trial, ui->catalog, ui->slot, i, 1) :
                    sc_evolve_owned(&trial, ui->catalog, ui->slot, i, 1);
                row(ui, label, i, status != SC_OK);
            }
            if (!count) row(ui, "No evolution", 0, 1);
            if (ui->postbattle_active && ui->state.phase == SC_BATTLE)
                row(ui, "Continue without evolving", 255, 0);
            break;
        case SC_UI_BATTLE:
            row(ui, "Waiting for native battle service", 0, 1);
            break;
        case SC_UI_TERMINAL:
            row(ui, ui->state.phase == SC_LOST ? "Defeat ended this campaign" : "Champion campaign complete", 0, 1);
            row(ui, "Return to campaign menu", 1, 0);
            break;
        case SC_UI_SAVE_ERROR:
            row(ui, "Retry original Pak (choice retained)", 0, 0);
            row(ui, "Do not remove or change the Pak", 1, 1);
            break;
    }
}

static int save_pending(ScUi *ui) {
    int status = sc_storage_store(ui->device, ui->catalog, &ui->pending, ui->create_save);
    ui->storage_status = status;
    ui->message = sc_ui_save_text(status);
    if (status != SC_SAVE_OK) {
        ui->pending_save = 1;
        show(ui, SC_UI_SAVE_ERROR);
        return SC_UI_STAY;
    }
    ui->pending_save = 0;
    ui->state = ui->pending.state;
    ui->active = 1;
    if (ui->operation == SC_UI_NEW) ui->options = ui->state.options;
    sync(ui);
    if (ui->operation == SC_UI_SAVE_EXIT) show(ui, SC_UI_MENU);
    return SC_UI_STAY;
}

static int postbattle_advance(ScUi *ui) {
    int status;
    unsigned i, j;
    if (!ui || !ui->postbattle_active || ui->state.phase != SC_BATTLE)
        return SC_INVALID;
    for (;;) {
        for (i = 0; i < ui->state.party_count; i++) {
            if (!(ui->postbattle_evolution_mask & (1U << i))) continue;
            for (j = 0; j < ui->catalog->species[ui->state.party[i].species].evolution_count; j++) {
                ScState trial = ui->state;
                if (sc_evolve_earned(&trial, ui->catalog, i, j, 1) == SC_OK) {
                    ui->slot = i;
                    show(ui, SC_UI_EVOLVE);
                    return SC_OK;
                }
            }
            ui->postbattle_evolution_mask &= (RlU8)~(1U << i);
        }
        if (ui->postbattle_ledger.next >= ui->postbattle_ledger.count) break;
        {
            RlU8 old_levels[6];
            for (i = 0; i < ui->state.party_count; i++)
                old_levels[i] = ui->state.party[i].level;
            status = sc_ko_ledger_apply_next(&ui->postbattle_ledger, &ui->state,
                                             &ui->postbattle_combat);
            for (i = 0; i < ui->state.party_count; i++)
                if (ui->state.party[i].level > old_levels[i])
                    ui->postbattle_evolution_mask |= (RlU8)(1U << i);
        }
        if (status == SC_PENDING && ui->state.phase == SC_LEARNING) {
            sync(ui);
            return SC_OK;
        }
        if (status != SC_OK && status != SC_PENDING) return status;
    }
    status = sc_finish(&ui->state, ui->catalog, SC_WIN, ui->state.encounter_variant);
    if (status != SC_OK) return status;
    ui->postbattle_active = 0;
    ui->postbattle_evolution_mask = 0;
    sc_ko_ledger_discard(&ui->postbattle_ledger);
    ui->pending.state = ui->state;
    ui->pending.interrupted = 0;
    ui->create_save = 0;
    ui->operation = SC_UI_DECLINE;
    save_pending(ui);
    return ui->pending_save ? SC_PENDING : SC_OK;
}

static void confirm(ScUi *ui, unsigned operation, const char *message) {
    ui->back = ui->screen;
    ui->operation = operation;
    ui->message = message;
    show(ui, SC_UI_CONFIRM);
}

static int execute(ScUi *ui) {
    int status = SC_INVALID;
    unsigned prior_species;
    ui->pending.state = ui->state;
    ui->pending.interrupted = 0;
    ui->create_save = 0;
    switch (ui->operation) {
        case SC_UI_NEW:
            status = sc_new(&ui->pending.state, ui->catalog, &ui->options, ui->seed);
            ui->create_save = 1;
            break;
        case SC_UI_START:
            status = sc_bridge_begin(&ui->state, &ui->bridge, ui->catalog,
                ui->device, ui->node, ui->variant, &ui->storage_status);
            if (status != SC_OK) {
                show(ui, ui->back);
                if (ui->storage_status != SC_SAVE_OK) {
                    ui->message = sc_ui_save_text(ui->storage_status);
                    ui->status = status;
                    return SC_UI_STAY;
                }
                return report(ui, status);
            }
            ui->operation_id++;
            if (!ui->operation_id) ui->operation_id++;
            ui->request_ready = 1;
            sync(ui);
            return SC_UI_REQUEST;
        case SC_UI_ACTION:
            status = sc_action(&ui->pending.state, ui->catalog, ui->node, ui->variant,
                ui->catalog->graph->nodes[ui->node].destination_warp);
            break;
        case SC_UI_GIFT:
            status = sc_begin(&ui->pending.state, ui->catalog, ui->node, ui->variant);
            break;
        case SC_UI_RECRUIT:
            status = sc_recruit(&ui->pending.state, ui->catalog, ui->slot, 1);
            break;
        case SC_UI_TRANSFER:
            status = sc_transfer(&ui->pending.state, ui->catalog, ui->donor, ui->slot, ui->move, 1);
            break;
        case SC_UI_DECLINE:
            status = sc_decline(&ui->pending.state, ui->catalog);
            break;
        case SC_UI_LEARN_MOVE:
            status = sc_learn(&ui->pending.state, ui->catalog, ui->slot,
                ui->move < 4 ? ui->move : 0, ui->move < 4);
            break;
        case SC_UI_EVOLUTION:
            if (ui->postbattle_active && ui->state.phase == SC_BATTLE) {
                prior_species = ui->pending.state.party[ui->slot].species;
                status = sc_evolve_earned(&ui->pending.state, ui->catalog, ui->slot,
                                           ui->evolution, 1);
                if (status == SC_OK) {
                    if (sc_ko_ledger_follow_evolution(&ui->postbattle_ledger,
                            &ui->postbattle_combat, &ui->pending.state, ui->slot,
                            prior_species) != SC_OK) {
                        status = SC_INVALID;
                        break;
                    }
                    ui->postbattle_combat.party_species[ui->slot] =
                        ui->catalog->species[ui->pending.state.party[ui->slot].species];
                    ui->postbattle_evolution_mask &= (RlU8)~(1U << ui->slot);
                }
            } else status = sc_evolve_owned(&ui->pending.state, ui->catalog, ui->slot,
                                             ui->evolution, 1);
            break;
        case SC_UI_EVOLUTION_SKIP:
            if (!ui->postbattle_active || ui->state.phase != SC_BATTLE ||
                !(ui->postbattle_evolution_mask & (1U << ui->slot))) {
                status = SC_INVALID;
                break;
            }
            ui->pending.state.evolution_mask &= (RlU8)~(1U << ui->slot);
            ui->postbattle_evolution_mask &= (RlU8)~(1U << ui->slot);
            status = SC_OK;
            break;
        case SC_UI_HEAL_SITE:
            status = sc_location_heal(&ui->pending.state, ui->catalog, ui->node);
            break;
        case SC_UI_END:
            if (ui->state.phase == SC_ROUTE && done(&ui->state, ui->catalog->graph->champion)) {
                ui->pending.state.phase = SC_FINISHED;
                status = SC_OK;
            }
            break;
    }
    if (status != SC_OK) {
        show(ui, ui->back);
        return report(ui, status);
    }
    ui->status = SC_OK;
    if (ui->operation == SC_UI_NEW) ui->request_ready = 0;
    if (ui->postbattle_active && ui->pending.state.phase == SC_BATTLE) {
        ui->state = ui->pending.state;
        status = postbattle_advance(ui);
        if (status != SC_OK && status != SC_PENDING) return report(ui, status);
        return SC_UI_STAY;
    }
    /* Learning is a paused battle transaction, not a serializable checkpoint. */
    if (ui->pending.state.phase == SC_LEARNING || ui->pending.state.phase == SC_BATTLE) {
        ui->state = ui->pending.state;
        sync(ui);
        return SC_UI_STAY;
    }
    return save_pending(ui);
}

int sc_ui_init(ScUi *ui, const ScCatalog *catalog, const ScSaveDevice *device, RlU32 seed) {
    unsigned i;
    unsigned char *bytes;
    if (!ui) return SC_INVALID;
    bytes = (unsigned char *)ui;
    for (i = 0; i < sizeof(*ui); i++) bytes[i] = 0;
    ui->catalog = catalog;
    ui->device = device;
    ui->seed = seed;
    if (!sc_catalog_valid(catalog) || !catalog->graph) return SC_INVALID;
    if (!catalog->graph_reviewed) return SC_UNREVIEWED;
    show(ui, SC_UI_MENU);
    return SC_OK;
}

int sc_ui_unsupported(ScUi *ui) {
    if (!ui) return SC_UI_STAY;
    ui->status = SC_UNAVAILABLE;
    ui->message = "Not supported: no PC, grinding, shops, NPC trades or items";
    return SC_UI_STAY;
}

/* Private scratch keeps the Game_Thread stack bounded; nested use is refused. */
static ScSnapshot input_loaded;
static int input_busy;

static int input_body(ScUi *ui, unsigned pressed) {
    int choice, status, cursor;
    unsigned id;
    ScSnapshot *loaded = &input_loaded;
    if (!ui || !ui->catalog || !ui->catalog->graph || !ui->list.title) return SC_UI_STAY;
    if (ui->screen == SC_UI_CREATE && (pressed & (RL_UI_LEFT | RL_UI_RIGHT)) &&
        ui->list.cursor == 4) {
        if (pressed & RL_UI_RIGHT) ui->seed++;
        else ui->seed--;
        return SC_UI_STAY;
    }
    choice = rl_ui_list_step(&ui->list, pressed);
    if (choice == RL_UI_NONE) return SC_UI_STAY;
    if (choice == RL_UI_DISABLED) return report(ui, SC_UNAVAILABLE);
    if (choice == RL_UI_INVALID) return report(ui, SC_INVALID);
    if (choice == RL_UI_CANCEL) {
        switch (ui->screen) {
            case SC_UI_MENU: return SC_UI_EXIT;
            case SC_UI_CREATE: case SC_UI_TERMINAL: show(ui, SC_UI_MENU); break;
            case SC_UI_CONFIRM: show(ui, ui->back); break;
            case SC_UI_ENCOUNTERS: case SC_UI_PARTY: case SC_UI_HEAL: show(ui, SC_UI_REGIONS); break;
            case SC_UI_MON: show(ui, SC_UI_PARTY); break;
            case SC_UI_EVOLVE:
                if (ui->postbattle_active && ui->state.phase == SC_BATTLE)
                    return report(ui, SC_PENDING);
                show(ui, SC_UI_MON);
                break;
            case SC_UI_VARIANT: show(ui, SC_UI_ENCOUNTERS); break;
            case SC_UI_REPLACE: case SC_UI_DONOR: case SC_UI_TARGET: show(ui, SC_UI_REWARD); break;
            case SC_UI_MOVE: show(ui, SC_UI_TARGET); break;
            default: return report(ui, SC_PENDING);
        }
        return SC_UI_STAY;
    }
    id = ui->ids[choice];
    switch (ui->screen) {
        case SC_UI_MENU:
            if (id == 2) return SC_UI_EXIT;
            if (id == 1) { show(ui, SC_UI_CREATE); break; }
            status = sc_storage_load(ui->device, ui->catalog, loaded);
            ui->storage_status = status;
            ui->message = sc_ui_save_text(status);
            if (status != SC_SAVE_OK && status != SC_SAVE_RECOVERED) break;
            ui->state = loaded->state;
            ui->request_ready = 0;
            if (loaded->interrupted) ui->state.phase = SC_LOST;
            ui->options = loaded->state.options;
            ui->active = 1;
            sync(ui);
            if (loaded->wire_version == 1)
                ui->message = "Legacy save loaded; next save upgrades format. Stone pickups remain unclaimed.";
            if (loaded->interrupted) ui->message = "Interrupted battle checkpoint: campaign ended";
            break;
        case SC_UI_CREATE:
            cursor = ui->list.cursor;
            if (id == 0) ui->options.version ^= 1;
            else if (id == 1) ui->options.difficulty = (RlU8)((ui->options.difficulty + 1) % 3);
            else if (id == 2) {
                ui->options.starter_pool = (RlU8)((ui->options.starter_pool + 1) % 3);
                ui->options.starter = 0;
            } else if (id == 3) {
                ui->options.starter = ui->options.starter == 0 ? 1 :
                    ui->options.starter == 1 ? 4 : ui->options.starter == 4 ? 7 : 0;
                if (ui->options.starter) ui->options.starter_pool = SC_STARTER_TRIO;
            } else if (id == 5) {
                confirm(ui, SC_UI_NEW, "Replace existing campaign save? Healing preset locks at creation.");
                break;
            }
            show(ui, SC_UI_CREATE);
            ui->list.cursor = cursor;
            break;
        case SC_UI_REGIONS:
            if (id == 0) show(ui, SC_UI_PARTY);
            else if (id == 1) show(ui, SC_UI_HEAL);
            else if (id == 2) { text(ui->region, "Story actions"); show(ui, SC_UI_ENCOUNTERS); }
            else if (id == 3) {
                ui->pending.state = ui->state; ui->pending.interrupted = 0;
                ui->create_save = 0; ui->operation = SC_UI_SAVE_EXIT;
                save_pending(ui);
                if (!ui->pending_save) show(ui, SC_UI_MENU);
            } else if (id == 4) confirm(ui, SC_UI_END, "End after Champion? This closes optional postgame encounters.");
            else { region_name(ui->region, ui->catalog->encounters[id - 5].id); show(ui, SC_UI_ENCOUNTERS); }
            break;
        case SC_UI_ENCOUNTERS:
            ui->node = id;
            ui->variant = 0;
            if (id < ui->catalog->count && ui->catalog->encounters[id].kind == SC_RIVAL)
                ui->variant = ui->state.rival_branch;
            else if (id == ui->catalog->graph->fossil_offer)
                ui->variant = ui->state.fossil_choice;
            else if (ui->catalog->graph->nodes[id].variants > 1) { show(ui, SC_UI_VARIANT); break; }
            confirm(ui, id >= ui->catalog->count ? SC_UI_ACTION :
                ui->catalog->encounters[id].kind == SC_GIFT ? SC_UI_GIFT : SC_UI_START,
                ui->catalog->graph->nodes[id].destination_warp ?
                "Depart ship? Remaining ship trainers become unavailable." : "Begin this canon encounter / story action?");
            break;
        case SC_UI_VARIANT:
            ui->variant = id;
            confirm(ui, ui->node >= ui->catalog->count ? SC_UI_ACTION :
                ui->catalog->encounters[ui->node].kind == SC_GIFT ? SC_UI_GIFT : SC_UI_START,
                "Lock this encounter choice?");
            break;
        case SC_UI_CONFIRM:
            if (!id) show(ui, ui->back);
            else return execute(ui);
            break;
        case SC_UI_PARTY: ui->slot = id; show(ui, SC_UI_MON); break;
        case SC_UI_MON: show(ui, SC_UI_EVOLVE); break;
        case SC_UI_HEAL:
            ui->node = id;
            confirm(ui, SC_UI_HEAL_SITE, "Heal HP / PP / status at this site? Limited consumes its one use.");
            break;
        case SC_UI_REWARD:
            if (id == 0) {
                if (ui->state.party_count == 6) show(ui, SC_UI_REPLACE);
                else { ui->slot = ui->state.party_count; confirm(ui, SC_UI_RECRUIT, "Recruit this offered Pokemon?"); }
            } else if (id == 1) show(ui, SC_UI_DONOR);
            else confirm(ui, SC_UI_DECLINE, "Permanently decline this offer?");
            break;
        case SC_UI_REPLACE:
            ui->slot = id;
            confirm(ui, SC_UI_RECRUIT, "Replace this member permanently? There is no PC.");
            break;
        case SC_UI_DONOR: ui->donor = id; show(ui, SC_UI_TARGET); break;
        case SC_UI_TARGET: ui->slot = id; show(ui, SC_UI_MOVE); break;
        case SC_UI_MOVE:
            ui->move = id;
            confirm(ui, SC_UI_TRANSFER, "Replace recipient move with donor's actual move? No compatibility restriction.");
            break;
        case SC_UI_LEARN:
            ui->move = id;
#ifdef SC_STORY_PACKED_MOVES
            confirm(ui, SC_UI_LEARN_MOVE, id == 4 ? "Decline this learned move?" :
                ui->state.party[ui->slot].moves[id] ?
                "Replace this move with pending level-up move?" : "Learn in the first empty move slot?");
#else
            confirm(ui, SC_UI_LEARN_MOVE, id == 4 ? "Decline this learned move?" : "Replace this move with pending level-up move?");
#endif
            break;
        case SC_UI_EVOLVE:
            if (id == 255 && ui->postbattle_active && ui->state.phase == SC_BATTLE)
                confirm(ui, SC_UI_EVOLUTION_SKIP, "Skip this level-up evolution?");
            else {
                ui->evolution = id;
                confirm(ui, SC_UI_EVOLUTION, "Evolve this member? HP / PP / status persist.");
            }
            break;
        case SC_UI_TERMINAL: show(ui, SC_UI_MENU); break;
        case SC_UI_SAVE_ERROR: return save_pending(ui);
        default: return report(ui, SC_PENDING);
    }
    return SC_UI_STAY;
}

int sc_ui_input(ScUi *ui, unsigned pressed) {
    int result;
    if (!ui) return SC_UI_STAY;
    if (input_busy) return report(ui, SC_INVALID);
    input_busy = 1;
    result = input_body(ui, pressed);
    input_busy = 0;
    return result;
}

int sc_ui_battle_request(const ScUi *ui, ScUiBattleRequest *request) {
    unsigned i, count;
    unsigned char *bytes;
    if (!ui || !request || !ui->request_ready || ui->state.phase != SC_BATTLE) return SC_INVALID;
    bytes = (unsigned char *)request;
    for (i = 0; i < sizeof(*request); i++) bytes[i] = 0;
    if (sc_team(&ui->state, ui->catalog, ui->state.encounter, ui->state.encounter_variant,
        request->enemy, &count) != SC_OK ||
        sc_combat_prepare(&request->progression, &ui->state, ui->catalog) != SC_OK) return SC_INVALID;
    for (i = 0; i < ui->state.party_count; i++) request->party[i] = ui->state.party[i];
    request->party_count = ui->state.party_count;
    request->enemy_count = (RlU8)count;
    request->encounter = ui->state.encounter;
    request->variant = ui->state.encounter_variant;
    request->operation_id = ui->operation_id;
    request->progression.operation_id = ui->operation_id;
    return SC_OK;
}

int sc_ui_observe(ScUi *ui, RlU32 operation_id, const ScBattleSnapshot party[6], unsigned active_slot,
                  unsigned enemy_slot, unsigned enemy_hp) {
    int status;
    if (!ui || !ui->request_ready || ui->pending_save || operation_id != ui->operation_id) {
        if (ui) report(ui, SC_INVALID);
        return SC_INVALID;
    }
    status = sc_bridge_observe(&ui->state, &ui->bridge, ui->catalog, party,
        active_slot, enemy_slot, enemy_hp);
    if (status == SC_OK) sync(ui);
    else report(ui, status);
    return status;
}

int sc_ui_finish(ScUi *ui, RlU32 operation_id, unsigned outcome) {
    int status;
    if (!ui || !ui->request_ready || ui->pending_save || operation_id != ui->operation_id) {
        if (ui) report(ui, SC_INVALID);
        return SC_INVALID;
    }
    ui->pending.state = ui->state;
    ui->pending.interrupted = 0;
    status = sc_finish(&ui->pending.state, ui->catalog, outcome, ui->state.encounter_variant);
    if (status != SC_OK) { report(ui, status); return status; }
    ui->request_ready = 0;
    ui->create_save = 0;
    ui->operation = SC_UI_DECLINE;
    save_pending(ui);
    return ui->pending_save ? SC_PENDING : SC_OK;
}

int sc_ui_resume_postbattle(ScUi *ui, const ScUiBattleRequest *request,
                            const ScState *battle_state, const RlLaunchResult *result,
                            const ScKoLedger *ledger, unsigned outcome) {
    ScState next;
    RlU8 returned = 0;
    unsigned i, j, slot;
    if (!ui || !ui->catalog || !request || !battle_state || !result || !ledger ||
        ui->pending_save || ui->request_ready || battle_state->phase != SC_BATTLE ||
        battle_state->party_count < 1 || battle_state->party_count > 6 ||
        request->party_count != battle_state->party_count ||
        request->enemy_count < 1 || request->enemy_count > 6 ||
        request->encounter != battle_state->encounter ||
        request->variant != battle_state->encounter_variant ||
        request->operation_id == 0 || result->player_count > request->party_count ||
        result->enemy_count != request->enemy_count || outcome > SC_DRAW ||
        ledger->count > SC_KO_LEDGER_CAPACITY || ledger->next != 0 ||
        !ledger->bound || ledger->operation_id != request->operation_id ||
        ledger->encounter != request->encounter || ledger->variant != request->variant ||
        ledger->party_count != request->party_count ||
        ledger->enemy_count != request->enemy_count)
        return SC_INVALID;
    for (i = 0; i < request->party_count; i++)
        if (request->party[i].species != battle_state->party[i].species ||
            ledger->party_ids[i] != battle_state->party[i].species)
            return SC_INVALID;
    next = *battle_state;
    if (outcome == SC_WIN) {
        if (result->outcome != RL_BATTLE_WIN) return SC_INVALID;
        for (i = 0; i < result->player_count; i++) {
            slot = result->player_index[i];
            if (slot >= next.party_count || (returned & (1U << slot)) ||
                result->player[i].species != next.party[slot].species ||
                result->player[i].cur_hp > next.party[slot].max_hp ||
                result->player[i].moves[0] != next.party[slot].moves[0] ||
                result->player[i].moves[1] != next.party[slot].moves[1] ||
                result->player[i].moves[2] != next.party[slot].moves[2] ||
                result->player[i].moves[3] != next.party[slot].moves[3])
                return SC_INVALID;
            for (j = 0; j < 4; j++)
                if (result->player[i].pp[j] > next.party[slot].max_pp[j])
                    return SC_INVALID;
            next.party[slot].cur_hp = result->player[i].cur_hp;
            next.party[slot].fainted = result->player[i].cur_hp == 0;
            next.party[slot].status = result->player[i].status;
            for (j = 0; j < 4; j++) next.party[slot].pp[j] = result->player[i].pp[j];
            returned |= (RlU8)(1U << slot);
        }
        if (ledger->enemy_count != request->enemy_count ||
            ledger->party_count != request->party_count ||
            ledger->count != request->enemy_count ||
            request->progression.operation_id != request->operation_id)
            return SC_INVALID;
        ui->postbattle_combat = request->progression;
        ui->postbattle_ledger = *ledger;
        ui->postbattle_active = 1;
    } else {
        if ((outcome == SC_LOSS && result->outcome != RL_BATTLE_LOSS) ||
            (outcome == SC_DRAW && result->outcome != RL_LAUNCH_DRAW))
            return SC_INVALID;
        ui->operation_id = request->operation_id;
        sc_ko_ledger_discard(&ui->postbattle_ledger);
        ui->postbattle_active = 0;
        ui->postbattle_evolution_mask = 0;
        next.phase = SC_LOST;
        ui->state = next;
        ui->postbattle_evolution_mask = 0;
        ui->request_ready = 0;
        ui->active = 1;
        ui->status = SC_OK;
        ui->message = "Battle lost: this campaign has ended";
        show(ui, SC_UI_TERMINAL);
        return SC_OK;
    }
    for (i = 0; i < request->party_count; i++)
        if (ui->postbattle_combat.party_ids[i] != next.party[i].species)
            return SC_INVALID;
    ui->operation_id = request->operation_id;
    ui->state = next;
    for (i = 0; i < 6; i++) ui->bridge.participants[i] = 0;
    ui->bridge.enemy_count = request->enemy_count;
    ui->request_ready = 0;
    ui->active = 1;
    ui->status = SC_OK;
    return postbattle_advance(ui);
}

#ifdef SC_SCRATCH_TEST
void sc_scratch_test_hold_input(int held) { input_busy = held; }
int sc_scratch_test_busy_input(void) { return input_busy; }
#endif
