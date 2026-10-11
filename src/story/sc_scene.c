#ifdef ROGUELIKE
#include "sc_scene.h"
#include "src/fragments/rl/rl_ui.h"

static const Color_RGBA8 sWhite = {255, 255, 255, 255};
static const Color_RGBA8 sGold = {255, 224, 96, 255};
static const Color_RGBA8 sMuted = {160, 176, 192, 255};

static void decimal(char *out, unsigned n, unsigned digits) {
    while (digits) {
        digits--;
        out[digits] = (char)('0' + n % 10);
        n /= 10;
    }
}

static const RlMon *detail_mon(const ScUi *ui) {
    unsigned slot = ui->slot;
    if (ui->screen == SC_UI_PARTY || ui->screen == SC_UI_REPLACE || ui->screen == SC_UI_TARGET)
        slot = ui->ids[ui->list.cursor];
    if (ui->screen == SC_UI_REWARD || ui->screen == SC_UI_DONOR) return &ui->state.offer;
    if (ui->screen == SC_UI_PARTY || ui->screen == SC_UI_MON || ui->screen == SC_UI_REPLACE ||
        ui->screen == SC_UI_TARGET || ui->screen == SC_UI_MOVE || ui->screen == SC_UI_LEARN ||
        ui->screen == SC_UI_EVOLVE)
        return slot < ui->state.party_count ? &ui->state.party[slot] : NULL;
    return NULL;
}

void sc_scene_draw(const ScUi *ui) {
    RlUiList visible;
    RlUiItem items[7];
    char labels[7][36], value[36];
    const char *label;
    const RlMon *mon;
    unsigned i, j, index, id, move;
    if (!ui || !rl_ui_list_valid(&ui->list)) {
        rl_ui_text(44, 80, "Story UI: invalid presentation state", sGold);
        return;
    }
    rl_ui_text(44, 40, "Red / Blue Story Campaign", sGold);
    value[0] = 'M'; value[1] = 'o'; value[2] = 'o'; value[3] = 'n';
    value[4] = ':'; value[5] = ' '; value[6] = (char)('0' + ui->state.moon_stones);
    value[7] = ' '; value[8] = '/'; value[9] = ' ';
    value[10] = (char)('0' + ui->state.moon_stones_spent);
    value[11] = ' '; value[12] = 'u'; value[13] = 's'; value[14] = 'e';
    value[15] = 'd'; value[16] = 0;
    rl_ui_text(44, 64, value, sMuted);
    visible = ui->list;
    visible.count = ui->list.count - ui->list.scroll;
    if (visible.count > 7) visible.count = 7;
    visible.cursor = ui->list.cursor - ui->list.scroll;
    visible.scroll = 0;
    visible.items = items;
    for (i = 0; i < (unsigned)visible.count; i++) {
        index = (unsigned)ui->list.scroll + i;
        id = ui->ids[index];
        label = ui->items[index].text;
        if (ui->screen == SC_UI_DONOR) label = rl_ui_move_name(ui->state.offer.moves[id]);
        else if ((ui->screen == SC_UI_MOVE || ui->screen == SC_UI_LEARN) && id < 4)
            label = rl_ui_move_name(ui->state.party[ui->slot].moves[id]);
        for (j = 0; j < 32 && label[j]; j++) labels[i][j] = label[j];
        labels[i][j] = 0;
        if (j == 32 && label[j]) {
            labels[i][29] = labels[i][30] = labels[i][31] = '.';
        }
        items[i].text = labels[i];
        items[i].disabled = ui->items[index].disabled;
    }
    rl_ui_list_draw(&visible, 44, 84, 360);
    if (ui->list.scroll) rl_ui_text(382, 92, "^", sGold);
    if (ui->list.scroll + 7 < ui->list.count) rl_ui_text(382, 292, "v", sGold);
    rl_ui_window(410, 84, 194, 300);
    mon = detail_mon(ui);
    if (mon && mon->species) {
        rl_ui_mon_detail(mon, 414, 90);
        value[0] = 'S'; value[1] = 't'; value[2] = 'a'; value[3] = 't';
        value[4] = 'u'; value[5] = 's'; value[6] = ':'; value[7] = ' ';
        decimal(value + 8, mon->status, 3); value[11] = 0;
        rl_ui_text(414, 364, value, sMuted);
        if (ui->screen == SC_UI_LEARN) {
            move = ui->state.learn_moves[ui->slot];
            rl_ui_text(44, 344, "Learning:", sGold);
            rl_ui_text_wrap(44, 364, 350, 1, rl_ui_move_name(move), sWhite);
        } else if (ui->screen == SC_UI_EVOLVE)
            rl_ui_text_wrap(44, 344, 350, 2, "Moon Stones: finite. Shop stones / trade unavailable.", sMuted);
    } else if (ui->screen == SC_UI_CREATE) {
        static const char hex[] = "0123456789ABCDEF";
        for (i = 0; i < 8; i++) value[i] = hex[(ui->seed >> ((7 - i) * 4)) & 15];
        value[8] = 0;
        rl_ui_text(414, 96, "Seed:", sGold);
        rl_ui_text(414, 120, value, sWhite);
        rl_ui_text_wrap(414, 156, 184, 9,
            "Six-member party. No PC. Defeat ends campaign. Healing preset cannot change after creation.", sWhite);
    } else if (ui->screen == SC_UI_ENCOUNTERS && ui->list.count &&
               !ui->items[ui->list.cursor].disabled) {
        id = ui->ids[ui->list.cursor];
        rl_ui_text_wrap(414, 96, 184, 6, ui->catalog->graph->nodes[id].id, sWhite);
        if (id < ui->catalog->count) {
            unsigned variant = ui->catalog->encounters[id].kind == SC_RIVAL ? ui->state.rival_branch :
                id == ui->catalog->graph->fossil_offer ? ui->state.fossil_choice : 0;
            if (variant < ui->catalog->encounters[id].variants) {
                const ScTeam *team = &ui->catalog->encounters[id].teams[variant];
                for (i = 0; i < team->count; i++)
                    rl_ui_text_wrap(414, 230 + i * 24, 184, 1,
                        rl_ui_species_name(team->mons[i].species), sWhite);
            }
        }
    } else
        rl_ui_text_wrap(414, 96, 184, 12,
            "Optional trainers unless gated by the canon graph. Finite encounters only. HP / PP / status persist.", sWhite);
    if (ui->message) rl_ui_text_wrap(44, 400, 556, 3, ui->message, sGold);
    rl_ui_text(44, 464, "D-pad: select / page    A: choose    B: back", sMuted);
}
#endif
