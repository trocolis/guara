/*
 * Equipamentos: manequim com a região do slot piscando e a lista dos 7 slots.
 */
#include <stdio.h>

#include "app.h"
#include "widgets.h"
#include "modal.h"
#include "screens.h"

/*
 * Tela de Equipamentos (mockup bitmaps/exemple/menu_equip.bmp): sem título nem
 * rodapé, manequim do lado direito do aparelho (esquerda do usuário) e lista
 * com os 7 slots. A região do slot selecionado pisca no manequim 2x por segundo.
 */

#define MANNEQUIN_X 6
#define MANNEQUIN_Y 4
#define DIVIDER_X 42
#define LIST_X 45
#define LINE_H 8

static const char *const NAMES_SLOT[COUNT_SLOTS_EQUIP] = {"Cabeça", "Corpo", "Mão ATK", "Mão DEF",
                                                       "Calçados", "Acessório 1", "Acessório 2"};
/* Regiões no manequim 32x56 (x, y, largura, altura) */
static const int8_t REGIONS[COUNT_SLOTS_EQUIP][4] = {
    {7, 3, 18, 20}, {10, 22, 12, 19}, {5, 25, 6, 16}, {21, 25, 6, 16}, {6, 46, 20, 8}, {11, 19, 10, 5}, {9, 38, 14, 4},
};

static int slot;
static marquee_t marquee;

/* Sub-menu com os equipamentos possuídos do tipo do slot */
static int options_ids[64];
static ui_list_t equipment_list;

/* Item da lista de equipamentos que cabem no slot. */
static void item_eq(int i, ui_item_t *it, void *ctx)
{
    (void)ctx;
    catalog_record_t r;
    catalog_read(CAT_EQUIPMENT, options_ids[i], &r);
    it->icon = r.icon;
    snprintf(it->text, sizeof(it->text), "%s", r.name);
    bool equipped = false;
    for (int s = 0; s < COUNT_SLOTS_EQUIP; s++) {
        equipped |= g_guara.equipped[s] == options_ids[i];
    }
    if (equipped) {
        snprintf(it->info, sizeof(it->info), "EQP");
    }
}

/* Equipa o item escolhido ou volta. */
static void eq_key(const key_event_t *ev)
{
    if (ui_list_key(&equipment_list, ev, UI_LINES_VISIBLE) || ev->type != KEY_PRESSED) {
        return;
    }
    if (ev->on_key == KEY_B) {
        ui_back();
    } else if (ev->on_key == KEY_A && equipment_list.total) {
        guara_equip(&g_guara, slot, options_ids[equipment_list.sel]);
        app_mark_changed();
        ui_back();
    }
}

/* Desenha a lista de equipamentos do slot. */
static void eq_draw(gfx_t *g, uint32_t now)
{
    ui_title(g, NAMES_SLOT[slot], now);
    if (equipment_list.total == 0) {
        gfx_text_center(g, 0, GFX_WIDTH, 32, "Nada para equipar", GFX_WHITE);
        return;
    }
    ui_list_draw(g, &equipment_list, UI_TITLE_HEIGHT, UI_LINES_VISIBLE, item_eq, NULL, now);
}

static const screen_t SCREEN_LIST_EQUIP = {
    .name = "lista_equip",
    .on_key = eq_key,
    .on_draw = eq_draw,
};

/* Monta e abre a lista de equipamentos possuídos do tipo do slot. */
static void open_list(void)
{
    catalog_record_t r;
    equipment_list = (ui_list_t){0};
    for (int id = 0; id < catalog_record_count(CAT_EQUIPMENT) && id < 64; id++) {
        if ((g_guara.owned_equipment & (1ull << id)) && catalog_read(CAT_EQUIPMENT, id, &r) &&
            r.type == guara_slot_type(slot)) {
            options_ids[equipment_list.total++] = id;
        }
    }
    ui_open(&SCREEN_LIST_EQUIP, NULL);
}

/* Trata a escolha Equipar ou Remover. */
static void option(int r, void *ctx)
{
    (void)ctx;
    if (r == 0) {
        open_list();
    } else if (r == 1) {
        guara_unequip(&g_guara, slot);
        app_mark_changed();
    }
}

/* Cima/Baixo escolhem o slot; A equipa ou abre as opções. */
static void on_key(const key_event_t *ev)
{
    if (ev->type == KEY_RELEASED) {
        return;
    }
    if (ev->on_key == KEY_UP) {
        slot = (slot + COUNT_SLOTS_EQUIP - 1) % COUNT_SLOTS_EQUIP;
    } else if (ev->on_key == KEY_DOWN) {
        slot = (slot + 1) % COUNT_SLOTS_EQUIP;
    } else if (ev->type != KEY_PRESSED) {
        return;
    } else if (ev->on_key == KEY_B) {
        ui_back();
    } else if (ev->on_key == KEY_A) {
        if (g_guara.equipped[slot] == SLOT_EMPTY) {
            open_list();
        } else {
            static const char *const options[] = {"Equipar", "Remover"};
            ui_choice(NAMES_SLOT[slot], options, 2, option, NULL);
        }
    }
}

/* Desenha o manequim, a região piscando e os nomes equipados. */
static void on_draw(gfx_t *g, uint32_t now)
{
    gfx_rect_rounded(g, 0, 0, GFX_WIDTH, GFX_HEIGHT, 3, GFX_WHITE);
    gfx_line_v(g, DIVIDER_X, 1, GFX_HEIGHT - 2, GFX_WHITE);
    ui_icon(g, "mannequin", MANNEQUIN_X, MANNEQUIN_Y);
    if ((now / 250) % 2 == 0) {
        const int8_t *r = REGIONS[slot];
        gfx_rect(g, MANNEQUIN_X + r[0], MANNEQUIN_Y + r[1], r[2], r[3], GFX_INVERT);
    }
    for (int s = 0; s < COUNT_SLOTS_EQUIP; s++) {
        int y = 2 + s * LINE_H + (s ? 1 : 0) * 0;
        char name[40];
        catalog_record_t r;
        if (g_guara.equipped[s] != SLOT_EMPTY && catalog_read(CAT_EQUIPMENT, g_guara.equipped[s], &r)) {
            snprintf(name, sizeof(name), "%s", r.name);
        } else {
            snprintf(name, sizeof(name), "- Vazio -");
        }
        bool sel = s == slot;
        if (sel) {
            gfx_rect_rounded_filled(g, LIST_X - 1, y, GFX_WIDTH - LIST_X - 1, LINE_H + 1, 2, GFX_WHITE);
        }
        int width = GFX_WIDTH - LIST_X - 5;
        int w = gfx_text_width(name);
        if (w <= width) {
            gfx_text(g, LIST_X + 1 + (width - w) / 2, y + 1, name, sel ? GFX_BLACK : GFX_WHITE);
        } else {
            ui_marquee_text(g, LIST_X + 1, y + 1, width, name, sel ? GFX_BLACK : GFX_WHITE, sel, &marquee, now);
        }
    }
}

const screen_t SCREEN_EQUIPMENT = {
    .name = "equipamentos",
    .on_key = on_key,
    .on_draw = on_draw,
};
