/*
 * Composição do sprite do Guará: cabeça + corpo animado + olhos, em 1x ou 2x.
 */
#include "sprite.h"

#include <stdio.h>

#include "storage.h"

static const char *const STAGES[COUNT_STAGES] = {"baby", "yong_a", "yong_b", "yong_c", "adult_a"};

/* Quadro da animação (1, 2 ou 3) a 3 FPS. */
int sprite_frame(uint32_t now)
{
    return (int)((now / 333) % 3) + 1; /* 1, 2, 3 */
}

int sprite_guara(gfx_t *g, uint8_t color, uint8_t head, uint8_t stage, const char *eye, int x_center, int y_base,
                 int frame, unsigned flags)
{
    const char *c = color == COLOR_WHITE ? "white" : "black";
    char name[40];
    gfx_bitmap_t body, body1, head_bmp, eyes;

    if (stage >= COUNT_STAGES) {
        stage = 0;
    }
    snprintf(name, sizeof(name), "sprite_%s_%s_%d", STAGES[stage], c, frame >= 2 ? 2 : 1); /* quadros 2 e 3 usam o bitmap _2 */
    if (!vrom_lookup(name, &body)) {
        return 0;
    }
    snprintf(name, sizeof(name), "sprite_%s_%s_1", STAGES[stage], c);
    if (!vrom_lookup(name, &body1)) {
        body1 = body;
    }
    const int e = (flags & GFX_SCALE2X) ? 2 : 1;
    int y_body = y_base - body.height * e + 1;
    gfx_bitmap(g, &body, x_center - body.width * e / 2, y_body, GFX_COPY, flags);

    int top = y_body;
    snprintf(name, sizeof(name), "sprite_head_%s_variant_%d", c, head >= 1 && head <= 9 ? head : 1);
    if (vrom_lookup(name, &head_bmp)) {
        top = y_body - head_bmp.height * e;
        gfx_bitmap(g, &head_bmp, x_center - head_bmp.width * e / 2, top, GFX_COPY, flags);
    }

    /* Olhos fixos em relação ao quadro 1 (1 px abaixo do topo): o corpo "respira" e os olhos ficam no lugar */
    snprintf(name, sizeof(name), "sprite_eye_%s_%s", c, eye ? eye : EYE_NORMAL);
    if (vrom_lookup(name, &eyes)) {
        int y_eyes = y_base - body1.height * e + 1 + e;
        if (frame == 3) {
            y_eyes -= e; /* no quadro 3 os olhos sobem 1 px */
        }
        gfx_bitmap(g, &eyes, x_center - eyes.width * e / 2, y_eyes, GFX_COPY, flags);
    }
    return y_base - top + 1;
}
