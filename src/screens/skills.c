/*
 * Habilidades possuídas; ←/→ ativa ou desativa (até 4 ativas).
 */
#include <stdio.h>

#include "app.h"
#include "widgets.h"
#include "modal.h"
#include "screens.h"

/* Habilidades possuídas (sub-menu padrão); as 4 ativas são marcadas com "ATV". */

static int ids[64];
static ui_list_t list;

/* Verdadeiro se a habilidade está entre as 4 ativas. */
static bool is_active(int id)
{
    for (int i = 0; i < COUNT_SKILL_ACTIVE; i++) {
        if (g_guara.active_skills[i] == id) {
            return true;
        }
    }
    return false;
}

/* Monta a lista de habilidades possuídas. */
static void mount(void)
{
    list.total = 0;
    for (int id = 0; id < catalog_record_count(CAT_SKILLS) && id < 64; id++) {
        if (g_guara.owned_skills & (1ull << id)) {
            ids[list.total++] = id;
        }
    }
}

/* Item da lista com ATV nas ativas. */
static void item(int i, ui_item_t *it, void *ctx)
{
    (void)ctx;
    catalog_record_t r;
    catalog_read(CAT_SKILLS, ids[i], &r);
    it->icon = r.icon;
    snprintf(it->text, sizeof(it->text), "%s", r.name);
    if (is_active(ids[i])) {
        snprintf(it->info, sizeof(it->info), "ATV");
    }
}

/* Ativa ou desativa a habilidade selecionada. */
static void toggle(int r, void *ctx)
{
    (void)ctx;
    int id = ids[list.sel];
    if (r != 1) {
        return;
    }
    if (is_active(id)) {
        for (int i = 0; i < COUNT_SKILL_ACTIVE; i++) {
            if (g_guara.active_skills[i] == id) {
                g_guara.active_skills[i] = SLOT_EMPTY;
            }
        }
    } else {
        for (int i = 0; i < COUNT_SKILL_ACTIVE; i++) {
            if (g_guara.active_skills[i] == SLOT_EMPTY) {
                g_guara.active_skills[i] = (uint8_t)id;
                break;
            }
        }
    }
    app_mark_changed();
}

/* Detalhes (A), ativar/desativar (←/→) e voltar (B). */
static void on_key(const key_event_t *ev)
{
    if (ui_list_key(&list, ev, UI_LINES_VISIBLE) || ev->type != KEY_PRESSED) {
        return;
    }
    if (ev->on_key == KEY_B) {
        ui_back();
    } else if (ev->on_key == KEY_A && list.total) {
        static const char *const types[] = {"Ataque", "Cura", "Efeito"};
        static const char *const targets[] = {"Usuário", "Oponente"};
        catalog_record_t r;
        catalog_read(CAT_SKILLS, ids[list.sel], &r);
        char text[200];
        int n = snprintf(text, sizeof(text), "%s\nTipo: %s\nAlvo: %s\nNatureza: %s\nPoder: %d  MP: %d", r.description,
                         types[r.type % 3], targets[r.target % 2], nature_name((nature_t)r.nature), r.power,
                         r.mp_cost);
        for (int i = 0; i < r.n_status && n < (int)sizeof(text); i++) {
            n += snprintf(text + n, sizeof(text) - (size_t)n, "\n%s: %d%%", state_name((state_t)r.status[i].state),
                          r.status[i].value);
        }
        ui_details(r.name, text);
    } else if ((ev->on_key == KEY_LEFT || ev->on_key == KEY_RIGHT) && list.total) {
        bool turning_on = !is_active(ids[list.sel]);
        int free_slots = 0;
        for (int i = 0; i < COUNT_SKILL_ACTIVE; i++) {
            free_slots += g_guara.active_skills[i] == SLOT_EMPTY;
        }
        if (turning_on && free_slots == 0) {
            ui_alert("HABILIDADES", "mp", "Já há 4 habilidades ativas.", "OK", NULL, NULL);
        } else {
            toggle(1, NULL);
        }
    }
}

/* Desenha a lista ou o aviso de vazio. */
static void on_draw(gfx_t *g, uint32_t now)
{
    ui_title(g, "HABILIDADES", now);
    if (list.total == 0) {
        gfx_text_center(g, 0, GFX_WIDTH, 32, "Nenhuma habilidade", GFX_WHITE);
        return;
    }
    ui_list_draw(g, &list, UI_TITLE_HEIGHT, UI_LINES_VISIBLE, item, NULL, now);
}

/* Monta a lista ao abrir. */
static void on_open(void *param)
{
    (void)param;
    list = (ui_list_t){0};
    mount();
}

const screen_t SCREEN_SKILLS = {
    .name = "habilidades",
    .on_open = on_open,
    .on_key = on_key,
    .on_draw = on_draw,
};
