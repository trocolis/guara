/*
 * Inventário: abas TODOS, COMIDAS, BEBIDAS e POCOES e as ações de cada item.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "app.h"
#include "audio.h"
#include "widgets.h"
#include "modal.h"
#include "screens.h"

/* Inventário: abas TODOS, COMIDAS, BEBIDAS e POCOES (README > Inventário). */

static const char *const TABS[] = {"TODOS", "COMIDAS", "BEBIDAS", "POCOES"};
static int tab;
static int ids[COUNT_ITEMS];
static ui_list_t list;

/* Ordena os itens pelo nome. */
static int by_name(const void *a, const void *b)
{
    catalog_record_t ra, rb;
    catalog_read(CAT_ITEMS, *(const int *)a, &ra);
    catalog_read(CAT_ITEMS, *(const int *)b, &rb);
    return strcasecmp(ra.name, rb.name);
}

/* Monta a lista de itens possuídos da aba atual. */
static void mount(void)
{
    catalog_record_t r;
    list.total = 0;
    for (int id = 0; id < catalog_record_count(CAT_ITEMS) && id < COUNT_ITEMS; id++) {
        if (g_guara.inventory[id] == 0 || !catalog_read(CAT_ITEMS, id, &r)) {
            continue;
        }
        if (tab == 0 || r.type == tab - 1) {
            ids[list.total++] = id;
        }
    }
    qsort(ids, (size_t)list.total, sizeof(int), by_name);
    ui_list_adjust(&list, UI_LINES_VISIBLE);
}

/* Item da lista com a quantidade à direita. */
static void item(int i, ui_item_t *it, void *ctx)
{
    (void)ctx;
    catalog_record_t r;
    catalog_read(CAT_ITEMS, ids[i], &r);
    it->icon = r.icon;
    snprintf(it->text, sizeof(it->text), "%s", r.name);
    snprintf(it->info, sizeof(it->info), "x%d", g_guara.inventory[ids[i]]);
}

/* Id do item selecionado, ou -1. */
static int item_sel(void)
{
    return list.total ? ids[list.sel] : -1;
}

/* Descarta a quantidade escolhida após a confirmação. */
static void discard_confirmed(int r, void *ctx)
{
    int amount = (int)(intptr_t)ctx;
    int id = item_sel();
    if (r == 1 && id >= 0) {
        g_guara.inventory[id] = (uint8_t)(g_guara.inventory[id] > amount ? g_guara.inventory[id] - amount : 0);
        app_mark_changed();
        mount();
    }
}

/* Recebe a quantidade e pede confirmação. */
static void discard_count(int amount, void *ctx)
{
    (void)ctx;
    if (amount <= 0) {
        return;
    }
    catalog_record_t r;
    catalog_read(CAT_ITEMS, item_sel(), &r);
    char msg[48];
    snprintf(msg, sizeof(msg), "Descartar %d %s?", amount, r.name);
    ui_confirm("DESCARTAR", r.icon, msg, "NAO", "SIM", discard_confirmed, (void *)(intptr_t)amount);
}

/* Mostra descrição, efeitos, sobre-efeitos e afinidade do item. */
static void details(const catalog_record_t *r)
{
    static const char *const targets[] = {"HP", "MP", "Energia", "Humor", "Alimentação", "Saúde"};
    char text[256];
    int n = snprintf(text, sizeof(text), "%s\n", r->description);
    for (int i = 0; i < r->n_effects && n < (int)sizeof(text); i++) {
        int a = r->effects[i].target;
        n += snprintf(text + n, sizeof(text) - (size_t)n, "%s %+d%s\n", a < 6 ? targets[a] : "?", r->effects[i].value,
                      a >= VITAL_ENERGY && a < 6 ? "%" : "");
    }
    for (int i = 0; i < r->n_status && n < (int)sizeof(text); i++) {
        const char *name = r->status[i].state == ST_ALL ? "Todos" : state_name((state_t)r->status[i].state);
        if (r->status[i].value <= -100) {
            n += snprintf(text + n, sizeof(text) - (size_t)n, "Cura: %s\n", name);
        } else {
            n += snprintf(text + n, sizeof(text) - (size_t)n, "%s %+d%%\n", name, r->status[i].value);
        }
    }
    if (n < (int)sizeof(text)) {
        snprintf(text + n, sizeof(text) - (size_t)n, "Afinidade: %s", nature_name((nature_t)r->nature));
    }
    ui_details(r->name, text);
}

/* Trata Comer/Beber/Usar, Detalhes, Descartar e Compartilhar. */
static void action_chosen(int option, void *ctx)
{
    (void)ctx;
    int id = item_sel();
    catalog_record_t r;
    if (id < 0 || !catalog_read(CAT_ITEMS, id, &r)) {
        return;
    }
    switch (option) {
    case 0:
        screens_use_item(id);
        break;
    case 1:
        details(&r);
        break;
    case 2:
        ui_quantity_picker("QUANTIDADE", 1, g_guara.inventory[id], 1, discard_count, NULL);
        break;
    case 3:
        /* README: TODO "Elaborar a opção de Compartilhar" (depende do Bluetooth) */
        ui_alert("COMPARTILHAR", "bluetooth", "Em breve: troca de itens entre Guarás.", "OK", NULL, NULL);
        break;
    default:
        break;
    }
}

/* Lista, troca de aba e modal de ações. */
static void on_key(const key_event_t *ev)
{
    if (ui_list_key(&list, ev, UI_LINES_VISIBLE) || ev->type == KEY_RELEASED) {
        return;
    }
    if (ev->on_key == KEY_LEFT || ev->on_key == KEY_RIGHT) {
        tab = (tab + (ev->on_key == KEY_RIGHT ? 1 : 3)) % 4;
        list.sel = list.top = 0;
        mount();
    } else if (ev->type != KEY_PRESSED) {
        return;
    } else if (ev->on_key == KEY_B) {
        ui_back();
    } else if (ev->on_key == KEY_A && list.total) {
        catalog_record_t r;
        catalog_read(CAT_ITEMS, item_sel(), &r);
        const char *use = r.type == ITEM_FOOD ? "Comer" : (r.type == ITEM_DRINK ? "Beber" : "Usar");
        const char *options[] = {use, "Detalhes", "Descartar", "Compartilhar"};
        ui_choice(r.name, options, 4, action_chosen, NULL);
    }
}

/* Desenha as abas e a lista (ou o aviso de vazio). */
static void on_draw(gfx_t *g, uint32_t now)
{
    ui_tabs(g, TABS, 4, tab, now);
    if (list.total == 0) {
        gfx_text_center(g, 0, GFX_WIDTH, 32, "Nenhum item", GFX_WHITE);
        return;
    }
    ui_list_draw(g, &list, UI_TITLE_HEIGHT, UI_LINES_VISIBLE, item, NULL, now);
}

/* Começa na aba TODOS. */
static void on_open(void *param)
{
    (void)param;
    tab = 0;
    list = (ui_list_t){0};
    mount();
}

/* Atualiza a lista ao voltar (ex.: depois de usar um item). */
static void on_resume(void)
{
    mount();
}

const screen_t SCREEN_INVENTORY = {
    .name = "inventario",
    .on_open = on_open,
    .on_resume = on_resume,
    .on_key = on_key,
    .on_draw = on_draw,
};
