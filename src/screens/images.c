/*
 * Imagens (/sdcard/image) e o visualizador de BMP 1-bit.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "files.h"
#include "audio.h"
#include "widgets.h"
#include "modal.h"
#include "sdcard.h"
#include "screens.h"

/* Imagens: .bmp da pasta /sdcard/image e visualizador (também usado pelo Cartão SD). */

#define FOLDER_IMAGES SD_ROOT "/image"

/* ---------------- Visualizador ---------------- */

static gfx_bitmap_t image;
static int vx, vy;

/* Direcional rola imagens maiores que a tela; A ou B fecham. */
static void viewer_key(const key_event_t *ev)
{
    if (ev->type == KEY_RELEASED) {
        return;
    }
    int step = 8;
    switch (ev->key) {
    case KEY_LEFT: vx -= step; break;
    case KEY_RIGHT: vx += step; break;
    case KEY_UP: vy -= step; break;
    case KEY_DOWN: vy += step; break;
    case KEY_B:
    case KEY_A:
        if (ev->type == KEY_PRESSED) {
            ui_back();
        }
        return;
    default: break;
    }
    int max_x = image.width > GFX_WIDTH ? image.width - GFX_WIDTH : 0;
    int max_y = image.height > GFX_HEIGHT ? image.height - GFX_HEIGHT : 0;
    vx = vx < 0 ? 0 : (vx > max_x ? max_x : vx);
    vy = vy < 0 ? 0 : (vy > max_y ? max_y : vy);
}

/* Desenha a imagem centralizada ou na posição de rolagem. */
static void viewer_draw(gfx_t *g, uint32_t now)
{
    (void)now;
    int x = image.width < GFX_WIDTH ? (GFX_WIDTH - image.width) / 2 : -vx;
    int y = image.height < GFX_HEIGHT ? (GFX_HEIGHT - image.height) / 2 : -vy;
    gfx_bitmap(g, &image, x, y, GFX_COPY, 0);
}

/* Libera a imagem carregada. */
static void viewer_close(void)
{
    files_release_bmp(&image);
}

static const screen_t SCREEN_VIEWER = {
    .keep_open = true,
    .name = "visualizador",
    .on_key = viewer_key,
    .on_draw = viewer_draw,
    .on_close = viewer_close,
};

/* Carrega e abre o visualizador. */
void screens_view_image(const char *path)
{
    if (!files_load_bmp(path, &image)) {
        audio_error();
        ui_alert("IMAGEM", "images", "Use BMP monocromático (1 bit).", "OK", NULL, NULL);
        return;
    }
    vx = vy = 0;
    ui_open(&SCREEN_VIEWER, NULL);
}

/* ---------------- Lista ---------------- */

static file_list_t *items;
static ui_list_t list;

/* Relê a pasta de imagens. */
static void reload(void)
{
    static const char *const ext[] = {".bmp", NULL};
    free(items);
    items = files_list(FOLDER_IMAGES, ext, false, 64);
    list.total = items ? items->count : 0;
    ui_list_adjust(&list, UI_LINES_VISIBLE);
}

/* Caminho da imagem selecionada. */
static void selected_path(char *buf, int length)
{
    snprintf(buf, (size_t)length, "%s/%s", FOLDER_IMAGES, items->items[list.sel].name);
}

/* Item da lista de imagens. */
static void item(int i, ui_item_t *it, void *ctx)
{
    (void)ctx;
    it->icon = "images";
    files_friendly_name(items->items[i].name, it->text, sizeof(it->text));
    files_format_size(items->items[i].size, it->info, sizeof(it->info));
}

/* Exclui a imagem após a confirmação. */
static void delete(int r, void *ctx)
{
    (void)ctx;
    if (r == 1) {
        char c[160];
        selected_path(c, sizeof(c));
        remove(c);
        reload();
    }
}

/* Trata Abrir, Detalhes e Excluir. */
static void option(int r, void *ctx)
{
    (void)ctx;
    char c[160];
    selected_path(c, sizeof(c));
    const file_entry_t *e = &items->items[list.sel];
    if (r == 0) {
        screens_view_image(c);
    } else if (r == 1) {
        char text[160];
        gfx_bitmap_t b;
        if (files_load_bmp(c, &b)) {
            snprintf(text, sizeof(text), "%s\n%dx%d px\n%lu bytes", e->name, b.width, b.height,
                     (unsigned long)e->size);
            files_release_bmp(&b);
        } else {
            snprintf(text, sizeof(text), "%s\n%lu bytes\nFormato não suportado", e->name, (unsigned long)e->size);
        }
        ui_details("DETALHES", text);
    } else if (r == 2) {
        ui_confirm("EXCLUIR", "images", e->name, "NAO", "SIM", delete, NULL);
    }
}

/* Teclas da lista de imagens. */
static void on_key(const key_event_t *ev)
{
    if (ui_list_key(&list, ev, UI_LINES_VISIBLE) || ev->type != KEY_PRESSED) {
        return;
    }
    if (ev->key == KEY_B) {
        ui_back();
    } else if (ev->key == KEY_A && list.total) {
        static const char *const options[] = {"Abrir", "Detalhes", "Excluir"};
        ui_choice(items->items[list.sel].name, options, 3, option, NULL);
    }
}

/* Desenha a lista ou o aviso de pasta vazia. */
static void on_draw(gfx_t *g, uint32_t now)
{
    ui_title(g, "IMAGENS", now);
    if (!sd_mounted()) {
        gfx_text_center(g, 0, GFX_WIDTH, 32, "Insira o cartão SD", GFX_WHITE);
    } else if (list.total == 0) {
        gfx_text_center(g, 0, GFX_WIDTH, 28, "Nenhuma imagem em", GFX_WHITE);
        gfx_text_center(g, 0, GFX_WIDTH, 38, "/image", GFX_WHITE);
    } else {
        ui_list_draw(g, &list, UI_LIST_Y, UI_LINES_VISIBLE, item, NULL, now);
    }
}

/* Monta o cartão (se preciso) e lista as imagens. */
static void on_open(void *param)
{
    (void)param;
    if (!sd_mounted()) {
        sd_mount();
    }
    list = (ui_list_t){0};
    reload();
}

/* Libera a lista. */
static void on_close(void)
{
    free(items);
    items = NULL;
}

const screen_t SCREEN_IMAGES = {
    .name = "imagens",
    .on_open = on_open,
    .on_close = on_close,
    .on_key = on_key,
    .on_draw = on_draw,
};
