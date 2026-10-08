/*
 * Sub-menu padrão genérico (lista estática de itens com ação e informação).
 */
#include "submenu.h"

#include <stdio.h>

#include "widgets.h"
#include "ui.h"

typedef struct {
    const submenu_t *def;
    ui_list_t list;
} state_t;

static state_t states[16];
static const submenu_t *next;

/* Estado do sub-menu da camada atual. */
static state_t *current(void)
{
    return &states[ui_current_layer() & 15];
}

/* Instala a definição pendente na camada. */
static void on_open(void *param)
{
    (void)param;
    state_t *e = current();
    e->def = next;
    e->list = (ui_list_t){.total = next->count};
}

/* Item da lista a partir da definição. */
static void item(int i, ui_item_t *it, void *ctx)
{
    const submenu_t *d = ctx;
    it->icon = d->items[i].icon;
    snprintf(it->text, sizeof(it->text), "%s", d->items[i].text);
    if (d->items[i].info) {
        d->items[i].info(it->info, sizeof(it->info));
    }
}

/* Lista, ação do item (A) e voltar (B). */
static void on_key(const key_event_t *ev)
{
    state_t *e = current();
    if (ui_list_key(&e->list, ev, UI_LINES_VISIBLE) || ev->type != KEY_PRESSED) {
        return;
    }
    if (ev->key == KEY_B) {
        ui_back();
    } else if (ev->key == KEY_A && e->def->items[e->list.sel].action) {
        e->def->items[e->list.sel].action();
    }
}

/* Título e lista do sub-menu. */
static void on_draw(gfx_t *g, uint32_t now)
{
    state_t *e = current();
    ui_title(g, e->def->title, now);
    ui_list_draw(g, &e->list, UI_LIST_Y, UI_LINES_VISIBLE, item, (void *)e->def, now);
}

static const screen_t SCREEN_SUBMENU = {
    .name = "submenu",
    .on_key = on_key,
    .on_draw = on_draw,
    .on_open = on_open,
};

/* Abre um sub-menu a partir de uma definição estática. */
void submenu_open(const submenu_t *def)
{
    next = def;
    ui_open(&SCREEN_SUBMENU, NULL);
}
