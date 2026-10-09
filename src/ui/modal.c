/*
 * Modais padrão (alerta, confirmação, escolha, quantidade, detalhes). Cada camada
 * da pilha tem seu próprio estado, então modais podem abrir outros modais.
 */
#include "modal.h"

#include <stdio.h>
#include <string.h>

#include "audio.h"
#include "widgets.h"
#include "ui.h"

#define MX 8
#define MY 6   /* 112x52 centralizado: Y=6..57 (sombra até Y=59) */
#define MW 112
#define MH 52
#define LINE_FOOTER (MY + MH - 13) /* linha do rodapé (Y=45): sprites B/A de 10 px abaixo dela */
#define TEXT_LINES 3               /* linhas de mensagem em alertas e confirmações */
#define DETAIL_LINES 4             /* linhas visíveis no modal de detalhes (sem rodapé) */
#define MAX_OPTIONS 16

typedef enum { M_ALERT, M_CONFIRM, M_CHOICE, M_COUNT, M_DETAILS } modal_type_t;

typedef struct {
    modal_type_t type;
    char title[40];
    char icon[32];
    char text[256];
    char text_a[16];
    char text_b[16];
    const char *options[MAX_OPTIONS];
    char options_buf[MAX_OPTIONS][24];
    int n_options;
    ui_list_t list;
    int minimum, maximum, value;
    int scrollbar, text_line_count;
    ui_result_fn fn;
    void *ctx;
} modal_t;

static modal_t modals[16];
static modal_t incoming;

/* Estado do modal da camada atual. */
static modal_t *current(void)
{
    return &modals[ui_current_layer() & 15];
}

/* Instala o modal preparado na camada que acabou de abrir. */
static void on_open(void *param)
{
    (void)param;
    *current() = incoming;
}

/* Fecha o modal e entrega a resposta ao callback (que pode abrir outra tela). */
static void respond(int answer)
{
    modal_t m = *current();
    ui_back();
    if (m.fn) {
        m.fn(answer, m.ctx);
    }
}

/* Teclas do modal conforme o tipo (B sempre cancela). */
static void on_key(const key_event_t *ev)
{
    modal_t *m = current();
    if (ev->type == KEY_RELEASED) {
        return;
    }
    if (ev->type == KEY_PRESSED && ev->key == KEY_B) {
        respond(-1);
        return;
    }
    switch (m->type) {
    case M_ALERT:
    case M_CONFIRM:
        if (ev->key == KEY_A && ev->type == KEY_PRESSED) {
            respond(1);
        }
        break;
    case M_CHOICE:
        if (ui_list_key(&m->list, ev, 3)) {
            break;
        }
        if (ev->key == KEY_A && ev->type == KEY_PRESSED) {
            respond(m->list.sel);
        }
        break;
    case M_COUNT: {
        int step = ev->type == KEY_REPEATED ? 5 : 1;
        if (ev->key == KEY_UP || ev->key == KEY_RIGHT) {
            m->value = m->value + step > m->maximum ? (ev->type == KEY_REPEATED ? m->maximum : m->minimum) : m->value + step;
        } else if (ev->key == KEY_DOWN || ev->key == KEY_LEFT) {
            m->value = m->value - step < m->minimum ? (ev->type == KEY_REPEATED ? m->minimum : m->maximum) : m->value - step;
        } else if (ev->key == KEY_A && ev->type == KEY_PRESSED) {
            respond(m->value);
        }
        break;
    }
    case M_DETAILS:
        if (ev->key == KEY_UP && m->scrollbar > 0) {
            m->scrollbar--;
        } else if (ev->key == KEY_DOWN && m->scrollbar + DETAIL_LINES < m->text_line_count) {
            m->scrollbar++;
        } else if (ev->key == KEY_A && ev->type == KEY_PRESSED) {
            respond(1);
        }
        break;
    }
}

