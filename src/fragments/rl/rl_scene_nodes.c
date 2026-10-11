#ifdef ROGUELIKE
#include "rl_scene_nodes.h"
#include "rl_ui.h"
#include "src/roguelike/rl_battle.h"
#include "src/2E460.h"
#include "src/232C0.h"

#define NODE_ITEMS 32
#define NODE_RELICS_TRIGGER 0x0010

enum { NODE_ROOT, NODE_TARGET, NODE_MOVE, NODE_PARTY_ACTION,
       NODE_SWAP, NODE_RELEASE, NODE_DONE, NODE_ERROR, NODE_RELICS };

static RunState* sRun;
static const RlPool* sRentals;
static const RlMeta* sMeta;
static unsigned sMode, sPage, sItem, sSlot, sChoice, sEvent;
RlU32 gRlSceneEvent;
static unsigned sShopCount, sWildCount;
static int sReturn;
static const char* sMessage;
static RlUiList sList;
static RlUiItem sItems[NODE_ITEMS];
static char sLabels[NODE_ITEMS][48];
static unsigned sValues[NODE_ITEMS];
static RlShopEntry sShop[12];
static RlMon sWild[3];
static RlU8 sMoves[165];
static RlMovePool sMovePool;
static const Color_RGBA8 sWhite = { 255, 255, 255, 255 };
static const Color_RGBA8 sGold = { 255, 224, 96, 255 };
static const Color_RGBA8 sRed = { 255, 144, 144, 255 };
static const char* sItemNames[RL_ITEM_COUNT] = {
    "Potion", "Super Potion", "Hyper Potion", "Full Restore", "Revive",
    "Ether", "Elixir", "Full Heal", "Rare Candy", "Vitamin",
    "Relic", "TM", "Money"
};
static const char* sDescriptions[RL_EVENT_COUNT] = {
    "Ancient fossils await a new trainer.",
    "Risk 100 coins. Win 250 or lose the bet.",
    "Deposit one. Returns after two nodes +5 Lv.",
    "Trade one for a rental five levels higher.",
    "Exchange one for a random rental.",
    "Lose 1 HP per living mon; gain Ghost relic.",
    "Choose one of three wild rentals.",
    "Take a candy or heal living Pokemon 50%.",
    "Discover a random unowned relic.",
    "Heal living Pokemon 50% or take 200 coins.",
    "Receive a legal rental move TM for your bag.",
    "Gain Cursed Power: stronger, but fragile."
};

static void node_copy(char* out, const char* text) {
    unsigned i;
    for (i = 0; i < 47 && text[i]; i++) out[i] = text[i];
    out[i] = 0;
}

static void node_number(char* out, unsigned value, unsigned digits) {
    while (digits) {
        digits--;
        out[digits] = (char)('0' + value % 10);
        value /= 10;
    }
}

static void node_mon_label(char* out, const RlMon* mon, unsigned slot) {
    unsigned length;
    node_number(out, slot + 1, 1);
    out[1] = ' ';
    node_copy(out + 2, rl_ui_species_name(mon->species));
    if (mon->fainted || !mon->cur_hp) {
        for (length = 0; out[length]; length++) {}
        out[length++] = '!';
        out[length] = 0;
    }
}

/* Content identities must survive opening Bag, purchases, and suspend/reentry. */
static RlU32 node_seed(unsigned salt) {
    RlU32 seed;
    seed = sRun->seed ^ salt ^ ((RlU32)sRun->act << 24) ^
           ((RlU32)sRun->row << 12) ^ ((RlU32)sRun->lane << 4);
    seed ^= seed >> 16;
    seed *= 0x7FEB352DU;
    seed ^= seed >> 15;
    seed *= 0x846CA68BU;
    seed ^= seed >> 16;
    return seed ? seed : 1;
}

/* Resident TM map/flags used by fragment24, with fragment62's Yellow additions. */
static int node_can_learn(const RlMon* mon, RlU8 move, RlU8* pp, void* context) {
    unk_D_80072B00* data;
    const u8* flags;
    unsigned i, bits;
    (void)context;
    data = func_8002ED40(move);
    if (!mon || !mon->species || mon->species > 151 || !data || !data->unk_05)
        return 0;
    flags = (const u8*)&D_80070FA0[mon->species - 1] + 0x0F;
    for (i = 0; i < 55; i++) {
        if (func_80022A28(i) != move) continue;
        bits = flags[i / 8];
        if (i / 8 == 6) {
            switch (mon->species) {
            case 6: bits |= 8; break;
            case 12: case 48: case 49: bits |= 0x40; break;
            case 50: case 51: case 141: bits |= 4; break;
            }
        }
        if (bits & (1U << (i % 8))) {
            *pp = data->unk_05;
            return 1;
        }
    }
    return 0;
}

