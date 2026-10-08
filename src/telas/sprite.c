#include "sprite.h"

#include <stdio.h>

#include "storage.h"

static const char *const ESTAGIOS[QTD_ESTAGIOS] = {"baby", "yong_a", "yong_b", "yong_c", "adult_a"};

int sprite_quadro(uint32_t agora)
{
    return (int)((agora / 333) % 3) + 1; /* 1, 2, 3 */
}

int sprite_guara(gfx_t *g, uint8_t cor, uint8_t cabeca, uint8_t estagio, const char *olho, int x_centro, int y_base,
                 int quadro, unsigned flags)
{
    const char *c = cor == COR_BRANCO ? "white" : "black";
    char nome[40];
    gfx_bitmap_t corpo, corpo1, cab, olhos;

    if (estagio >= QTD_ESTAGIOS) {
        estagio = 0;
    }
    snprintf(nome, sizeof(nome), "sprite_%s_%s_%d", ESTAGIOS[estagio], c, quadro >= 2 ? 2 : 1); /* quadros 2 e 3 usam o bitmap _2 */
    if (!vrom_buscar(nome, &corpo)) {
        return 0;
    }
    snprintf(nome, sizeof(nome), "sprite_%s_%s_1", ESTAGIOS[estagio], c);
    if (!vrom_buscar(nome, &corpo1)) {
        corpo1 = corpo;
    }
    const int e = (flags & GFX_ESCALA2) ? 2 : 1;
    int y_corpo = y_base - corpo.altura * e + 1;
    gfx_bitmap(g, &corpo, x_centro - corpo.largura * e / 2, y_corpo, GFX_COPIA, flags);

    int topo = y_corpo;
    snprintf(nome, sizeof(nome), "sprite_head_%s_variant_%d", c, cabeca >= 1 && cabeca <= 9 ? cabeca : 1);
    if (vrom_buscar(nome, &cab)) {
        topo = y_corpo - cab.altura * e;
        gfx_bitmap(g, &cab, x_centro - cab.largura * e / 2, topo, GFX_COPIA, flags);
    }

    /* Olhos fixos em relação ao quadro 1 (1 px abaixo do topo): o corpo "respira" e os olhos ficam no lugar */
    snprintf(nome, sizeof(nome), "sprite_eye_%s_%s", c, olho ? olho : OLHO_NORMAL);
    if (vrom_buscar(nome, &olhos)) {
        int y_olhos = y_base - corpo1.altura * e + 1 + e;
        if (quadro == 3) {
            y_olhos -= e; /* no quadro 3 os olhos sobem 1 px */
        }
        gfx_bitmap(g, &olhos, x_centro - olhos.largura * e / 2, y_olhos, GFX_COPIA, flags);
    }
    return y_base - topo + 1;
}