/* Moldura 112x48 com sombra de 2 px e barra de título branca (como a das telas). */
static void draw_frame(gfx_t *g, const char *title, uint32_t now)
{
    static marquee_t marquee;
    gfx_rect_filled(g, MX - 2, MY - 2, MW + 4, MH + 4, GFX_BLACK);
    gfx_rect(g, MX, MY, MW, MH, GFX_WHITE);
    /* barra de título branca com texto preto, da mesma altura da barra das telas */
    gfx_rect_filled(g, MX, MY, MW, UI_TITLE_HEIGHT, GFX_WHITE);
    int w = gfx_text_width(title);
    if (w <= MW - 6) {
        gfx_text(g, MX + (MW - w) / 2, MY + UI_TITLE_TEXT_Y, title, GFX_BLACK);
    } else {
        ui_marquee_text(g, MX + 3, MY + UI_TITLE_TEXT_Y, MW - 6, title, GFX_BLACK, true, &marquee, now);
    }
}

/* Texto de uma opção do modal de escolha. */
static void item_option(int i, ui_item_t *it, void *ctx)
{
    modal_t *m = ctx;
    snprintf(it->text, sizeof(it->text), "%s", m->options[i]);
}

/* Contagem de linhas de um parágrafo, desenhando-o fora da tela. */
static int count_lines(const char *text, int width)
{
    static gfx_t scratch;
    gfx_init(&scratch);
    gfx_clip(&scratch, 0, 0, 0, 0);
    return ui_paragraph(&scratch, 0, 0, width, 64, text, false);
}

/* Desenha o modal conforme o tipo. */
static void on_draw(gfx_t *g, uint32_t now)
{
    modal_t *m = current();
    draw_frame(g, m->title, now);
    int body_y = MY + UI_TITLE_HEIGHT + 1; /* 1 px abaixo da barra de título */
    switch (m->type) {
    case M_ALERT:
    case M_CONFIRM: {
        int x = MX + 4, width = MW - 8;
        if (m->icon[0] && ui_icon(g, m->icon, MX + 4, body_y + 2)) {
            x = MX + 24;
            width = MW - 28;
        }
        /* centraliza verticalmente entre a barra de título e o rodapé (até 3 linhas) */
        int lines = m->text[0] ? count_lines(m->text, width) : 0;
        if (lines > TEXT_LINES) {
            lines = TEXT_LINES;
        }
        int y = body_y + (LINE_FOOTER - body_y - lines * FONT_5X8.line_height) / 2;
        ui_paragraph(g, x, y, width, TEXT_LINES, m->text, true);
        ui_footer_modal(g, LINE_FOOTER, m->text_b[0] ? m->text_b : NULL, m->text_a, now);
        break;
    }
    case M_CHOICE: {
        ui_list_adjust(&m->list, 3);
        for (int i = 0; i < 3 && m->list.top + i < m->n_options; i++) {
            int idx = m->list.top + i, y = body_y + i * 11;
            ui_item_t it = {0};
            item_option(idx, &it, m);
            bool sel = idx == m->list.sel;
            ui_marquee_text(g, MX + 6, y + 2, MW - 16, it.text, GFX_WHITE, sel, &m->list.marquee, now);
            if (sel) {
                gfx_rect_rounded(g, MX + 2, y, MW - 7, 11, 2, GFX_WHITE);
            }
        }
        if (m->n_options > 3) {
            int bar_height = 33, length = bar_height * 3 / m->n_options;
            int y = body_y + (bar_height - length) * m->list.top / (m->n_options - 3);
            gfx_line_v(g, MX + MW - 3, y, length, GFX_WHITE);
        }
        break;
    }
    case M_COUNT: {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", m->value);
        int w = gfx_text_width(buf) * 2 + 1;
        int x = MX + (MW - w) / 2;
        gfx_text_scaled(g, x, body_y + 6, buf, GFX_WHITE, 2);
        gfx_arrow(g, x - 10, body_y + 9, 9, -1, GFX_WHITE);
        gfx_arrow(g, x + w + 5, body_y + 9, 9, 1, GFX_WHITE);
        ui_footer_modal(g, LINE_FOOTER, m->text_b, m->text_a, now);
        break;
    }
    case M_DETAILS: {
        gfx_t *gg = g;
        int16_t cy0 = gg->clip_y0, cy1 = gg->clip_y1;
        gfx_clip(gg, MX + 1, body_y, MX + MW - 1, MY + MH - 1);
        ui_paragraph(gg, MX + 4, body_y + 1 - m->scrollbar * 9, MW - 12, 64, m->text, false);
        gg->clip_y0 = cy0;
        gg->clip_y1 = cy1;
        gfx_reset_clip(gg);
        if (m->text_line_count > DETAIL_LINES) {
            int bar_height = DETAIL_LINES * FONT_5X8.line_height, length = bar_height * DETAIL_LINES / m->text_line_count;
            int y = body_y + (bar_height - length) * m->scrollbar / (m->text_line_count - DETAIL_LINES);
            gfx_line_v(g, MX + MW - 3, y, length, GFX_WHITE);
        }
        break;
    }
    }
}