static void node_move_pool(void) {
    unsigned i;
    RlU8 move;
    sMovePool.moves = sMoves;
    sMovePool.count = 0;
    for (i = 0; i < 50; i++) {
        move = func_80022A28(i);
        if (move && move <= 165) sMoves[sMovePool.count++] = move;
    }
}

static void node_add(unsigned index, const char* text, unsigned value) {
    sItems[index].text = text;
    sItems[index].disabled = 0;
    sValues[index] = value;
}

static void node_list(const char* title, unsigned count, unsigned page) {
    sPage = page;
    rl_ui_list_init(&sList, title, sItems, count, count < 5 ? count : 5);
}

static void node_error(int result) {
    switch (result) {
    case RL_FULL: sMessage = "Full: release a Pokemon or free bag space."; break;
    case RL_EMPTY: sMessage = "No item available."; break;
    case RL_FUNDS: sMessage = "Not enough coins."; break;
    default: sMessage = "Unavailable for this Pokemon or already owned."; break;
    }
}

static void node_done(const char* message) {
    sMessage = message;
    sReturn = RL_SCENE_COMPLETED;
    node_add(0, "Continue", 0);
    node_list("Node resolved", 1, NODE_DONE);
}

static void node_root(void);

/* Resolve only on a private copy, with the same stream used on confirmation. */
static int node_event_mon(unsigned choice, unsigned slot, RlMon* out) {
    RunState copy;
    unsigned index;
    if (sEvent != 0 && sEvent != 3 && sEvent != 4) return 0;
    copy = *sRun;
    copy.rng = node_seed(0x45564E54U);
    if (sEvent == 0) copy.party_count = 0;
    if (rl_event_resolve_unlocked(&copy, sEvent, choice, slot, sRentals,
                                 &sMovePool, rl_meta_permitted_relics(sMeta)) != RL_OK)
        return 0;
    index = sEvent == 0 ? 0 : slot;
    *out = copy.party[index];
    return 1;
}

static void node_party(const char* title, unsigned page) {
    unsigned i;
    for (i = 0; i < sRun->party_count; i++) {
        node_mon_label(sLabels[i], &sRun->party[i], i);
        node_add(i, sLabels[i], i);
    }
    node_add(i, "Back", RL_NO_SLOT);
    node_list(title, i + 1, page);
}

static void node_bag(void) {
    unsigned i, n, length;
    n = 0;
    for (i = 0; i < RL_ITEM_COUNT; i++) {
        if (!sRun->bag[i] || i == RL_TM || i == RL_RELIC_ITEM) continue;
        node_copy(sLabels[n], sItemNames[i]);
        for (length = 0; sLabels[n][length]; length++) {}
        sLabels[n][length++] = ' ';
        sLabels[n][length++] = 'x';
        node_number(sLabels[n] + length, sRun->bag[i], 3);
        sLabels[n][length + 3] = 0;
        node_add(n, sLabels[n], i);
        n++;
    }
    for (i = 0; i < sRun->tm_count && n < NODE_ITEMS - 1; i++) {
        node_copy(sLabels[n], "TM move 000");
        node_number(sLabels[n] + 8, sRun->tm_moves[i], 3);
        node_add(n, sLabels[n], RL_ITEM_COUNT + i);
        n++;
    }
    node_add(n, "Back", RL_NO_SLOT);
    node_list("Bag: choose an item", n + 1, NODE_ROOT);
}

static int node_relic_is_one_use(unsigned relic) {
    return relic == RL_OLD_AMBER || relic == RL_MASTER_BALL;
}

static void node_relics(void) {
    unsigned i, n;
    char* label;
    n = 0;
    for (i = 0; i < RL_RELIC_COUNT; i++) {
        if (!(sRun->relics & (1U << i))) continue;
        label = sLabels[n];
        node_copy(label, rl_relic_defs[i].name);
        if (node_relic_is_one_use(i) && (sRun->consumed_relics & (1U << i))) {
            unsigned length;
            for (length = 0; label[length]; length++) {}
            node_copy(label + length, " (used)");
        }
        node_add(n, label, i);
        n++;
    }
    if (!n) {
        node_add(n, "No relics owned", RL_NO_SLOT);
        sItems[n].disabled = 1;
        n++;
    }
    node_add(n, "Back", RL_NO_SLOT);
    node_list("Owned relics", n + 1, NODE_RELICS);
}

