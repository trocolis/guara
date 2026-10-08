#ifndef GUARA_GFX_H
#define GUARA_GFX_H

#include <stdbool.h>
#include <stdint.h>

#include "fonte.h"

#define GFX_LARGURA 128
#define GFX_ALTURA  64

/*
 * Framebuffer 1-bit no formato nativo do SH1106: 8 páginas de 128 colunas,
 * cada byte guarda 8 pixels verticais (bit 0 = linha de cima da página).
 * Todas as operações respeitam o retângulo de recorte (clip).
 */
typedef struct {
    uint8_t buf[GFX_ALTURA / 8][GFX_LARGURA];
    int16_t clip_x0, clip_y0, clip_x1, clip_y1; /* x1/y1 exclusivos */
} gfx_t;

typedef enum {
    GFX_PRETO = 0,
    GFX_BRANCO = 1,
    GFX_INVERTER = 2,
} gfx_cor_t;

/*
 * Bitmap 1-bit em linhas (row-major), MSB primeiro, (largura+7)/8 bytes por
 * linha, bit 1 = pixel aceso (branco). É o formato da VROM.
 */
typedef struct {
    uint16_t largura;
    uint16_t altura;
    const uint8_t *dados;
} gfx_bitmap_t;

typedef enum {
    GFX_COPIA,    /* desenha branco e preto (opaco) */
    GFX_OU,       /* só os pixels brancos acendem (fundo transparente) */
    GFX_E,        /* pixels pretos apagam, brancos são transparentes (máscara) */
    GFX_XOR,      /* pixels brancos invertem o fundo */
    GFX_INVERSO,  /* opaco com cores invertidas */
} gfx_modo_t;

enum {
    GFX_ESPELHAR = 1 << 0,  /* espelha no eixo vertical */
    GFX_ESCALA2 = 1 << 1,   /* amplia 2x (nearest neighbor) */
};

void gfx_iniciar(gfx_t *g);
void gfx_limpar(gfx_t *g);
void gfx_recortar(gfx_t *g, int x0, int y0, int x1, int y1);
void gfx_sem_recorte(gfx_t *g);

void gfx_pixel(gfx_t *g, int x, int y, gfx_cor_t cor);
bool gfx_ler_pixel(const gfx_t *g, int x, int y);
void gfx_linha_h(gfx_t *g, int x, int y, int largura, gfx_cor_t cor);
void gfx_linha_v(gfx_t *g, int x, int y, int altura, gfx_cor_t cor);
void gfx_linha(gfx_t *g, int x0, int y0, int x1, int y1, gfx_cor_t cor);
void gfx_retangulo(gfx_t *g, int x, int y, int l, int a, gfx_cor_t cor);
void gfx_retangulo_cheio(gfx_t *g, int x, int y, int l, int a, gfx_cor_t cor);
void gfx_retangulo_arred(gfx_t *g, int x, int y, int l, int a, int raio, gfx_cor_t cor);
void gfx_retangulo_arred_cheio(gfx_t *g, int x, int y, int l, int a, int raio, gfx_cor_t cor);
/* Retângulo só com os cantos de cima arredondados e sem a base (abas). */
void gfx_aba(gfx_t *g, int x, int y, int l, int a, int raio, bool cheia, gfx_cor_t cor);
void gfx_circulo(gfx_t *g, int cx, int cy, int r, gfx_cor_t cor);
void gfx_circulo_cheio(gfx_t *g, int cx, int cy, int r, gfx_cor_t cor);
/* Triângulo cheio apontando para a esquerda (-1) ou para a direita (+1) da tela. */
void gfx_seta(gfx_t *g, int x, int y, int altura, int direcao, gfx_cor_t cor);

void gfx_bitmap(gfx_t *g, const gfx_bitmap_t *bmp, int x, int y, gfx_modo_t modo, unsigned flags);

/* Texto UTF-8; só os pixels acesos do glyph são desenhados. Retorna a largura. */
int gfx_texto(gfx_t *g, int x, int y, const char *texto, gfx_cor_t cor);
int gfx_texto_escala(gfx_t *g, int x, int y, const char *texto, gfx_cor_t cor, int escala);
int gfx_largura_texto(const char *texto);
/* Desenha centralizado em [x, x+largura). */
void gfx_texto_centro(gfx_t *g, int x, int largura, int y, const char *texto, gfx_cor_t cor);

#endif
