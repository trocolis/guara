/*
 * Tela do Guará com as abas GUARA, ESTADO, NIVEL e NATUREZA.
 */
#include <stdio.h>

#include "app.h"
#include "widgets.h"
#include "rules.h"
#include "sprite.h"
#include "screens.h"

/* Tela do Guará: abas GUARA, ESTADO, NIVEL e NATUREZA (README > Menu principal > Guará). */

static const char *const TABS[] = {"GUARA", "ESTADO", "NIVEL", "NATUREZA"};
static int tab;

/* ←/→ trocam de aba; B volta. */
static void on_key(const key_event_t *ev)
{
    if (ev->type == KEY_RELEASED) {
        return;
    }
    if (ev->on_key == KEY_LEFT) {
        tab = (tab + 3) % 4;
    } else if (ev->on_key == KEY_RIGHT) {
        tab = (tab + 1) % 4;
    } else if (ev->on_key == KEY_B && ev->type == KEY_PRESSED) {
        ui_back();
    }
}

/* Aba GUARA: sprite, nome e UID, nível e XP, idade. */
static void draw_pet_tab(gfx_t *g, uint32_t now)
{
    const guara_t *pet = &g_guara;
    char buf[32];
    sprite_guara(g, pet->color, pet->head, pet->stage, EYE_NORMAL, 10, 50, sprite_frame(now), 0);

    ui_icon(g, "id", 22, 13);
    gfx_text(g, 40, 13, pet->name, GFX_WHITE);
    snprintf(buf, sizeof(buf), "ID: %08lX", (unsigned long)pet->uid);
    gfx_text(g, 40, 22, buf, GFX_WHITE);

    ui_icon(g, "level", 22, 31);
    snprintf(buf, sizeof(buf), "NV: %d | XP: %lu", pet->level, (unsigned long)pet->xp);
    gfx_text(g, 40, 31, buf, GFX_WHITE);
    ui_progress(g, 40, 40, 84, 5, (int)pet->xp, (int)guara_xp_to_next_level(pet->level));

    ui_icon(g, "calendar", 22, 47);
    snprintf(buf, sizeof(buf), "Dias: %lu", (unsigned long)(pet->age_minutes / 1440));
    gfx_text(g, 40, 47, buf, GFX_WHITE);
    snprintf(buf, sizeof(buf), "Horas: %02lu:%02lu", (unsigned long)(pet->age_minutes % 1440 / 60),
             (unsigned long)(pet->age_minutes % 60));
    gfx_text(g, 40, 56, buf, GFX_WHITE);
}

/* Célula com ícone, texto e barra de progresso (abas ESTADO e NATUREZA). */
static void cell(gfx_t *g, int col, int row, const char *icon, const char *text, int value, int maximum)
{
    int x = col * 64, y = 13 + row * 17;
    ui_icon(g, icon, x + 1, y);
    gfx_text(g, x + 19, y, text, GFX_WHITE);
    if (maximum > 0) {
        ui_progress(g, x + 19, y + 10, 42, 5, value, maximum);
    }
}

/* Aba ESTADO: HP, MP, energia, humor, alimentação e saúde. */
static void draw_status_tab(gfx_t *g)
{
    const guara_t *pet = &g_guara;
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", pet->hp);
    cell(g, 0, 0, "hp", buf, pet->hp, pet->hp_max);
    snprintf(buf, sizeof(buf), "%d", pet->mp);
    cell(g, 1, 0, "mp", buf, pet->mp, pet->mp_max);
    snprintf(buf, sizeof(buf), "%d%%", pet->energy / 100);
    cell(g, 0, 1, "energy", buf, pet->energy, VITAL_MAX);
    snprintf(buf, sizeof(buf), "%d%%", pet->mood / 100);
    cell(g, 1, 1, "humor", buf, pet->mood, VITAL_MAX);
    snprintf(buf, sizeof(buf), "%d%%", pet->satiety / 100);
    cell(g, 0, 2, "hamburger", buf, pet->satiety, VITAL_MAX);
    snprintf(buf, sizeof(buf), "%d%%", pet->health / 100);
    cell(g, 1, 2, "health", buf, pet->health, VITAL_MAX);
}

/* Aba NIVEL: os seis atributos com equipamentos aplicados. */
static void draw_level_tab(gfx_t *g)
{
    static const char *const icons[] = {"attack", "defense", "strength", "agility", "intelligence", "luck"};
    static const char *const abbrevs[] = {"ATK", "DEF", "FOR", "AGI", "INT", "SOR"};
    for (int i = 0; i < ATTRIBUTE_COUNT; i++) {
        int x = (i % 2) * 64, y = 13 + (i / 2) * 17;
        char buf[8];
        ui_icon(g, icons[i], x + 1, y);
        gfx_text(g, x + 19, y, abbrevs[i], GFX_WHITE);
        snprintf(buf, sizeof(buf), "%d", guara_effective_attribute(&g_guara, (effect_target_t)(ATTR_ATK + i)));
        gfx_text(g, x + 19, y + 9, buf, GFX_WHITE);
    }
}

/* Aba NATUREZA: natureza atual e afinidades, ou a composição da natureza secundária. */
static void draw_nature_tab(gfx_t *g)
{
    const guara_t *pet = &g_guara;
    ui_icon(g, nature_icon(pet->nature), 1, 13);
    gfx_text(g, 19, 17, nature_name(pet->nature), GFX_WHITE);

    if (!guara_affinity_enabled(pet)) {
        /* Natureza secundária: nascimento + afinidade que a gerou */
        unsigned e = nature_elements(pet->nature) & ~(1u << pet->birth_nature);
        int source = NAT_NEUTRAL;
        for (int n = NAT_FIRE; n <= NAT_AIR; n++) {
            if (e & (1u << n)) {
                source = n;
            }
        }
        ui_icon(g, nature_icon(pet->birth_nature), 36, 38);
        gfx_text(g, 61, 42, "+", GFX_WHITE);
        ui_icon(g, nature_icon(source), 76, 38);
        return;
    }
    int cell_i = 0;
    for (int n = NAT_FIRE; n <= NAT_NEUTRAL; n++) {
        if (n == pet->birth_nature) {
            continue;
        }
        int x = (cell_i % 2) * 64, y = 30 + (cell_i / 2) * 17;
        char buf[8];
        snprintf(buf, sizeof(buf), "%d%%", pet->affinities[n] / 100);
        ui_icon(g, nature_icon((nature_t)n), x + 1, y);
        gfx_text(g, x + 19, y, buf, GFX_WHITE);
        ui_progress(g, x + 19, y + 10, 42, 5, pet->affinities[n], VITAL_MAX);
        cell_i++;
    }
}

/* Desenha as abas e o conteúdo da aba ativa. */
static void on_draw(gfx_t *g, uint32_t now)
{
    ui_tabs(g, TABS, 4, tab, now);
    switch (tab) {
    case 0: draw_pet_tab(g, now); break;
    case 1: draw_status_tab(g); break;
    case 2: draw_level_tab(g); break;
    default: draw_nature_tab(g); break;
    }
}

const screen_t SCREEN_GUARA = {
    .name = "guara",
    .on_key = on_key,
    .on_draw = on_draw,
};
