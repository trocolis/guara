/*
 * Componentes padrão da interface (README > UI > Padrões do sistema): título,
 * abas, lista de 3 linhas, barra de rolagem, rodapé B/A, letreiro, progresso.
 */
#include "widgets.h"

#include <stdio.h>
#include <string.h>

#include "storage.h"

#define MARQUEE_PAUSE_MS 1000
#define MARQUEE_MS_PER_PX 40

/* Hash FNV-1a do texto (detecta troca de texto no letreiro). */
uint32_t ui_hash(const char *text)
{
    uint32_t h = 2166136261u;
    while (text && *text) {
        h = (h ^ (uint8_t)*text++) * 16777619u;
    }
    return h;
}

/* Copia o texto cortado em caracteres inteiros para caber em `width` px. */
void ui_truncate(char *dst, int length, const char *src, int width)
{
    int max_chars = (width + 1) / FONT_5X8.advance;
    const char *end = utf8_advance(src, max_chars);
    int n = (int)(end - src);
    if (n >= length) {
        n = length - 1;
    }
    memcpy(dst, src, (size_t)n);
    dst[n] = '\0';
}

void ui_marquee_text(gfx_t *g, int x, int y, int width, const char *text, gfx_color_t color, bool scroll,
                      marquee_t *m, uint32_t now)
{
    int w = gfx_text_width(text);
    int16_t cx0 = g->clip_x0, cy0 = g->clip_y0, cx1 = g->clip_x1, cy1 = g->clip_y1;
    if (w <= width) {
        gfx_text(g, x, y, text, color);
        return;
    }
    if (!scroll || !m) {
        char buf[96];
        ui_truncate(buf, sizeof(buf), text, width);
        gfx_text(g, x, y, buf, color);
        return;
    }
    uint32_t key = ui_hash(text);
    if (m->key != key) {
        m->key = key;
        m->start = now;
    }
    int overflow = w - width;
    uint32_t cycle = MARQUEE_PAUSE_MS * 2 + (uint32_t)overflow * MARQUEE_MS_PER_PX;
    uint32_t t = (now - m->start) % cycle;
    int offset = 0;
    if (t > MARQUEE_PAUSE_MS) {
        offset = (int)((t - MARQUEE_PAUSE_MS) / MARQUEE_MS_PER_PX);
        if (offset > overflow) {
            offset = overflow;
        }
    }
    gfx_clip(g, x > cx0 ? x : cx0, cy0, x + width < cx1 ? x + width : cx1, cy1);
    gfx_text(g, x - offset, y, text, color);
    g->clip_x0 = cx0;
    g->clip_y0 = cy0;
    g->clip_x1 = cx1;
    g->clip_y1 = cy1;
}

/* Barra de título branca com texto preto centralizado (ou rolando). */
void ui_title(gfx_t *g, const char *text, uint32_t now)
{
    static marquee_t marquee;
    gfx_rect_filled(g, 0, 0, GFX_WIDTH, UI_TITLE_HEIGHT, GFX_WHITE);
    int w = gfx_text_width(text);
    if (w <= GFX_WIDTH - 4) {
        gfx_text(g, (GFX_WIDTH - w) / 2, UI_TITLE_TEXT_Y, text, GFX_BLACK);
    } else {
        ui_marquee_text(g, 2, UI_TITLE_TEXT_Y, GFX_WIDTH - 4, text, GFX_BLACK, true, &marquee, now);
    }
}

#define TAB_CHARS_INACTIVE 4
#define TAB_CHARS_ACTIVE 10
#define TAB_MARGIN 3

