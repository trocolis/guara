/*
 * Decodificador de BMP 1-bit (imagens do cartão SD) para o formato da gfx.
 */
#include "gfx_bmp.h"

#include <string.h>

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t le32(const uint8_t *p) { return le16(p) | ((uint32_t)le16(p + 2) << 16); }

/* Valida o cabeçalho e devolve as dimensões. */
int gfx_bmp_info(const uint8_t *b, size_t length, int *width, int *height)
{
    if (length < 62 || b[0] != 'B' || b[1] != 'M') {
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
    *width = w;
    *height = h < 0 ? -h : h;
    return 0;
}

/* Converte os pixels para linhas MSB-first com 1 = aceso, respeitando a paleta. */
int gfx_bmp_decode(const uint8_t *b, size_t length, uint8_t *destination, size_t target_size)
{
    int w, h;
    int err = gfx_bmp_info(b, length, &w, &h);
    if (err) {
        return err;
    }
    int32_t h_raw = (int32_t)le32(b + 22);
    uint32_t off = le32(b + 10);
    uint32_t head_bmp = le32(b + 14);
    const uint8_t *palette = b + 14 + head_bmp;
    if (14 + head_bmp + 8 > length) {
        return -5;
    }
    int lit[2];
    for (int i = 0; i < 2; i++) {
        lit[i] = (palette[i * 4] + palette[i * 4 + 1] + palette[i * 4 + 2]) / 3 > 127;
    }
    size_t step_bmp = ((size_t)(w + 31) / 32) * 4;
    size_t step = ((size_t)w + 7) / 8;
    if (off + step_bmp * (size_t)h > length || step * (size_t)h > target_size) {
        return -6;
    }
    memset(destination, 0, step * (size_t)h);
    for (int y = 0; y < h; y++) {
        int sy = h_raw > 0 ? h - 1 - y : y;
        const uint8_t *line = b + off + step_bmp * (size_t)sy;
        for (int x = 0; x < w; x++) {
            int position = (line[x >> 3] >> (7 - (x & 7))) & 1;
            if (lit[position]) {
                destination[step * (size_t)y + (size_t)(x >> 3)] |= (uint8_t)(0x80 >> (x & 7));
            }
        }
    }
    return 0;
}
