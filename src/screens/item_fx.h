/*
 * Efeito de uso de item, comum à tela principal e ao combate: o frasco tomba
 * na direção de quem bebe, partículas voam até ele, o sprite pisca invertido
 * e sobem os textos do que mudou, com as abreviações do CATALOG.md.
 */
#ifndef GUARA_ITEM_FX_H
#define GUARA_ITEM_FX_H

#include <stdbool.h>
#include <stdint.h>

#include "gfx.h"

/* Tempos do efeito (README > Tela principal > Área de itens) */
#define FX_SIP_MS 300 /* bebida e poção sobem e tombam */
#define FX_RISE_PX 8
#define FX_POUR_DEGREES 45
#define FX_FLY_MS 350
#define FX_PARTICLES 8
#define FX_PARTICLE_DELAY_MS 20
#define FX_FLY_TOTAL_MS (FX_FLY_MS + FX_PARTICLES * FX_PARTICLE_DELAY_MS) /* até a última partícula chegar */
#define FX_FLASH_MS 450 /* 3 piscadas invertidas de 75 ms */
#define FX_FLASH_STEP_MS 75
#define FX_TEXT_MS 600
#define FX_TEXT_STEP_MS 200
#define FX_MAX_TEXTS 8
#define FX_TEXT_SIZE 12

/* Valores que um item pode mudar (energia, humor e alimentação em centésimos de %). */
typedef struct {
    int hp, mp, energy, mood, satiety;
    uint32_t states;
} fx_values_t;

/* Ícone girado `degrees` em torno do centro (positivo = anti-horário: o topo vai para a esquerda). */
void fx_icon_rotated(gfx_t *g, const char *name, int x, int y, float degrees);
/* Boca do ícone 16x16 em (x, y) girado `degrees`: o meio da borda de cima, girado. */
void fx_icon_mouth(int x, int y, float degrees, int *mouth_x, int *mouth_y);
/* Partículas de (x0, y0) até (x2, y2) numa curva por cima, `t` ms depois de lançadas. */
void fx_particles(gfx_t *g, int x0, int y0, int x2, int y2, uint32_t t);
/* Inverte em `g` só a silhueta do sprite desenhado sozinho em `sprite` (sem o fundo externo). */
void fx_invert_silhouette(gfx_t *g, const gfx_t *sprite);
/* Verdadeiro na fase acesa das piscadas, `t` ms depois de começarem. */
bool fx_flash_on(uint32_t t);
/* Monta os textos ("HP+10", "EP+12", "-PSN") do que mudou de `before` para `after`. Retorna quantos. */
int fx_build_texts(const fx_values_t *before, const fx_values_t *after, char texts[][FX_TEXT_SIZE], int max);
/*
 * Desenha os textos subindo de `y` (1 px a cada 20 ms, um a cada FX_TEXT_STEP_MS),
 * centralizados em `x`, brancos sobre fundo preto arredondado. Retorna se ainda há texto por vir.
 */
bool fx_texts(gfx_t *g, char texts[][FX_TEXT_SIZE], int n, uint32_t t, int x, int y);
/* Duração dos textos. */
uint32_t fx_texts_ms(int n);

#endif
