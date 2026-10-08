/*
 * Biblioteca gráfica 1-bit: framebuffer no formato de páginas do SH1106,
 * primitivas, bitmaps com modos de desenho e texto UTF-8.
 */
#include "gfx.h"

#include <stdlib.h>
#include <string.h>

/* Limpa o framebuffer e remove o recorte. */
void gfx_init(gfx_t *g)
{
    gfx_clear(g);
    gfx_reset_clip(g);
}

/* Apaga todos os pixels. */
void gfx_clear(gfx_t *g)
{
    memset(g->buf, 0, sizeof(g->buf));
}

/* Define o retângulo de recorte (limitado à tela). */
void gfx_clip(gfx_t *g, int x0, int y0, int x1, int y1)
{
    g->clip_x0 = x0 < 0 ? 0 : x0;
    g->clip_y0 = y0 < 0 ? 0 : y0;
    g->clip_x1 = x1 > GFX_WIDTH ? GFX_WIDTH : x1;
    g->clip_y1 = y1 > GFX_HEIGHT ? GFX_HEIGHT : y1;
}

/* Volta o recorte para a tela inteira. */
void gfx_reset_clip(gfx_t *g)
{
    gfx_clip(g, 0, 0, GFX_WIDTH, GFX_HEIGHT);
}

/* Acende, apaga ou inverte um pixel (respeitando o recorte). */
void gfx_pixel(gfx_t *g, int x, int y, gfx_color_t color)
{
    if (x < g->clip_x0 || x >= g->clip_x1 || y < g->clip_y0 || y >= g->clip_y1) {
        return;
    }
    uint8_t *b = &g->buf[y >> 3][x];
    uint8_t m = (uint8_t)(1u << (y & 7));
    switch (color) {
    case GFX_WHITE:
        *b |= m;
        break;
    case GFX_BLACK:
        *b &= (uint8_t)~m;
        break;
    default:
        *b ^= m;
        break;
    }
}

/* Verdadeiro se o pixel está aceso. */
bool gfx_read_pixel(const gfx_t *g, int x, int y)
{
    if (x < 0 || x >= GFX_WIDTH || y < 0 || y >= GFX_HEIGHT) {
        return false;
    }
    return (g->buf[y >> 3][x] >> (y & 7)) & 1;
}

/* Linha horizontal a partir de (x, y). */
void gfx_line_h(gfx_t *g, int x, int y, int width, gfx_color_t color)
{
    for (int i = 0; i < width; i++) {
        gfx_pixel(g, x + i, y, color);
    }
}

/* Linha vertical a partir de (x, y). */
void gfx_line_v(gfx_t *g, int x, int y, int height, gfx_color_t color)
{
    for (int i = 0; i < height; i++) {
        gfx_pixel(g, x, y + i, color);
    }
}

/* Linha entre dois pontos (algoritmo de Bresenham). */
void gfx_line(gfx_t *g, int x0, int y0, int x1, int y1, gfx_color_t color)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    for (;;) {
        gfx_pixel(g, x0, y0, color);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int e2 = 2 * error;
        if (e2 >= dy) {
            error += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

/* Contorno de retângulo. */
void gfx_rect(gfx_t *g, int x, int y, int l, int a, gfx_color_t color)
{
    if (l <= 0 || a <= 0) {
        return;
    }
    gfx_line_h(g, x, y, l, color);
    if (a > 1) {
        gfx_line_h(g, x, y + a - 1, l, color);
    }
    if (a > 2) {
        gfx_line_v(g, x, y + 1, a - 2, color);
        if (l > 1) {
            gfx_line_v(g, x + l - 1, y + 1, a - 2, color);
        }
    }
}

/* Retângulo preenchido. */
void gfx_rect_filled(gfx_t *g, int x, int y, int l, int a, gfx_color_t color)
{
    for (int j = 0; j < a; j++) {
        gfx_line_h(g, x, y + j, l, color);
    }
}

/* Recuo horizontal da linha j (0 = borda) num canto chanfrado de raio r. */
static int inset_corner(int j, int radius)
{
    return j < radius ? radius - j : 0;
}

/* Contorno de retângulo com cantos arredondados (chanfrados). */
void gfx_rect_rounded(gfx_t *g, int x, int y, int l, int a, int radius, gfx_color_t color)
{
    if (radius <= 0) {
        gfx_rect(g, x, y, l, a, color);
        return;
    }
    gfx_line_h(g, x + radius, y, l - 2 * radius, color);
    gfx_line_h(g, x + radius, y + a - 1, l - 2 * radius, color);
    gfx_line_v(g, x, y + radius, a - 2 * radius, color);
    gfx_line_v(g, x + l - 1, y + radius, a - 2 * radius, color);
    for (int j = 1; j < radius; j++) {
        int r = inset_corner(j, radius);
        gfx_pixel(g, x + r, y + j, color);
        gfx_pixel(g, x + l - 1 - r, y + j, color);
        gfx_pixel(g, x + r, y + a - 1 - j, color);
        gfx_pixel(g, x + l - 1 - r, y + a - 1 - j, color);
    }
}

/* Retângulo preenchido com cantos arredondados. */
void gfx_rect_rounded_filled(gfx_t *g, int x, int y, int l, int a, int radius, gfx_color_t color)
{
    for (int j = 0; j < a; j++) {
        int r = 0;
        if (j < radius) {
            r = inset_corner(j, radius);
        } else if (j >= a - radius) {
            r = inset_corner(a - 1 - j, radius);
        }
        gfx_line_h(g, x + r, y + j, l - 2 * r, color);
    }
}

/* Aba: cantos de cima arredondados e sem a linha de baixo. */
void gfx_tab(gfx_t *g, int x, int y, int l, int a, int radius, bool filled, gfx_color_t color)
{
    for (int j = 0; j < a; j++) {
        int r = inset_corner(j, radius);
        if (filled || j == 0) {
            gfx_line_h(g, x + r, y + j, l - 2 * r, color);
        } else {
            gfx_pixel(g, x + r, y + j, color);
            gfx_pixel(g, x + l - 1 - r, y + j, color);
        }
    }
}

/* Contorno de círculo (algoritmo do ponto médio). */
void gfx_circle(gfx_t *g, int cx, int cy, int r, gfx_color_t color)
{
    int x = r, y = 0, error = 1 - r;
    while (x >= y) {
        gfx_pixel(g, cx + x, cy + y, color);
        gfx_pixel(g, cx + y, cy + x, color);
        gfx_pixel(g, cx - y, cy + x, color);
        gfx_pixel(g, cx - x, cy + y, color);
        gfx_pixel(g, cx - x, cy - y, color);
        gfx_pixel(g, cx - y, cy - x, color);
        gfx_pixel(g, cx + y, cy - x, color);
        gfx_pixel(g, cx + x, cy - y, color);
        y++;
        if (error < 0) {
            error += 2 * y + 1;
        } else {
            x--;
            error += 2 * (y - x) + 1;
        }
    }
}

/* Círculo preenchido. */
void gfx_circle_filled(gfx_t *g, int cx, int cy, int r, gfx_color_t color)
{
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            if (dx * dx + dy * dy <= r * r + r) {
                gfx_pixel(g, cx + dx, cy + dy, color);
            }
        }
    }
}