/* Barra de abas: aba ativa cheia com até 10 caracteres, inativas com 4, setas nas bordas. */
void ui_tabs(gfx_t *g, const char *const *names, int n, int sel, uint32_t now)
{
    static marquee_t marquee;
    int xs[12], ws[12];
    int total = 0;
    if (n > 12) {
        n = 12;
    }
    for (int i = 0; i < n; i++) {
        int chars = utf8_size(names[i]);
        int limit = i == sel ? TAB_CHARS_ACTIVE : TAB_CHARS_INACTIVE;
        if (chars > limit) {
            chars = limit;
        }
        ws[i] = chars * FONT_5X8.advance - 1 + TAB_MARGIN * 2;
        xs[i] = total;
        total += ws[i] - 1; /* abas vizinhas compartilham a borda */
    }
    total++;
    int offset = 0;
    if (total > GFX_WIDTH) {
        offset = xs[sel] + ws[sel] / 2 - GFX_WIDTH / 2;
        if (offset < 0) {
            offset = 0;
        }
        if (offset > total - GFX_WIDTH) {
            offset = total - GFX_WIDTH;
        }
    }
    gfx_line_h(g, 0, UI_TITLE_HEIGHT - 1, GFX_WIDTH, GFX_WHITE);
    for (int i = 0; i < n; i++) {
        int x = xs[i] - offset;
        if (x + ws[i] < 0 || x >= GFX_WIDTH) {
            continue;
        }
        bool is_active = i == sel;
        /* contorno com cantos de cima arredondados; a aba ativa é preenchida */
        gfx_tab(g, x, 0, ws[i], UI_TITLE_HEIGHT - 1, 2, is_active, GFX_WHITE);
        if (is_active) {
            ui_marquee_text(g, x + TAB_MARGIN, UI_TITLE_TEXT_Y, ws[i] - TAB_MARGIN * 2, names[i], GFX_BLACK, true, &marquee, now);
        } else {
            char buf[24];
            ui_truncate(buf, sizeof(buf), names[i], ws[i] - TAB_MARGIN * 2);
            gfx_text(g, x + TAB_MARGIN, UI_TITLE_TEXT_Y, buf, GFX_WHITE);
        }
    }
    /* Setas cheias nas extremidades quando há abas fora da tela */
    if (offset > 0) {
        gfx_rect_filled(g, 0, 0, 6, UI_TITLE_HEIGHT - 1, GFX_BLACK);
        gfx_arrow(g, 1, 2, 7, -1, GFX_WHITE);
    }
    if (offset < total - GFX_WIDTH) {
        gfx_rect_filled(g, GFX_WIDTH - 6, 0, 6, UI_TITLE_HEIGHT - 1, GFX_BLACK);
        gfx_arrow(g, GFX_WIDTH - 5, 2, 7, 1, GFX_WHITE);
    }
}

/* Barra de rolagem de 1 px em X=127. */
void ui_scrollbar(gfx_t *g, int y0, int height, int total, int visible_count, int top)
{
    gfx_line_v(g, GFX_WIDTH - 1, y0, height, GFX_BLACK);
    if (total <= visible_count || total <= 0) {
        return;
    }
    int length = height * visible_count / total;
    if (length < 3) {
        length = 3;
    }
    int y = y0 + (height - length) * top / (total - visible_count);
    gfx_line_v(g, GFX_WIDTH - 1, y, length, GFX_WHITE);
}

static void captions(gfx_t *g, int x_b, int x_a_end, int y, int width, const char *text_b, const char *text_a,
                     uint32_t now)
{
    static marquee_t marquee_b, marquee_a;
    /* Com uma legenda só, ela usa a largura toda entre os dois sprites */
    if (!text_a || !text_b) {
        width = x_a_end - x_b + 1;
    }
    if (text_b) {
        ui_marquee_text(g, x_b, y, width, text_b, GFX_WHITE, true, &marquee_b, now);
    }
    if (text_a) {
        int w = gfx_text_width(text_a);
        if (w <= width) {
            gfx_text(g, x_a_end - w + 1, y, text_a, GFX_WHITE);
        } else {
            ui_marquee_text(g, x_a_end - width + 1, y, width, text_a, GFX_WHITE, true, &marquee_a, now);
        }
    }
}

