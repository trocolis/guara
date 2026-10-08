#include "gfx_bmp.h"

#include <string.h>

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t le32(const uint8_t *p) { return le16(p) | ((uint32_t)le16(p + 2) << 16); }

int gfx_bmp_info(const uint8_t *b, size_t tam, int *largura, int *altura)
{
    if (tam < 62 || b[0] != 'B' || b[1] != 'M') {
        return -1;
    }
    int32_t w = (int32_t)le32(b + 18), h = (int32_t)le32(b + 22);
    if (le16(b + 28) != 1) {
        return -2; /* não é 1 bpp */
    }
    if (le32(b + 30) != 0) {
        return -3; /* comprimido */
    }
    if (w <= 0 || w > 1024 || h == 0 || h > 1024 || h < -1024) {
        return -4;
    }
    *largura = w;
    *altura = h < 0 ? -h : h;
    return 0;
}

int gfx_bmp_decodificar(const uint8_t *b, size_t tam, uint8_t *destino, size_t tam_destino)
{
    int w, h;
    int err = gfx_bmp_info(b, tam, &w, &h);
    if (err) {
        return err;
    }
    int32_t h_bruto = (int32_t)le32(b + 22);
    uint32_t off = le32(b + 10);
    uint32_t cab = le32(b + 14);
    const uint8_t *paleta = b + 14 + cab;
    if (14 + cab + 8 > tam) {
        return -5;
    }
    int aceso[2];
    for (int i = 0; i < 2; i++) {
        aceso[i] = (paleta[i * 4] + paleta[i * 4 + 1] + paleta[i * 4 + 2]) / 3 > 127;
    }
    size_t passo_bmp = ((size_t)(w + 31) / 32) * 4;
    size_t passo = ((size_t)w + 7) / 8;
    if (off + passo_bmp * (size_t)h > tam || passo * (size_t)h > tam_destino) {
        return -6;
    }
    memset(destino, 0, passo * (size_t)h);
    for (int y = 0; y < h; y++) {
        int sy = h_bruto > 0 ? h - 1 - y : y;
        const uint8_t *linha = b + off + passo_bmp * (size_t)sy;
        for (int x = 0; x < w; x++) {
            int indice = (linha[x >> 3] >> (7 - (x & 7))) & 1;
            if (aceso[indice]) {
                destino[passo * (size_t)y + (size_t)(x >> 3)] |= (uint8_t)(0x80 >> (x & 7));
            }
        }
    }
    return 0;
}
