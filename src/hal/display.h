#ifndef GUARA_DISPLAY_H
#define GUARA_DISPLAY_H

#include <stdbool.h>

#include "gfx.h"

/* Driver do OLED SH1106 128x64 (I2C). O envio roda numa task própria. */
bool display_iniciar(void);
/* Copia o framebuffer e agenda o envio das páginas alteradas. */
void display_enviar(const gfx_t *g);
void display_ligar(bool ligado);
void display_contraste(uint8_t valor);
/* Cópia do último quadro enviado (1024 bytes, formato de páginas). */
void display_copiar(uint8_t destino[GFX_ALTURA / 8][GFX_LARGURA]);

#endif
