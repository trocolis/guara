/*
 * Tela de espera para operações que seguram o laço principal (conectar ao
 * Wi-Fi, buscar redes, falar com o GitHub, gravar uma atualização): ícone
 * opcional, texto com reticências animadas e barra de progresso opcional.
 * Um timer a redesenha a cada UI_BUSY_FRAME_MS; ela some sozinha quando o
 * laço principal volta a desenhar as telas (ui_draw).
 */
#ifndef GUARA_BUSY_H
#define GUARA_BUSY_H

#include <stdbool.h>

#define UI_BUSY_FRAME_MS 100

/* Mostra a espera com `text` (sem as reticências, que são animadas) e o ícone (ou NULL). */
void ui_busy_show(const char *icon, const char *text);
/* Barra de progresso de 0 a 100 (-1 esconde). Mostra a espera se ainda não estiver na tela. */
void ui_busy_progress(const char *text, int percent);
/* Para o timer (chamado por ui_draw). */
void ui_busy_hide(void);

#endif
