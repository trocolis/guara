#ifndef GUARA_GFX_H
#define GUARA_GFX_H

#include <stdbool.h>
#include <stdint.h>

#include "font.h"

#define GFX_WIDTH 128
#define GFX_HEIGHT  64

/*
 * Framebuffer 1-bit no formato nativo do SH1106: 8 páginas de 128 colunas,
 * cada byte guarda 8 pixels verticais (bit 0 = linha de cima da página).
 * Todas as operações respeitam o retângulo de recorte (clip).
 */
typedef struct {
    uint8_t buf[GFX_HEIGHT / 8][GFX_WIDTH];
    int16_t clip_x0, clip_y0, clip_x1, clip_y1; /* x1/y1 exclusivos */
} gfx_t;

typedef enum {
    GFX_BLACK = 0,
    GFX_WHITE = 1,
    GFX_INVERT = 2,
} gfx_color_t;

/*
 * Bitmap 1-bit em linhas (row-major), MSB primeiro, (largura+7)/8 bytes por
 * linha, bit 1 = pixel aceso (branco). É o formato da VROM.
 */
typedef struct {
    uint16_t width;
    uint16_t height;
    const uint8_t *data;
} gfx_bitmap_t;

typedef enum {
    GFX_COPY,    /* desenha branco e preto (opaco) */
    GFX_OR,      /* só os pixels brancos acendem (fundo transparente) */
    GFX_E,       /* pixels pretos apagam, brancos são transparentes (máscara) */
    GFX_XOR,     /* pixels brancos invertem o fundo */
    GFX_INVERSE, /* opaco com cores invertidas */
} gfx_mode_t;

enum {
    GFX_MIRROR = 1 << 0,  /* espelha no eixo vertical */
    GFX_SCALE2X = 1 << 1, /* amplia 2x (nearest neighbor) */
};

void gfx_init(gfx_t *g);
void gfx_clear(gfx_t *g);
void gfx_clip(gfx_t *g, int x0, int y0, int x1, int y1);
void gfx_reset_clip(gfx_t *g);

void gfx_pixel(gfx_t *g, int x, int y, gfx_color_t color);
bool gfx_read_pixel(const gfx_t *g, int x, int y);
void gfx_line_h(gfx_t *g, int x, int y, int width, gfx_color_t color);
void gfx_line_v(gfx_t *g, int x, int y, int height, gfx_color_t color);
void gfx_line(gfx_t *g, int x0, int y0, int x1, int y1, gfx_color_t color);
void gfx_rect(gfx_t *g, int x, int y, int l, int a, gfx_color_t color);
void gfx_rect_filled(gfx_t *g, int x, int y, int l, int a, gfx_color_t color);
void gfx_rect_rounded(gfx_t *g, int x, int y, int l, int a, int radius, gfx_color_t color);
void gfx_rect_rounded_filled(gfx_t *g, int x, int y, int l, int a, int radius, gfx_color_t color);
/* Retângulo só com os cantos de cima arredondados e sem a base (abas). */
void gfx_tab(gfx_t *g, int x, int y, int l, int a, int radius, bool filled, gfx_color_t color);
void gfx_circle(gfx_t *g, int cx, int cy, int r, gfx_color_t color);
void gfx_circle_filled(gfx_t *g, int cx, int cy, int r, gfx_color_t color);
/* Triângulo cheio apontando para a esquerda (-1) ou para a direita (+1) da tela. */
void gfx_arrow(gfx_t *g, int x, int y, int height, int direction, gfx_color_t color);

void gfx_bitmap(gfx_t *g, const gfx_bitmap_t *bmp, int x, int y, gfx_mode_t mode, unsigned flags);

/* Texto UTF-8; só os pixels acesos do glyph são desenhados. Retorna a largura. */
int gfx_text(gfx_t *g, int x, int y, const char *text, gfx_color_t color);
int gfx_text_scaled(gfx_t *g, int x, int y, const char *text, gfx_color_t color, int scale);
int gfx_text_width(const char *text);
/* Desenha centralizado em [x, x+largura). */
void gfx_text_center(gfx_t *g, int x, int width, int y, const char *text, gfx_color_t color);

#endif
