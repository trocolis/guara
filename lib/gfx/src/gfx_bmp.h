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
int gfx_bmp_info(const uint8_t *arquivo, size_t tamanho, int *largura, int *altura);
int gfx_bmp_decodificar(const uint8_t *arquivo, size_t tamanho, uint8_t *destino, size_t tam_destino);

#endif
