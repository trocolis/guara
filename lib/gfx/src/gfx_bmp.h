#ifndef GUARA_GFX_BMP_H
#define GUARA_GFX_BMP_H

#include <stddef.h>
#include <stdint.h>

#include "gfx.h"

/*
 * Decodifica um BMP de 1 bpp (do cartão SD) para o formato gfx_bitmap_t
 * (linhas, MSB primeiro, 1 = aceso). `destino` deve ter (largura+7)/8*altura
 * bytes; chame primeiro com destino NULL para obter as dimensões.
 * Retorna 0 em caso de sucesso ou um código de erro negativo.
 */
int gfx_bmp_info(const uint8_t *file, size_t size, int *width, int *height);
int gfx_bmp_decode(const uint8_t *file, size_t size, uint8_t *destination, size_t target_size);

#endif
