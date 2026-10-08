/*
 * Menu principal em carrossel com animação de deslizamento.
 */
#include <stdio.h>

#include "app.h"
#include "widgets.h"
#include "persistence.h"
#include "screens.h"

/* Menu principal em carrossel (mockup bitmaps/exemple/main_menu.bmp). */

typedef struct {
    const char *name;
    const char *icon;
    const screen_t *screen; /* tela a abrir, ou */
    void (*on_open)(void);  /* função que abre um sub-menu */
} item_menu_t;

static const item_menu_t ITEMS[] = {
    {"GUARA", "guara", &SCREEN_GUARA, NULL},
    {"INVENTARIO", "inventory", &SCREEN_INVENTORY, NULL},
    {"EQUIPAMENTOS", "equipment", &SCREEN_EQUIPMENT, NULL},
    {"HABILIDADES", "mp", &SCREEN_SKILLS, NULL},
    {"ARENA", "arena", NULL, arena_open_menu},
    {"BIBLIOTECA", "library", &SCREEN_LIBRARY, NULL},
    {"JOGOS", "games", &SCREEN_GAMES, NULL},
    {"MUSICAS", "music", &SCREEN_MUSIC, NULL},
    {"IMAGENS", "images", &SCREEN_IMAGES, NULL},
    {"RELOGIO", "clock", NULL, clock_open_menu},
    {"CARTAO SD", "sd_card", &SCREEN_CARD_SD, NULL},
    {"AJUSTES", "adjustments", NULL, settings_open_menu},
};
#define COUNT ((int)(sizeof(ITEMS) / sizeof(ITEMS[0])))

#define ANIM_MS 160
#define CENTER_X 48   /* ícone 2x (32x32) */
#define CENTER_Y 6
#define SIDE_DIST 40  /* distância entre os centros dos ícones */
#define SIDE_Y 14

static int sel;
static int anim_dir;  /* -1 veio do anterior, +1 do próximo */
static uint32_t anim_start;

/* ←/→ giram o carrossel, A abre o item, B volta para a tela principal. */
static void on_key(const key_event_t *ev)
{
    if (ev->type == KEY_RELEASED) {
        return;
    }
    switch (ev->on_key) {
    case KEY_LEFT: /* item da esquerda (anterior) vem para o centro */
        sel = (sel - 1 + COUNT) % COUNT;
        anim_dir = -1;
        anim_start = app_ms();
        break;
    case KEY_RIGHT:
        sel = (sel + 1) % COUNT;
        anim_dir = 1;
        anim_start = app_ms();
        break;
    case KEY_A:
        if (ev->type == KEY_PRESSED) {
            if (ITEMS[sel].screen) {
                ui_open(ITEMS[sel].screen, NULL);
            } else {
                ITEMS[sel].on_open();
            }
        }
        break;
    case KEY_B:
        if (ev->type == KEY_PRESSED) {
            ui_back();
        }
        break;
    default:
        break;
    }
}

/* Desenha o ícone de um item do menu em 1x ou 2x. */
static void icon(gfx_t *g, int position, int cx, bool large)
{
    const gfx_bitmap_t *b = vrom(ITEMS[(position + COUNT) % COUNT].icon);
    if (!b) {
        return;
    }
    if (large) {
        gfx_bitmap(g, b, cx - b->width, CENTER_Y, GFX_OR, GFX_SCALE2X);
    } else {
        gfx_bitmap(g, b, cx - b->width / 2, SIDE_Y, GFX_OR, 0);
    }
}

/* Desenha os ícones (com animação), as setas, o nome e os pontos indicadores. */
static void on_draw(gfx_t *g, uint32_t now)
{
    int offset = 0;
    uint32_t t = now - anim_start;
    if (anim_dir && t < ANIM_MS) {
        offset = anim_dir * SIDE_DIST * (int)(ANIM_MS - t) / ANIM_MS;
    } else {
        anim_dir = 0;
    }
    bool half = anim_dir == 0 || t >= ANIM_MS / 2;
    int center = 64 + offset;

    gfx_clip(g, 9, 0, 119, 44);
    for (int k = -2; k <= 2; k++) {
        int cx = center + k * SIDE_DIST;
        bool large = (k == 0 && half) || (k == -anim_dir && anim_dir != 0 && !half);
        if (anim_dir == 0) {
            large = k == 0;
        }
        icon(g, sel + k, cx, large);
    }
    gfx_reset_clip(g);

    /* Setas: lado direito do aparelho (esquerda do usuário) aponta para fora */
    gfx_arrow(g, 2, 17, 9, -1, GFX_WHITE);
    gfx_arrow(g, 121, 17, 9, 1, GFX_WHITE);

    /* Nome do item em caixa branca */
    const char *name = ITEMS[sel].name;
    int w = gfx_text_width(name) + 6;
    gfx_rect_filled(g, 64 - w / 2, 44, w, 11, GFX_WHITE);
    gfx_text_center(g, 0, GFX_WIDTH, 46, name, GFX_BLACK);

    /* Indicador de pontos */
    int width = COUNT * 6 - 2, x0 = 64 - width / 2;
    for (int i = 0; i < COUNT; i++) {
        if (i == sel) {
            gfx_rect_filled(g, x0 + i * 6 - 1, 58, 4, 4, GFX_WHITE);
        } else {
            gfx_pixel(g, x0 + i * 6, 59, GFX_WHITE);
            gfx_pixel(g, x0 + i * 6 + 1, 59, GFX_WHITE);
        }
    }
}

/* Ao voltar de um menu, grava se houver alterações (Pilar 2). */
static void on_resume(void)
{
    persist_save_if_dirty(&g_guara);
}

const screen_t SCREEN_MENU = {
    .name = "menu",
    .on_key = on_key,
    .on_draw = on_draw,
    .on_resume = on_resume,
};