/* Rodapé de tela cheia com os sprites B (X=2) e A (X=116). */
void ui_footer(gfx_t *g, const char *text_b, const char *text_a, uint32_t now)
{
    gfx_rect_filled(g, 0, UI_FOOTER_Y, GFX_WIDTH, GFX_HEIGHT - UI_FOOTER_Y, GFX_BLACK);
    gfx_line_h(g, 0, UI_FOOTER_Y - 1, GFX_WIDTH, GFX_WHITE);
    if (text_b) {
        ui_icon_mode(g, "button_b", 2, UI_FOOTER_Y, GFX_COPY, 0);
    }
    if (text_a) {
        ui_icon_mode(g, "button_a", 116, UI_FOOTER_Y, GFX_COPY, 0);
    }
    captions(g, 15, 112, UI_FOOTER_Y + 1, 47, text_b, text_a, now);
}

/* Rodapé de modal com os sprites B (X=11) e A (X=107). */
void ui_footer_modal(gfx_t *g, int y_line, const char *text_b, const char *text_a, uint32_t now)
{
    /* faixa preta de 11 px abaixo da linha; os sprites ficam 2 px abaixo dela */
    gfx_rect_filled(g, 9, y_line + 1, 110, 11, GFX_BLACK);
    gfx_line_h(g, 9, y_line, 110, GFX_WHITE);
    if (text_b) {
        ui_icon_mode(g, "button_b", 11, y_line + 2, GFX_COPY, 0);
    }
    if (text_a) {
        ui_icon_mode(g, "button_a", 107, y_line + 2, GFX_COPY, 0);
    }
    captions(g, 24, 104, y_line + 3, 38, text_b, text_a, now);
}

/* Barra de progresso com contorno. */
void ui_progress(gfx_t *g, int x, int y, int width, int height, int value, int maximum)
{
    gfx_rect(g, x, y, width, height, GFX_WHITE);
    if (maximum <= 0) {
        return;
    }
    if (value < 0) {
        value = 0;
    }
    if (value > maximum) {
        value = maximum;
    }
    int internal = width - 4;
    int filled_width = internal * value / maximum;
    if (height > 4) {
        gfx_rect_filled(g, x + 2, y + 2, filled_width, height - 4, GFX_WHITE);
    } else {
        gfx_line_h(g, x + 1, y + height / 2, (width - 2) * value / maximum, GFX_WHITE);
    }
}

/* Desenha um bitmap da VROM com o modo pedido. */
void ui_icon_mode(gfx_t *g, const char *name, int x, int y, gfx_mode_t mode, unsigned flags)
{
    const gfx_bitmap_t *b = vrom(name);
    if (b) {
        gfx_bitmap(g, b, x, y, mode, flags);
    }
}

/* Desenha um ícone da VROM com fundo transparente. */
bool ui_icon(gfx_t *g, const char *name, int x, int y)
{
    const gfx_bitmap_t *b = vrom(name);
    if (!b) {
        return false;
    }
    gfx_bitmap(g, b, x, y, GFX_OR, 0);
    return true;
}

/* Quebra o texto em linhas (por palavras) e desenha até `max_lines`. */
int ui_paragraph(gfx_t *g, int x, int y, int width, int max_lines, const char *text, bool centered)
{
    int max_chars = (width + 1) / FONT_5X8.advance;
    int lines = 0;
    const char *p = text;
    while (*p && lines < max_lines) {
        while (*p == ' ') {
            p++;
        }
        /* Avança até max_chars, recuando para o último espaço */
        const char *end = p, *line_break = NULL;
        int n = 0;
        while (*end && *end != '\n' && n < max_chars) {
            const char *prev = end;
            utf8_next(&end);
            if (*prev == ' ') {
                line_break = prev;
            }
            n++;
        }
        if (*end && *end != '\n' && *end != ' ' && line_break) {
            end = line_break;
        }
        char buf[64];
        int length = (int)(end - p) < (int)sizeof(buf) - 1 ? (int)(end - p) : (int)sizeof(buf) - 1;
        memcpy(buf, p, (size_t)length);
        buf[length] = '\0';
        if (centered) {
            gfx_text_center(g, x, width, y + lines * FONT_5X8.line_height, buf, GFX_WHITE);
        } else {
            gfx_text(g, x, y + lines * FONT_5X8.line_height, buf, GFX_WHITE);
        }
        lines++;
        p = end;
        if (*p == '\n') {
            p++;
        }
    }
    return lines;
}