/* Triângulo cheio apontando para a esquerda (-1) ou para a direita (+1) da tela. */
void gfx_arrow(gfx_t *g, int x, int y, int height, int direction, gfx_color_t color)
{
    int mid = height / 2;
    for (int j = 0; j < height; j++) {
        int l = mid - abs(j - mid) + 1;
        if (direction > 0) {
            gfx_line_h(g, x, y + j, l, color);
        } else {
            gfx_line_h(g, x + mid + 1 - l, y + j, l, color);
        }
    }
}

/* Lê o pixel (x, y) de um bitmap row-major. */
static inline bool bit_bitmap(const gfx_bitmap_t *bmp, int x, int y)
{
    int step = (bmp->width + 7) / 8;
    return (bmp->data[y * step + (x >> 3)] >> (7 - (x & 7))) & 1;
}

/* Desenha um bitmap com o modo e as opções (espelhar, ampliar 2x) pedidos. */
void gfx_bitmap(gfx_t *g, const gfx_bitmap_t *bmp, int x, int y, gfx_mode_t mode, unsigned flags)
{
    if (bmp == NULL || bmp->data == NULL) {
        return;
    }
    int scale = (flags & GFX_SCALE2X) ? 2 : 1;
    for (int by = 0; by < bmp->height; by++) {
        for (int bx = 0; bx < bmp->width; bx++) {
            int sx = (flags & GFX_MIRROR) ? bmp->width - 1 - bx : bx;
            bool lit = bit_bitmap(bmp, sx, by);
            int color = -1;
            switch (mode) {
            case GFX_COPY:
                color = lit ? GFX_WHITE : GFX_BLACK;
                break;
            case GFX_OR:
                color = lit ? GFX_WHITE : -1;
                break;
            case GFX_MASK:
                color = lit ? -1 : GFX_BLACK;
                break;
            case GFX_XOR:
                color = lit ? GFX_INVERT : -1;
                break;
            case GFX_INVERSE:
                color = lit ? GFX_BLACK : GFX_WHITE;
                break;
            }
            if (color < 0) {
                continue;
            }
            for (int ey = 0; ey < scale; ey++) {
                for (int ex = 0; ex < scale; ex++) {
                    gfx_pixel(g, x + bx * scale + ex, y + by * scale + ey, (gfx_color_t)color);
                }
            }
        }
    }
}

/* Desenha texto UTF-8 ampliado; só os pixels acesos do glyph são desenhados. */
int gfx_text_scaled(gfx_t *g, int x, int y, const char *text, gfx_color_t color, int scale)
{
    const font_t *f = &FONT_5X8;
    int x0 = x;
    uint32_t c;
    while ((c = utf8_next(&text)) != 0) {
        const uint8_t *col = font_glyph(f, c);
        if (x + f->width * scale > g->clip_x0 && x < g->clip_x1) {
            for (int cx = 0; cx < f->width; cx++) {
                for (int cy = 0; cy < f->height; cy++) {
                    if ((col[cx] >> cy) & 1) {
                        for (int ey = 0; ey < scale; ey++) {
                            for (int ex = 0; ex < scale; ex++) {
                                gfx_pixel(g, x + cx * scale + ex, y + cy * scale + ey, color);
                            }
                        }
                    }
                }
            }
        }
        x += f->advance * scale;
    }
    return x == x0 ? 0 : x - x0 - scale;
}

/* Desenha texto UTF-8 em tamanho normal. Retorna a largura em pixels. */
int gfx_text(gfx_t *g, int x, int y, const char *text, gfx_color_t color)
{
    return gfx_text_scaled(g, x, y, text, color, 1);
}

/* Largura do texto em pixels (6 px por caractere, sem o espaço final). */
int gfx_text_width(const char *text)
{
    int n = utf8_size(text);
    return n ? n * FONT_5X8.advance - 1 : 0;
}

/* Desenha o texto centralizado em [x, x + width). */
void gfx_text_center(gfx_t *g, int x, int width, int y, const char *text, gfx_color_t color)
{
    gfx_text(g, x + (width - gfx_text_width(text)) / 2, y, text, color);
}
