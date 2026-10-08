/*
 * Teclado virtual: 3 linhas de 10 teclas e a linha de atalhos (modo, espaço,
 * apagar, concluir). A insere, B cancela.
 */
#include <stdio.h>
#include <string.h>

#include "widgets.h"
#include "modal.h"
#include "ui.h"

/*
 * Teclado virtual: 3 linhas de 10 teclas + linha de atalhos
 * (modo, espaço, apagar, concluir). A insere, B cancela.
 */

#define COLUMNS 10
#define LINES_CHARS 3
#define CELL_L 12
#define CELL_A 10
#define X0 4
#define Y0 14
#define Y_SHORTCUTS 46

static const char *const PAGES[] = {
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ.,-_",
    "abcdefghijklmnopqrstuvwxyz.,-_",
    "1234567890!?@#$%&*()+=/:;'\"<>",
};
static const char *const NAME_PAGE[] = {"ABC", "abc", "123"};
#define COUNT_PAGES 3

static const char *const SHORTCUTS[] = {NULL, "ESPACO", "APAGAR", "OK"};

static struct {
    char title[32];
    char text[72];
    int max;
    int page;
    int row, col; /* lin 3 = atalhos (col 0..3) */
    ui_text_fn fn;
    void *ctx;
} kb;

/* Fecha o teclado e entrega o texto (ou NULL se cancelado). */
static void respond(const char *text)
{
    ui_text_fn fn = kb.fn;
    void *ctx = kb.ctx;
    char copy[sizeof(kb.text)];
    snprintf(copy, sizeof(copy), "%s", text ? text : "");
    ui_back();
    if (fn) {
        fn(text ? copy : NULL, ctx);
    }
}

/* Caractere da tecla na página atual. */
static char char_at(int row, int col)
{
    const char *p = PAGES[kb.page];
    int i = row * COLUMNS + col;
    return i < (int)strlen(p) ? p[i] : ' ';
}

/* Acrescenta um caractere respeitando o tamanho máximo. */
static void insert(char c)
{
    int n = (int)strlen(kb.text);
    if (n < kb.max && n < (int)sizeof(kb.text) - 1) {
        kb.text[n] = c;
        kb.text[n + 1] = '\0';
    }
}

/* Navegação pela grade e ação da tecla selecionada. */
static void on_key(const key_event_t *ev)
{
    if (ev->type == KEY_RELEASED) {
        return;
    }
    switch (ev->key) {
    case KEY_B:
        if (ev->type == KEY_PRESSED) {
            respond(NULL);
        }
        return;
    case KEY_UP:
        kb.row = (kb.row + 3) % 4;
        break;
    case KEY_DOWN:
        kb.row = (kb.row + 1) % 4;
        break;
    case KEY_LEFT:
        kb.col = kb.row == 3 ? (kb.col + 3) % 4 : (kb.col + COLUMNS - 1) % COLUMNS;
        break;
    case KEY_RIGHT:
        kb.col = kb.row == 3 ? (kb.col + 1) % 4 : (kb.col + 1) % COLUMNS;
        break;
    case KEY_A:
        if (kb.row < 3) {
            insert(char_at(kb.row, kb.col));
        } else if (kb.col == 0) {
            kb.page = (kb.page + 1) % COUNT_PAGES;
        } else if (kb.col == 1) {
            insert(' ');
        } else if (kb.col == 2) {
            const char *p = kb.text + strlen(kb.text);
            /* apaga um caractere UTF-8 inteiro */
            while (p > kb.text) {
                p--;
                if (((uint8_t)*p & 0xC0) != 0x80) {
                    break;
                }
            }
            kb.text[p - kb.text] = '\0';
        } else if (ev->type == KEY_PRESSED) {
            respond(kb.text);
            return;
        }
        break;
    default:
        break;
    }
    if (kb.row == 3 && kb.col > 3) {
        kb.col = 3;
    }
}

/* Desenha o texto digitado, a grade de teclas e os atalhos. */
static void on_draw(gfx_t *g, uint32_t now)
{
    gfx_clear(g);
    /* Barra superior com o texto digitado (mostra o fim se for longo) */
    gfx_rect_filled(g, 0, 0, GFX_WIDTH, UI_TITLE_HEIGHT, GFX_WHITE);
    const char *show = kb.text[0] ? kb.text : kb.title;
    int n = utf8_size(show);
    if (n > 19) {
        show = utf8_advance(show, n - 19);
    }
    int w = gfx_text(g, 2, UI_TITLE_TEXT_Y, show, GFX_BLACK);
    if (kb.text[0] && (now / 400) % 2) {
        gfx_line_v(g, 2 + w + 1, UI_TITLE_TEXT_Y, 8, GFX_BLACK);
    }

    for (int row = 0; row < LINES_CHARS; row++) {
        for (int col = 0; col < COLUMNS; col++) {
            int x = X0 + col * CELL_L, y = Y0 + row * CELL_A;
            char s[2] = {char_at(row, col), 0};
            bool sel = kb.row == row && kb.col == col;
            if (sel) {
                gfx_rect_rounded_filled(g, x, y, CELL_L, CELL_A, 2, GFX_WHITE);
            }
            gfx_text(g, x + 4, y + 1, s, sel ? GFX_BLACK : GFX_WHITE);
        }
    }
    for (int i = 0; i < 4; i++) {
        int x = i * 32, y = Y_SHORTCUTS;
        const char *r = i == 0 ? NAME_PAGE[(kb.page + 1) % COUNT_PAGES] : SHORTCUTS[i];
        bool sel = kb.row == 3 && kb.col == i;
        if (sel) {
            gfx_rect_rounded_filled(g, x + 1, y, 30, 11, 2, GFX_WHITE);
        } else {
            gfx_rect_rounded(g, x + 1, y, 30, 11, 2, GFX_WHITE);
        }
        char buf[8];
        ui_truncate(buf, sizeof(buf), r, 28);
        gfx_text_center(g, x + 1, 30, y + 2, buf, sel ? GFX_BLACK : GFX_WHITE);
    }
}

static const screen_t SCREEN_KEYBOARD = {
    .name = "teclado",
    .on_key = on_key,
    .on_draw = on_draw,
};

/* Abre o teclado virtual. */
void ui_keyboard(const char *title, const char *initial, int max_length, ui_text_fn fn, void *ctx)
{
    memset(&kb, 0, sizeof(kb));
    snprintf(kb.title, sizeof(kb.title), "%s", title ? title : "");
    snprintf(kb.text, sizeof(kb.text), "%s", initial ? initial : "");
    kb.max = max_length > 0 && max_length < (int)sizeof(kb.text) ? max_length : (int)sizeof(kb.text) - 1;
    kb.fn = fn;
    kb.ctx = ctx;
    ui_open(&SCREEN_KEYBOARD, NULL);
}