static const screen_t SCREEN_MODAL = {
    .name = "modal",
    .modal = true,
    .on_open = on_open,
    .on_key = on_key,
    .on_draw = on_draw,
};

/* Zera o modal pendente e define título e callback. */
static void prepare(modal_type_t type, const char *title, ui_result_fn fn, void *ctx)
{
    memset(&incoming, 0, sizeof(incoming));
    incoming.type = type;
    snprintf(incoming.title, sizeof(incoming.title), "%s", title ? title : "");
    incoming.fn = fn;
    incoming.ctx = ctx;
}

void ui_alert(const char *title, const char *icon, const char *message, const char *text_a, ui_result_fn fn,
               void *ctx)
{
    prepare(M_ALERT, title, fn, ctx);
    snprintf(incoming.icon, sizeof(incoming.icon), "%s", icon ? icon : "");
    snprintf(incoming.text, sizeof(incoming.text), "%s", message ? message : "");
    snprintf(incoming.text_a, sizeof(incoming.text_a), "%s", text_a ? text_a : "OK");
    ui_open(&SCREEN_MODAL, NULL);
}

void ui_confirm(const char *title, const char *icon, const char *message, const char *text_no,
                  const char *text_yes, ui_result_fn fn, void *ctx)
{
    prepare(M_CONFIRM, title, fn, ctx);
    snprintf(incoming.icon, sizeof(incoming.icon), "%s", icon ? icon : "");
    snprintf(incoming.text, sizeof(incoming.text), "%s", message ? message : "");
    snprintf(incoming.text_a, sizeof(incoming.text_a), "%s", text_yes ? text_yes : "SIM");
    snprintf(incoming.text_b, sizeof(incoming.text_b), "%s", text_no ? text_no : "NAO");
    ui_open(&SCREEN_MODAL, NULL);
}

/* Abre uma lista de opções compacta (sem rodapé). */
void ui_choice(const char *title, const char *const *options, int count, ui_result_fn fn, void *ctx)
{
    prepare(M_CHOICE, title, fn, ctx);
    incoming.n_options = count > MAX_OPTIONS ? MAX_OPTIONS : count;
    for (int i = 0; i < incoming.n_options; i++) {
        snprintf(incoming.options_buf[i], sizeof(incoming.options_buf[i]), "%s", options[i]);
    }
    incoming.list.total = incoming.n_options;
    ui_open(&SCREEN_MODAL, NULL);
    /* os ponteiros precisam apontar para a cópia já instalada na camada */
    modal_t *m = &modals[ui_depth() - 1];
    for (int i = 0; i < m->n_options; i++) {
        m->options[i] = m->options_buf[i];
    }
}

/* Abre a escolha de quantidade entre `minimum` e `maximum`. */
void ui_quantity_picker(const char *title, int minimum, int maximum, int initial, ui_result_fn fn, void *ctx)
{
    prepare(M_COUNT, title, fn, ctx);
    incoming.minimum = minimum;
    incoming.maximum = maximum;
    incoming.value = initial;
    snprintf(incoming.text_a, sizeof(incoming.text_a), "OK");
    snprintf(incoming.text_b, sizeof(incoming.text_b), "VOLTAR");
    ui_open(&SCREEN_MODAL, NULL);
}

/* Abre um texto rolável sem rodapé. */
void ui_details(const char *title, const char *text)
{
    prepare(M_DETAILS, title, NULL, NULL);
    snprintf(incoming.text, sizeof(incoming.text), "%s", text);
    incoming.text_line_count = count_lines(incoming.text, MW - 12);
    ui_open(&SCREEN_MODAL, NULL);
}
