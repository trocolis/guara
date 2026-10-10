/*
 * Texto com estilos: leitura das marcações (**, *, __, ==, \) e desenho
 * glyph a glyph com negrito, itálico, sublinhado e marcação.
 */
#include "rich_text.h"

#include <stdbool.h>

#include "font.h"

/* Se [p, end) começa com a marcação de dois caracteres `c c`. */
static bool pair(const char *p, const char *end, char c)
{
    return p + 1 < end && p[0] == c && p[1] == c;
}

/* Lê o próximo caractere visível, alternando o estilo nas marcações. */
uint32_t rich_next(const char **p, const char *end, uint8_t *style)
{
    while (*p < end) {
        const char *q = *p;
        if (*q == '\\' && q + 1 < end) {
            *p = q + 1; /* escape: o próximo caractere é literal */
            break;
        }
        if (pair(q, end, '*')) {
            *style ^= RICH_BOLD;
            *p = q + 2;
        } else if (pair(q, end, '_')) {
            *style ^= RICH_UNDERLINE;
            *p = q + 2;
        } else if (pair(q, end, '=')) {
            *style ^= RICH_MARK;
            *p = q + 2;
        } else if (*q == '*') {
            *style ^= RICH_ITALIC;
            *p = q + 1;
        } else {
            break;
        }
    }
    if (*p >= end) {
        return 0;
    }
    return utf8_next(p);
}

/* 6 px por caractere; o negrito ocupa 1 px a mais para as letras não colarem. */
int rich_advance(uint8_t style)
{
    return FONT_5X8.advance + ((style & RICH_BOLD) ? 1 : 0);
}

/* Desenha um glyph com negrito e itálico (o fundo e o traço vêm de quem chama). */
static void draw_glyph(gfx_t *g, int x, int y, uint32_t code, uint8_t style, gfx_color_t color)
{
    const font_t *f = &FONT_5X8;
    const uint8_t *col = font_glyph(f, code);
    int copies = (style & RICH_BOLD) ? 2 : 1;
    for (int cx = 0; cx < f->width; cx++) {
        for (int cy = 0; cy < f->height; cy++) {
            if (!((col[cx] >> cy) & 1)) {
                continue;
            }
            /* Itálico: as 4 linhas de cima deslocadas 1 px para a direita */
            int dx = (style & RICH_ITALIC) && cy < f->height / 2 ? 1 : 0;
            for (int k = 0; k < copies; k++) {
                gfx_pixel(g, x + cx + dx + k, y + cy, color);
            }
        }
    }
}

/* Desenha o trecho com os estilos, caractere a caractere. */
int rich_draw(gfx_t *g, int x, int y, const char *text, const char *end, uint8_t style)
{
    int x0 = x;
    uint32_t c;
    while ((c = rich_next(&text, end, &style)) != 0) {
        int adv = rich_advance(style);
        gfx_color_t color = GFX_WHITE;
        if (style & RICH_MARK) {
            /* 1 px de folga à esquerda e o espaço entre letras: trechos seguidos se emendam */
            gfx_rect_filled(g, x - 1, y - 1, adv + 1, 9, GFX_WHITE);
            color = GFX_BLACK;
        }
        draw_glyph(g, x, y, c, style, color);
        if (style & RICH_UNDERLINE) {
            gfx_line_h(g, x, y + 8, adv, GFX_WHITE); /* abaixo do fundo do marcado: sempre branco */
        }
        x += adv;
    }
    return x - x0;
}