/* Mantém a seleção e o topo da lista dentro dos limites. */
void ui_list_adjust(ui_list_t *l, int lines)
{
    if (l->total <= 0) {
        l->sel = l->top = 0;
        return;
    }
    if (l->sel >= l->total) {
        l->sel = l->total - 1;
    }
    if (l->sel < 0) {
        l->sel = 0;
    }
    if (l->sel < l->top) {
        l->top = l->sel;
    }
    if (l->sel >= l->top + lines) {
        l->top = l->sel - lines + 1;
    }
    if (l->top > l->total - lines) {
        l->top = l->total - lines > 0 ? l->total - lines : 0;
    }
}

/* Desenha a lista padrão: ícone, texto, informação à direita e seleção arredondada. */
void ui_list_draw(gfx_t *g, ui_list_t *l, int y0, int lines, ui_item_fn item_fn, void *ctx, uint32_t now)
{
    ui_list_adjust(l, lines);
    for (int i = 0; i < lines && l->top + i < l->total; i++) {
        int position = l->top + i;
        int y = y0 + i * UI_LINE_HEIGHT;
        ui_item_t it = {0};
        item_fn(position, &it, ctx);
        if (it.icon) {
            ui_icon(g, it.icon, 4, y);
        }
        int text_end = 123;
        if (it.info[0]) {
            int wi = gfx_text_width(it.info);
            gfx_text(g, 123 - wi + 1, y + 4, it.info, GFX_WHITE);
            text_end = 123 - wi - 4;
        }
        int x_text = it.icon ? 22 : 6;
        bool sel = position == l->sel;
        ui_marquee_text(g, x_text, y + 4, text_end - x_text, it.text, GFX_WHITE, sel, &l->marquee, now);
        if (sel) {
            gfx_rect_rounded(g, 2, y, 124, UI_SELECTION_HEIGHT, 2, GFX_WHITE);
        }
    }
    ui_scrollbar(g, y0, lines * UI_LINE_HEIGHT, l->total, lines, l->top);
}

/* Move a seleção com Cima/Baixo (com volta). Retorna true se tratou a tecla. */
bool ui_list_key(ui_list_t *l, const key_event_t *ev, int lines)
{
    if (ev->type == KEY_RELEASED || l->total <= 0) {
        return false;
    }
    if (ev->key == KEY_UP) {
        l->sel = (l->sel - 1 + l->total) % l->total;
    } else if (ev->key == KEY_DOWN) {
        l->sel = (l->sel + 1) % l->total;
    } else {
        return false;
    }
    ui_list_adjust(l, lines);
    return true;
}

/* Reticências de espera: aparecem uma a uma, somem e recomeçam. */
const char *ui_dots(uint32_t now_ms)
{
    static const char *const DOTS[] = {"", ".", "..", "..."};
    return DOTS[(now_ms / UI_DOTS_STEP_MS) % 4];
}

/* Texto com reticências animadas; a posição é a do texto completo, para não tremer. */
void ui_text_dots(gfx_t *g, int x, int width, int y, const char *text, uint32_t now_ms, gfx_color_t color)
{
    char full[64], shown[64];
    snprintf(full, sizeof(full), "%s...", text);
    snprintf(shown, sizeof(shown), "%s%s", text, ui_dots(now_ms));
    gfx_text(g, x + (width - gfx_text_width(full)) / 2, y, shown, color);
}
