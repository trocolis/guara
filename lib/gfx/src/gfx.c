#include "gfx.h"

#include <stdlib.h>
#include <string.h>

void gfx_iniciar(gfx_t *g)
{
    gfx_limpar(g);
    gfx_sem_recorte(g);
}

void gfx_limpar(gfx_t *g)
{
    memset(g->buf, 0, sizeof(g->buf));
}

void gfx_recortar(gfx_t *g, int x0, int y0, int x1, int y1)
{
    g->clip_x0 = x0 < 0 ? 0 : x0;
    g->clip_y0 = y0 < 0 ? 0 : y0;
    g->clip_x1 = x1 > GFX_LARGURA ? GFX_LARGURA : x1;
    g->clip_y1 = y1 > GFX_ALTURA ? GFX_ALTURA : y1;
}

void gfx_sem_recorte(gfx_t *g)
{
    gfx_recortar(g, 0, 0, GFX_LARGURA, GFX_ALTURA);
}

void gfx_pixel(gfx_t *g, int x, int y, gfx_cor_t cor)
{
    if (x < g->clip_x0 || x >= g->clip_x1 || y < g->clip_y0 || y >= g->clip_y1) {
        return;
    }
    uint8_t *b = &g->buf[y >> 3][x];
    uint8_t m = (uint8_t)(1u << (y & 7));
    switch (cor) {
    case GFX_BRANCO:
        *b |= m;
        break;
    case GFX_PRETO:
        *b &= (uint8_t)~m;
        break;
    default:
        *b ^= m;
        break;
    }
}

bool gfx_ler_pixel(const gfx_t *g, int x, int y)
{
    if (x < 0 || x >= GFX_LARGURA || y < 0 || y >= GFX_ALTURA) {
        return false;
    }
    return (g->buf[y >> 3][x] >> (y & 7)) & 1;
}

void gfx_linha_h(gfx_t *g, int x, int y, int largura, gfx_cor_t cor)
{
    for (int i = 0; i < largura; i++) {
        gfx_pixel(g, x + i, y, cor);
    }
}

void gfx_linha_v(gfx_t *g, int x, int y, int altura, gfx_cor_t cor)
{
    for (int i = 0; i < altura; i++) {
        gfx_pixel(g, x, y + i, cor);
    }
}

void gfx_linha(gfx_t *g, int x0, int y0, int x1, int y1, gfx_cor_t cor)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int erro = dx + dy;
    for (;;) {
        gfx_pixel(g, x0, y0, cor);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int e2 = 2 * erro;
        if (e2 >= dy) {
            erro += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            erro += dx;
            y0 += sy;
        }
    }
}

void gfx_retangulo(gfx_t *g, int x, int y, int l, int a, gfx_cor_t cor)
{
    if (l <= 0 || a <= 0) {
        return;
    }
    gfx_linha_h(g, x, y, l, cor);
    if (a > 1) {
        gfx_linha_h(g, x, y + a - 1, l, cor);
    }
    if (a > 2) {
        gfx_linha_v(g, x, y + 1, a - 2, cor);
        if (l > 1) {
            gfx_linha_v(g, x + l - 1, y + 1, a - 2, cor);
        }
    }
}

void gfx_retangulo_cheio(gfx_t *g, int x, int y, int l, int a, gfx_cor_t cor)
{
    for (int j = 0; j < a; j++) {
        gfx_linha_h(g, x, y + j, l, cor);
    }
}

/* Recuo horizontal da linha j (0 = borda) num canto chanfrado de raio r. */
static int recuo_canto(int j, int raio)
{
    return j < raio ? raio - j : 0;
}

void gfx_retangulo_arred(gfx_t *g, int x, int y, int l, int a, int raio, gfx_cor_t cor)
{
    if (raio <= 0) {
        gfx_retangulo(g, x, y, l, a, cor);
        return;
    }
    gfx_linha_h(g, x + raio, y, l - 2 * raio, cor);
    gfx_linha_h(g, x + raio, y + a - 1, l - 2 * raio, cor);
    gfx_linha_v(g, x, y + raio, a - 2 * raio, cor);
    gfx_linha_v(g, x + l - 1, y + raio, a - 2 * raio, cor);
    for (int j = 1; j < raio; j++) {
        int r = recuo_canto(j, raio);
        gfx_pixel(g, x + r, y + j, cor);
        gfx_pixel(g, x + l - 1 - r, y + j, cor);
        gfx_pixel(g, x + r, y + a - 1 - j, cor);
        gfx_pixel(g, x + l - 1 - r, y + a - 1 - j, cor);
    }
}

void gfx_retangulo_arred_cheio(gfx_t *g, int x, int y, int l, int a, int raio, gfx_cor_t cor)
{
    for (int j = 0; j < a; j++) {
        int r = 0;
        if (j < raio) {
            r = recuo_canto(j, raio);
        } else if (j >= a - raio) {
            r = recuo_canto(a - 1 - j, raio);
        }
        gfx_linha_h(g, x + r, y + j, l - 2 * r, cor);
    }
}

