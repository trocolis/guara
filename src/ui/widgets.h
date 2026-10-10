#ifndef GUARA_WIDGETS_H
#define GUARA_WIDGETS_H

#include <stdbool.h>
#include <stdint.h>

#include "buttons.h"
#include "gfx.h"

/* Componentes padrão da UI (README > UI > Padrões do system). */

#define UI_TITLE_HEIGHT 11   /* barra de título e de abas: Y=0..10 */
#define UI_LINE_HEIGHT 18   /* caixa de 16 px + 2 px de espaço entre os itens */
#define UI_SELECTION_HEIGHT 16 /* caixa de seleção, da altura do ícone */
#define UI_TITLE_TEXT_Y 2    /* linha do texto: 1 px de folga até o contorno e a linha de base das abas */
#define UI_LIST_Y 12         /* primeira linha da lista: 1 px abaixo da barra */
#define UI_LINES_VISIBLE 3   /* Y=12..63 */
#define UI_FOOTER_Y 54

/* Letreiro (marquee): rola o texto quando não cabe; reinicia quando a chave muda. */
typedef struct {
    uint32_t start;
    uint32_t key;
} marquee_t;

uint32_t ui_hash(const char *text);
void ui_marquee_text(gfx_t *g, int x, int y, int width, const char *text, gfx_color_t color, bool scroll,
                      marquee_t *m, uint32_t now);
/* Trunca para caber em `largura` px (em caracteres). */
void ui_truncate(char *dst, int length, const char *src, int width);

void ui_title(gfx_t *g, const char *text, uint32_t now);
void ui_tabs(gfx_t *g, const char *const *names, int count, int selected, uint32_t now);
void ui_scrollbar(gfx_t *g, int y0, int height, int total, int visible_count, int top);
void ui_footer(gfx_t *g, const char *text_b, const char *text_a, uint32_t now);
void ui_footer_modal(gfx_t *g, int y_line, const char *text_b, const char *text_a, uint32_t now);
void ui_progress(gfx_t *g, int x, int y, int width, int height, int value, int maximum);
/* Desenha um bitmap da VROM pelo nome; retorna false se não existir. */
bool ui_icon(gfx_t *g, const char *name, int x, int y);
void ui_icon_mode(gfx_t *g, const char *name, int x, int y, gfx_mode_t mode, unsigned flags);
/* Quebra o texto em linhas de até `largura` px. Retorna o número de linhas desenhadas. */
int ui_paragraph(gfx_t *g, int x, int y, int width, int max_lines, const char *text, bool centered);

/* Reticências de espera: "", ".", ".." e "..." a cada UI_DOTS_STEP_MS, em ciclo. */
#define UI_DOTS_STEP_MS 400
const char *ui_dots(uint32_t now_ms);
/* `text` com as reticências animadas, centralizado em [x, x + width) como se tivesse "..." (sem tremer). */
void ui_text_dots(gfx_t *g, int x, int width, int y, const char *text, uint32_t now_ms, gfx_color_t color);

/* Lista padrão de sub-menu: 3 linhas de 16 px com ícone, texto e informação à direita. */
typedef struct {
    const char *icon;
    char text[64];
    char info[8];
} ui_item_t;

typedef void (*ui_item_fn)(int position, ui_item_t *item, void *ctx);

typedef struct {
    int total;
    int sel;
    int top;
    marquee_t marquee;
} ui_list_t;

void ui_list_adjust(ui_list_t *l, int lines);
void ui_list_draw(gfx_t *g, ui_list_t *l, int y0, int lines, ui_item_fn item, void *ctx, uint32_t now);
/* Trata Cima/Baixo. Retorna true se consumiu o evento. */
bool ui_list_key(ui_list_t *l, const key_event_t *ev, int lines);

#endif