static void node_root(void) {
    unsigned i, n;
    RlMon preview;
    const char* title;
    n = 0;
    sChoice = 0;
    title = "Run node";
    if (sMode == RL_SCENE_BAG) {
        node_bag();
        return;
    }
    if (sMode == RL_SCENE_PARTY) {
        node_party("Party: select a Pokemon", NODE_ROOT);
        return;
    }
    switch (sMode) {
    case RL_MART:
        title = "Poke Mart";
        for (i = 0; i < sShopCount; i++) {
            node_copy(sLabels[n], sItemNames[sShop[i].item]);
            if (sShop[i].item == RL_TM) {
                node_copy(sLabels[n], "TM move 000");
                node_number(sLabels[n] + 8, sShop[i].detail, 3);
            } else if (sShop[i].item == RL_RELIC_ITEM &&
                       sShop[i].detail < RL_RELIC_COUNT) {
                node_copy(sLabels[n], rl_relic_defs[sShop[i].detail].name);
            } else if (sShop[i].item == RL_RELIC_ITEM) {
                node_copy(sLabels[n], "Relic unavailable");
            }
            node_add(n, sLabels[n], i);
            if (sShop[i].item == RL_RELIC_ITEM &&
                (sShop[i].detail >= RL_RELIC_COUNT ||
                 sShop[i].detail == RL_OLD_AMBER ||
                 sShop[i].detail == RL_MASTER_BALL ||
                 (sRun->relics & (1U << sShop[i].detail))))
                sItems[n].disabled = 1;
            n++;
        }
        break;
    case RL_CENTER:
        title = "Pokemon Center";
        node_add(n++, "Rest: heal all living 50%", 0);
        node_add(n++, "Revive one fainted Pokemon", 1);
        break;
    case RL_EVENT:
        title = rl_events[sEvent].name;
        if (sEvent == 6) {
            for (i = 0; i < sWildCount; i++) {
                node_mon_label(sLabels[n], &sWild[i], i);
                node_add(n, sLabels[n], i);
                n++;
            }
        } else if (sEvent == 0) {
            for (i = 0; i < 2; i++) {
                node_number(sLabels[n], i + 1, 1);
                sLabels[n][1] = ' ';
                node_copy(sLabels[n] + 2, rl_ui_species_name(i ? 138 : 140));
                node_add(n, sLabels[n], i);
                n++;
            }
        } else if ((sEvent == 3 || sEvent == 4) && node_event_mon(0, 0, &preview)) {
            node_copy(sLabels[n], "Receive ");
            node_copy(sLabels[n] + 8, rl_ui_species_name(preview.species));
            node_add(n, sLabels[n], 0);
            n++;
            node_add(n++, rl_events[sEvent].choices[1], 1);
        } else {
            node_add(n++, rl_events[sEvent].choices[0], 0);
            node_add(n++, rl_events[sEvent].choices[1], 1);
        }
        break;
    case RL_RECRUIT:
        title = "Recruit: choose one rental";
        for (i = 0; i < sWildCount; i++) {
            node_mon_label(sLabels[n], &sWild[i], i);
            node_add(n, sLabels[n], i);
            n++;
        }
        break;
    }
    node_add(n++, "Open Bag", 100);
    node_add(n++, "Manage Party", 101);
    node_add(n++, "Leave node", RL_NO_SLOT);
    node_list(title, n, NODE_ROOT);
}

static int node_event(unsigned choice, unsigned slot) {
    RunState copy;
    RlU32 rng;
    int result;
    copy = *sRun;
    rng = copy.rng;
    copy.rng = node_seed(0x45564E54U);
    if (sEvent == 7 && choice == 0 && copy.bag[RL_CANDY] >= 999)
        return RL_FULL;
    result = rl_event_resolve_unlocked(
        &copy, sEvent, choice, slot, sRentals, &sMovePool,
        rl_scene_available_relics(rl_meta_permitted_relics(sMeta)));
    if (result == RL_OK) {
        if (!(sRun->relics & (1U << RL_PP_POUCH)) &&
            (copy.relics & (1U << RL_PP_POUCH)) &&
            rl_battle_apply_pp_pouch(&copy) < 0)
            return RL_INVALID;
        copy.rng = rng;
        *sRun = copy;
    }
    return result;
}

