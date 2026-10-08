#ifndef GUARA_SPRITE_H
#define GUARA_SPRITE_H

#include <stdint.h>

#include "gfx.h"
#include "guara.h"

/* Nomes dos olhos na VROM (sprite_eye_<cor>_<nome>) */
#define EYE_NORMAL "standard"
#define EYE_ANGER "anger"
#define EYE_BORED "bored"
#define EYE_CLOSED "closed"
#define EYE_DAZED "dazed"
#define EYE_FAINTED "fainted"
#define EYE_HAPPY "happy"
#define EYE_SCARED "scared"
#define EYE_WINK "wink"

/*
 * Quadro da animação (1, 2 ou 3) a 3 FPS. Os quadros 2 e 3 usam o bitmap _2
 * do corpo; no quadro 3 os olhos sobem 1 px (README > Sprites).
 */
int sprite_frame(uint32_t now_ms);

/*
 * Compõe cabeça + corpo + olhos com a base em `y_base` (última linha) e
 * centralizado em `x_center`. Com GFX_SCALE2X em `flags`, todas as partes são
 * ampliadas 2x mantendo o encaixe. Retorna a altura total desenhada.
 */
int sprite_guara(gfx_t *g, uint8_t color, uint8_t head, uint8_t stage, const char *eye, int x_center, int y_base,
                 int frame, unsigned flags);

#endif
