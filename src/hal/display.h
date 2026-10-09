#ifndef GUARA_DISPLAY_H
#define GUARA_DISPLAY_H

#include <stdbool.h>

#include "gfx.h"

/* Driver do OLED SH1106 128x64 (I2C). O envio roda numa task própria. */
bool display_init(void);
/* Copia o framebuffer e agenda o envio das páginas alteradas. */
void display_send(const gfx_t *g);
void display_set_power(bool enabled);
/* Desliga o painel de forma segura antes do deep sleep (não volta a enviar quadros). */
void display_shutdown(void);
void display_contrast(uint8_t value);
/* Cópia do último quadro enviado (1024 bytes, formato de páginas). */
void display_copy(uint8_t destination[GFX_HEIGHT / 8][GFX_WIDTH]);

#endif
