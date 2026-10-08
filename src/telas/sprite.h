#ifndef GUARA_SPRITE_H
#define GUARA_SPRITE_H

#include <stdint.h>

#include "gfx.h"
#include "guara.h"

/* Nomes dos olhos na VROM (sprite_eye_<cor>_<nome>) */
#define OLHO_NORMAL "standard"
#define OLHO_RAIVA "anger"
#define OLHO_CHATEADO "bored"
#define OLHO_FECHADO "closed"
#define OLHO_ATORDOADO "dazed"
#define OLHO_DESMAIADO "fainted"
#define OLHO_FELIZ "happy"
#define OLHO_ASSUSTADO "scared"
#define OLHO_PISCADELA "wink"

/*
 * Quadro da animação (1, 2 ou 3) a 3 FPS. Os quadros 2 e 3 usam o bitmap _2
 * do corpo; no quadro 3 os olhos sobem 1 px (README > Sprites).
 */
int sprite_quadro(uint32_t agora_ms);

/*
 * Compõe cabeça + corpo + olhos com a base em `y_base` (última linha) e
 * centralizado em `x_centro`. Com GFX_ESCALA2 em `flags`, todas as partes são
 * ampliadas 2x mantendo o encaixe. Retorna a altura total desenhada.
 */
int sprite_guara(gfx_t *g, uint8_t cor, uint8_t cabeca, uint8_t estagio, const char *olho, int x_centro, int y_base,
                 int quadro, unsigned flags);

#endif