void rl_scene_nodes_begin(RunState* run, const RlPool* rentals, unsigned mode,
                          const RlMeta* meta) {
    RunState copy;
    RlU8 event;
    RlU32 event_rng;
    sRun = run;
    sRentals = rentals;
    sMeta = meta;
    sMode = mode;
    sMessage = NULL;
    sReturn = RL_SCENE_ONGOING;
    sShopCount = sWildCount = 0;
    sItem = sSlot = sChoice = 0;
    if (!run || !meta || run->party_count > RL_PARTY || run->tm_count > RL_TM_CAPACITY ||
        (mode != RL_CENTER && mode != RL_MART && mode != RL_EVENT &&
         mode != RL_RECRUIT && mode != RL_SCENE_BAG && mode != RL_SCENE_PARTY)) {
        sMessage = "Invalid node scene request.";
        node_add(0, "Return", 0);
        node_list("Scene error", 1, NODE_ERROR);
        return;
    }
    node_move_pool();
    copy = *run;
    copy.rng = node_seed(0x53484F50U);
    /* Owned relics affect purchase validity, not the identity of this node's offer. */
    copy.relics = 0;
    sShopCount = rl_shop_generate_unlocked(&copy, &sMovePool,
        rl_scene_available_relics(rl_meta_permitted_relics(meta)), sShop, 12);
    copy = *run;
    copy.rng = node_seed(0x45564E54U);
    event_rng = node_seed(0x45564944U);
    sEvent = 0;
    if (mode == RL_EVENT && rl_meta_choose_event(meta, &event_rng, &event) != RL_OK) {
        sMessage = "Profile has no unlocked events.";
        node_add(0, "Return", 0);
        node_list("Scene error", 1, NODE_ERROR);
        return;
    }
    if (mode == RL_EVENT) sEvent = event;
    gRlSceneEvent = sEvent;
    sWildCount = rl_event_wild_options(&copy, rentals, sWild, 3);
    node_root();
    if ((mode == RL_RECRUIT || (mode == RL_EVENT && sEvent == 6)) && !sWildCount)
        sMessage = "Canonical rental pool unavailable. No recruit.";
    if (mode == RL_EVENT && sEvent == 0 &&
        (!rentals || !rentals->mons || !rentals->count))
        sMessage = "Fossil rental data unavailable.";
}

static void node_moves(void) {
    unsigned i;
    const RlMon* mon;
    mon = &sRun->party[sSlot];
    for (i = 0; i < 4; i++) {
        node_copy(sLabels[i], "Move 000 PP00/00");
        node_number(sLabels[i] + 5, mon->moves[i], 3);
        node_number(sLabels[i] + 11, mon->pp[i], 2);
        node_number(sLabels[i] + 14, mon->max_pp[i], 2);
        node_add(i, sLabels[i], i);
    }
    node_add(4, "Back", RL_NO_SLOT);
    node_list(sItem >= RL_ITEM_COUNT ? "Replace which move?" : "Restore which move?", 5, NODE_MOVE);
}

static unsigned node_living(void) {
    unsigned i, count;
    count = 0;
    for (i = 0; i < sRun->party_count; i++)
        if (sRun->party[i].cur_hp && !sRun->party[i].fainted) count++;
    return count;
}

