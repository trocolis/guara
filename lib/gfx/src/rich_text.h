/*
 * Texto com estilos (leitor da Biblioteca): **negrito**, *itálico*,
 * __sublinhado__ e ==marcado== (fundo branco, texto invertido). "\" antes de
 * um caractere o mostra literalmente (\* vira *).
 */
#ifndef GUARA_RICH_TEXT_H
#define GUARA_RICH_TEXT_H

#include <stdint.h>

#include "gfx.h"

enum {
    RICH_BOLD = 1 << 0,      /* glyph desenhado duas vezes (1 px à direita), avanço de 7 px */
    RICH_ITALIC = 1 << 1,    /* metade de cima do glyph 1 px à direita */
    RICH_UNDERLINE = 1 << 2, /* traço na linha logo abaixo do glyph (y + 8) */
    RICH_MARK = 1 << 3,      /* fundo branco de y - 1 a y + 7, texto preto */
};

/*
 * Lê o próximo caractere visível de [*p, end), consumindo as marcações no
 * caminho (cada uma alterna o estilo em *style). Retorna o codepoint, ou 0 no
 * fim. Depois de retornar, *style é o estilo desse caractere.
 */
uint32_t rich_next(const char **p, const char *end, uint8_t *style);

/* Avanço horizontal de um caractere com o estilo (6 px, ou 7 px em negrito). */
int rich_advance(uint8_t style);

/*
 * Desenha [text, end) em (x, y) começando com o estilo `style`, em branco (o
 * marcado sai em preto sobre branco). Retorna a largura percorrida.
 */
int rich_draw(gfx_t *g, int x, int y, const char *text, const char *end, uint8_t style);

#endif