void gfx_aba(gfx_t *g, int x, int y, int l, int a, int raio, bool cheia, gfx_cor_t cor)
{
    for (int j = 0; j < a; j++) {
        int r = recuo_canto(j, raio);
        if (cheia || j == 0) {
            gfx_linha_h(g, x + r, y + j, l - 2 * r, cor);
        } else {
            gfx_pixel(g, x + r, y + j, cor);
            gfx_pixel(g, x + l - 1 - r, y + j, cor);
        }
    }
}

void gfx_circulo(gfx_t *g, int cx, int cy, int r, gfx_cor_t cor)
{
    int x = r, y = 0, erro = 1 - r;
    while (x >= y) {
        gfx_pixel(g, cx + x, cy + y, cor);
        gfx_pixel(g, cx + y, cy + x, cor);
        gfx_pixel(g, cx - y, cy + x, cor);
        gfx_pixel(g, cx - x, cy + y, cor);
        gfx_pixel(g, cx - x, cy - y, cor);
        gfx_pixel(g, cx - y, cy - x, cor);
        gfx_pixel(g, cx + y, cy - x, cor);
        gfx_pixel(g, cx + x, cy - y, cor);
        y++;
        if (erro < 0) {
            erro += 2 * y + 1;
        } else {
            x--;
            erro += 2 * (y - x) + 1;
        }
    }
}

void gfx_circulo_cheio(gfx_t *g, int cx, int cy, int r, gfx_cor_t cor)
{
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            if (dx * dx + dy * dy <= r * r + r) {
                gfx_pixel(g, cx + dx, cy + dy, cor);
            }
        }
    }
}

void gfx_seta(gfx_t *g, int x, int y, int altura, int direcao, gfx_cor_t cor)
{
    int meio = altura / 2;
    for (int j = 0; j < altura; j++) {
        int l = meio - abs(j - meio) + 1;
        if (direcao > 0) {
            gfx_linha_h(g, x, y + j, l, cor);
        } else {
            gfx_linha_h(g, x + meio + 1 - l, y + j, l, cor);
        }
    }
}

static inline bool bit_bitmap(const gfx_bitmap_t *bmp, int x, int y)
{
    int passo = (bmp->largura + 7) / 8;
    return (bmp->dados[y * passo + (x >> 3)] >> (7 - (x & 7))) & 1;
}

void gfx_bitmap(gfx_t *g, const gfx_bitmap_t *bmp, int x, int y, gfx_modo_t modo, unsigned flags)
{
    if (bmp == NULL || bmp->dados == NULL) {
        return;
    }
    int escala = (flags & GFX_ESCALA2) ? 2 : 1;
    for (int by = 0; by < bmp->altura; by++) {
        for (int bx = 0; bx < bmp->largura; bx++) {
            int sx = (flags & GFX_ESPELHAR) ? bmp->largura - 1 - bx : bx;
            bool aceso = bit_bitmap(bmp, sx, by);
            int cor = -1;
            switch (modo) {
            case GFX_COPIA:
                cor = aceso ? GFX_BRANCO : GFX_PRETO;
                break;
            case GFX_OU:
                cor = aceso ? GFX_BRANCO : -1;
                break;
            case GFX_E:
                cor = aceso ? -1 : GFX_PRETO;
                break;
            case GFX_XOR:
                cor = aceso ? GFX_INVERTER : -1;
                break;
            case GFX_INVERSO:
                cor = aceso ? GFX_PRETO : GFX_BRANCO;
                break;
            }
            if (cor < 0) {
                continue;
            }
            for (int ey = 0; ey < escala; ey++) {
                for (int ex = 0; ex < escala; ex++) {
                    gfx_pixel(g, x + bx * escala + ex, y + by * escala + ey, (gfx_cor_t)cor);
                }
            }
        }
    }
}

int gfx_texto_escala(gfx_t *g, int x, int y, const char *texto, gfx_cor_t cor, int escala)
{
    const fonte_t *f = &FONTE_5X8;
    int x0 = x;
    uint32_t c;
    while ((c = utf8_proximo(&texto)) != 0) {
        const uint8_t *col = fonte_glyph(f, c);
        if (x + f->largura * escala > g->clip_x0 && x < g->clip_x1) {
            for (int cx = 0; cx < f->largura; cx++) {
                for (int cy = 0; cy < f->altura; cy++) {
                    if ((col[cx] >> cy) & 1) {
                        for (int ey = 0; ey < escala; ey++) {
                            for (int ex = 0; ex < escala; ex++) {
                                gfx_pixel(g, x + cx * escala + ex, y + cy * escala + ey, cor);
                            }
                        }
                    }
                }
            }
        }
        x += f->avanco * escala;
    }
    return x == x0 ? 0 : x - x0 - escala;
}

int gfx_texto(gfx_t *g, int x, int y, const char *texto, gfx_cor_t cor)
{
    return gfx_texto_escala(g, x, y, texto, cor, 1);
}

int gfx_largura_texto(const char *texto)
{
    int n = utf8_tamanho(texto);
    return n ? n * FONTE_5X8.avanco - 1 : 0;
}

void gfx_texto_centro(gfx_t *g, int x, int largura, int y, const char *texto, gfx_cor_t cor)
{
    gfx_texto(g, x + (largura - gfx_largura_texto(texto)) / 2, y, texto, cor);
}