int rl_scene_nodes_update(u16 pressed) {
    RunState copy;
    int selected, result;
    unsigned value, i;
    if (sPage == NODE_DONE) {
        return sReturn;
    }
    if (sPage == NODE_ERROR)
        return pressed & (RL_UI_A | RL_UI_B) ? RL_SCENE_CANCEL : RL_SCENE_ONGOING;
    if (sPage == NODE_ROOT &&
        (sMode == RL_SCENE_BAG || sMode == RL_SCENE_PARTY) &&
        (pressed & NODE_RELICS_TRIGGER)) {
        sMessage = NULL;
        node_relics();
        return RL_SCENE_ONGOING;
    }
    selected = rl_ui_list(&sList, pressed);
    if (selected == RL_UI_NONE) return RL_SCENE_ONGOING;
    if (selected == RL_UI_CANCEL) {
        if (sPage == NODE_ROOT) {
            if (sMode == RL_SCENE_BAG || sMode == RL_SCENE_PARTY) return RL_SCENE_CANCEL;
            sMessage = "Choose Leave node to finish, or resolve its offer.";
        } else node_root();
        return sReturn;
    }
    if (selected < 0) {
        if (selected == RL_UI_DISABLED && sPage == NODE_ROOT &&
            sMode == RL_MART && sList.cursor >= 0 &&
            (unsigned)sList.cursor < sShopCount &&
            sShop[sValues[sList.cursor]].item == RL_RELIC_ITEM &&
            (sShop[sValues[sList.cursor]].detail == RL_OLD_AMBER ||
             sShop[sValues[sList.cursor]].detail == RL_MASTER_BALL)) {
            sMessage = "Recruit action not implemented; relic unavailable.";
        } else if (selected == RL_UI_DISABLED && sPage == NODE_ROOT &&
                   sMode == RL_MART && sList.cursor >= 0 &&
                   (unsigned)sList.cursor < sShopCount &&
                   sShop[sValues[sList.cursor]].item == RL_RELIC_ITEM) {
            sMessage = "Recruit action not implemented; Amber/Master Ball unavailable.";
        } else {
            sMessage = "This offer is unavailable or its relic is already owned.";
        }
        return RL_SCENE_ONGOING;
    }
    value = sValues[selected];
    if (value == RL_NO_SLOT) {
        if (sPage != NODE_ROOT) node_root();
        else if (sMode == RL_SCENE_BAG || sMode == RL_SCENE_PARTY) return RL_SCENE_CANCEL;
        else node_done("You left the node.");
        return sReturn;
    }
    sMessage = NULL;
    if (sPage == NODE_RELICS) {
        if (value == RL_NO_SLOT) node_root();
    } else if (sPage == NODE_ROOT) {
        if (sMode == RL_SCENE_BAG) {
            sItem = value;
            node_party("Use on which Pokemon?", NODE_TARGET);
        } else if (sMode == RL_SCENE_PARTY) {
            sSlot = value;
            node_add(0, "Reorder: choose destination", 0);
            node_add(1, "Release this Pokemon", 1);
            node_add(2, "Back", RL_NO_SLOT);
            node_list("Party actions", 3, NODE_PARTY_ACTION);
        } else if (value == 100 || value == 101) {
            /* Preserve the node mode so closing the utility returns to the same offers. */
            if (value == 100) {
                node_bag();
                sPage = NODE_PARTY_ACTION;
                sChoice = 100;
            } else {
                node_party("Party: select a Pokemon", NODE_PARTY_ACTION);
                sChoice = 101;
            }
        } else if (sMode == RL_MART) {
            copy = *sRun;
            result = rl_shop_buy(&copy, &sShop[value]);
            if (result == RL_OK && sShop[value].item == RL_RELIC_ITEM &&
                sShop[value].detail == RL_PP_POUCH &&
                rl_battle_apply_pp_pouch(&copy) < 0)
                result = RL_INVALID;
            if (result == RL_OK) {
                *sRun = copy;
                if (sShop[value].item == RL_RELIC_ITEM) sItems[selected].disabled = 1;
                sMessage = "Purchased. Added to Bag or relics.";
            }
            else node_error(result);
        } else if (sMode == RL_CENTER) {
            if (value == 0) {
                if (!node_living()) node_error(RL_INVALID);
                else {
                    for (i = 0; i < sRun->party_count; i++) rl_mon_heal(&sRun->party[i], 50);
                    node_done("Living Pokemon healed 50%. Fainted stay fainted.");
                }
            } else node_party("Revive which Pokemon?", NODE_TARGET);
        } else if (sMode == RL_RECRUIT) {
            result = rl_party_recruit(sRun, &sWild[value]);
            if (result == RL_OK) node_done("Rental recruited.");
            else node_error(result);
        } else {
            sChoice = value;
            if ((sEvent == 2 || sEvent == 3 || sEvent == 4) && value == 0)
                node_party(sEvent == 2 ? "Deposit which Pokemon?" :
                           "Replace which Pokemon?", NODE_TARGET);
            else {
                result = node_event(value, 0);
                if (result == RL_OK) node_done("Event resolved.");
                else if (sEvent == 0 && result == RL_INVALID)
                    sMessage = "Chosen fossil is missing from canonical rentals.";
                else if (sEvent == 8 && result == RL_FULL)
                    sMessage = "No available relics. Amber/Master Ball recruit unavailable.";
                else node_error(result);
            }
        }
    } else if (sPage == NODE_PARTY_ACTION && sChoice == 100 && sMode != RL_SCENE_PARTY) {
        sItem = value;
        node_party("Use on which Pokemon?", NODE_TARGET);
        sChoice = 100;
    } else if (sPage == NODE_PARTY_ACTION && sChoice == 101 && sMode != RL_SCENE_PARTY) {
        sSlot = value;
        node_add(0, "Reorder: choose destination", 0);
        node_add(1, "Release this Pokemon", 1);
        node_add(2, "Back", RL_NO_SLOT);
        node_list("Party actions", 3, NODE_PARTY_ACTION);
        sChoice = 102;
    } else if (sPage == NODE_PARTY_ACTION) {
        if (value == 0) node_party("Choose destination", NODE_SWAP);
        else {
            node_add(0, "Keep Pokemon", 0);
            node_add(1, "Confirm permanent release", 1);
            node_list("Release cannot be undone", 2, NODE_RELEASE);
        }
    } else if (sPage == NODE_SWAP) {
        result = rl_party_swap(sRun, sSlot, value);
        node_root();
        if (result == RL_OK) sMessage = "Party order updated.";
        else node_error(result);
    } else if (sPage == NODE_RELEASE) {
        if (value == 0) node_root();
        else if (sRun->party_count <= 1 ||
                 (node_living() <= 1 && sRun->party[sSlot].cur_hp &&
                  !sRun->party[sSlot].fainted))
            sMessage = "Cannot release your last living Pokemon.";
        else {
            result = rl_party_release(sRun, sSlot);
            node_root();
            if (result == RL_OK) sMessage = "Pokemon released.";
            else node_error(result);
        }
    } else if (sPage == NODE_TARGET) {
        sSlot = value;
        if (sMode == RL_SCENE_BAG || sChoice == 100) {
            if (sItem == RL_ETHER || sItem >= RL_ITEM_COUNT) {
                if (sItem >= RL_ITEM_COUNT) {
                    RlU8 pp;
                    if (!node_can_learn(&sRun->party[value],
                                       sRun->tm_moves[sItem - RL_ITEM_COUNT], &pp,
                                       (void*)sRentals)) {
                        sMessage = "This species cannot learn that TM.";
                        return sReturn;
                    }
                }
                node_moves();
            } else {
                result = rl_item_use(sRun, sItem, value, 0);
                node_root();
                if (result == RL_OK) sMessage = "Item used.";
                else node_error(result);
            }
        } else if (sMode == RL_CENTER) {
            result = rl_party_revive(sRun, value);
            if (result == RL_OK) node_done("Pokemon revived with 50% HP.");
            else node_error(result);
        } else {
            result = node_event(sChoice, value);
            if (result == RL_OK) node_done("Event resolved.");
            else if (sEvent == 2) sMessage = "Daycare occupied or last party Pokemon.";
            else node_error(result);
        }
    } else if (sPage == NODE_MOVE) {
        if (sItem >= RL_ITEM_COUNT)
            result = rl_tm_teach(sRun, sItem - RL_ITEM_COUNT, sSlot, value,
                                 node_can_learn, (void*)sRentals);
        else result = rl_item_use(sRun, sItem, sSlot, value);
        node_root();
        if (result == RL_OK) sMessage = "Move updated; item consumed.";
        else node_error(result);
    }
    return sReturn;
}

void rl_scene_nodes_draw(void) {
    char label[48];
    unsigned value;
    int relic_shop;
    int mon_page;
    const RlMon* mon;
    RlMon preview;
    mon = NULL;
    mon_page = 0;
    if (sRun && sList.cursor >= 0 && sList.cursor < sList.count) {
        value = sValues[sList.cursor];
        if (sPage == NODE_ROOT && (sMode == RL_RECRUIT ||
            (sMode == RL_EVENT && (sEvent == 0 || sEvent == 3 ||
                                  sEvent == 4 || sEvent == 6)))) {
            mon_page = 1;
            if ((sMode == RL_RECRUIT || sEvent == 6) && value < sWildCount)
                mon = &sWild[value];
            else if (sMode == RL_EVENT && value < (sEvent == 0 ? 2U : 1U) &&
                     node_event_mon(value, 0, &preview))
                mon = &preview;
        } else if (sPage == NODE_TARGET || sPage == NODE_SWAP ||
                   (sPage == NODE_ROOT && sMode == RL_SCENE_PARTY) ||
                   (sPage == NODE_PARTY_ACTION && sChoice == 101)) {
            mon_page = 1;
            if (value < sRun->party_count) {
                mon = &sRun->party[value];
                if (sPage == NODE_TARGET && sMode == RL_EVENT &&
                    sChoice == 0 && (sEvent == 3 || sEvent == 4) &&
                    node_event_mon(0, value, &preview)) {
                    node_mon_label(label, mon, value);
                    rl_ui_text(30, 276, "Replace party slot:", sGold);
                    rl_ui_text_wrap(30, 296, 340, 1, label, sWhite);
                    mon = &preview;
                }
            }
        } else if (sPage == NODE_MOVE || sPage == NODE_RELEASE ||
                   (sPage == NODE_PARTY_ACTION &&
                    (sMode == RL_SCENE_PARTY || sChoice == 102))) {
            mon_page = 1;
            if (sSlot < sRun->party_count) {
                mon = &sRun->party[sSlot];
                node_mon_label(label, mon, sSlot);
                rl_ui_text_wrap(30, 276, 340, 1, label, sGold);
            }
        }
    }
    relic_shop = sPage == NODE_ROOT && sMode == RL_MART &&
        sList.cursor >= 0 && (unsigned)sList.cursor < sShopCount &&
        sShop[sValues[sList.cursor]].item == RL_RELIC_ITEM;
    rl_ui_list_draw(&sList, 18, 18, mon_page ? 364 : 604);
    if (mon) rl_ui_mon_detail(mon, 400, 50);
    if (sRun && sPage != NODE_RELICS) {
        node_copy(label, "Coins 0000000000  Party 0/6");
        node_number(label + 6, sRun->money, 10);
        node_number(label + 24, sRun->party_count, 1);
        rl_ui_text(30, 220, label, sGold);
    }
    if (sPage == NODE_ROOT && sMode == RL_EVENT) {
        rl_ui_text_wrap(30, 250, mon_page ? 340 : 570,
                        mon_page ? 3 : 2, sDescriptions[sEvent], sWhite);
        if (sEvent == 8)
            rl_ui_text_wrap(30, 276, 570, 1,
                            "Amber/Master Ball recruit unavailable.", sGold);
    }
    if (sPage == NODE_ROOT && sMode == RL_MART && sList.cursor >= 0 &&
        sValues[sList.cursor] < sShopCount) {
        value = sValues[sList.cursor];
        node_copy(label, "Price: 00000 coins");
        node_number(label + 7, sShop[value].price, 5);
        rl_ui_text(30, 250, label, sWhite);
        if (sShop[value].item == RL_RELIC_ITEM && sShop[value].detail < RL_RELIC_COUNT)
            rl_ui_text_wrap(30, 276, 570, 2,
                            rl_relic_defs[sShop[value].detail].effect, sWhite);
        if (sShop[value].item == RL_RELIC_ITEM && sShop[value].detail < RL_RELIC_COUNT)
            rl_ui_text_wrap(30, 320, 570, 2,
                            rl_scene_relic_status(sRun, sShop[value].detail),
                            sGold);
        if (sShop[value].item == RL_RELIC_ITEM)
            rl_ui_text_wrap(30, 354, 570, 1,
                            "Amber/Master Ball recruit unavailable.", sRed);
    }
    if (sPage == NODE_RELICS && sList.cursor >= 0 &&
        sList.cursor < sList.count && sValues[sList.cursor] < RL_RELIC_COUNT) {
        value = sValues[sList.cursor];
        rl_ui_text(30, 220, rl_relic_defs[value].name, sGold);
        rl_ui_text_wrap(30, 246, 570, 3, rl_relic_defs[value].effect, sWhite);
        rl_ui_text_wrap(30, 306, 570, 2,
                        rl_scene_relic_status(sRun, value), sGold);
    }
    if (sMessage) {
        if (mon_page) rl_ui_text_wrap(30, 340, 570, 2, sMessage, sRed);
        else rl_ui_text(30, 314, sMessage, sRed);
    }
    rl_ui_text(30, mon_page || relic_shop ? 394 : 354,
               (sPage == NODE_ROOT &&
                (sMode == RL_SCENE_BAG || sMode == RL_SCENE_PARTY)) ?
               "D-pad: select   A: confirm   B: back   R: relics" :
               "D-pad: select   A: confirm   B: back", sWhite);
}
#endif
