/*
 * Efeito de uso de item (tela principal e combate): rotação do ícone,
 * partículas, piscada invertida da silhueta e textos do efeito.
 */
#include "item_fx.h"

#include <math.h>
#include <stdio.h>

#include "storage.h"
#include "types.h"

/* Gira o ícone por vizinho mais próximo: cada pixel de destino amostra o de origem (opaco, como GFX_COPY). */
void fx_icon_rotated(gfx_t *g, const char *name, int x, int y, float degrees)
{
    gfx_bitmap_t bmp;
    if (!vrom_lookup(name, &bmp)) {
        return;
    }
    float a = degrees * (float)M_PI / 180.0f, c = cosf(a), s = sinf(a);
    float cx = bmp.width / 2.0f, cy = bmp.height / 2.0f;
    int stride = (bmp.width + 7) / 8;
    int reach = (bmp.width > bmp.height ? bmp.width : bmp.height) * 3 / 4 + 1; /* meia diagonal */
    for (int dy = -reach; dy <= reach; dy++) {
        for (int dx = -reach; dx <= reach; dx++) {
            /* rotação inversa: destino -> origem */
            float px = dx + 0.5f, py = dy + 0.5f;
            int sx = (int)floorf(px * c - py * s + cx);
            int sy = (int)floorf(px * s + py * c + cy);
            if (sx < 0 || sy < 0 || sx >= bmp.width || sy >= bmp.height) {
                continue;
            }
            bool lit = bmp.data[sy * stride + sx / 8] & (0x80 >> (sx % 8));
            gfx_pixel(g, x + (int)cx + dx, y + (int)cy + dy, lit ? GFX_WHITE : GFX_BLACK);
        }
    }
}

/* Meio da borda de cima do ícone 16x16 (8 px acima do centro), girado. */
void fx_icon_mouth(int x, int y, float degrees, int *mouth_x, int *mouth_y)
{
    float a = degrees * (float)M_PI / 180.0f;
    *mouth_x = x + 8 - (int)lroundf(8 * sinf(a));
    *mouth_y = y + 8 - (int)lroundf(8 * cosf(a));
}

/* Cruzes 3x3 em inversão numa curva quadrática por cima; cada partícula sai um pouco depois e com seu desvio. */
void fx_particles(gfx_t *g, int x0, int y0, int x2, int y2, uint32_t t)
{
    int top = (y0 < y2 ? y0 : y2) - 20;
    if (top < 4) {
        top = 4;
    }
    for (int i = 0; i < FX_PARTICLES; i++) {
        int32_t pt = (int32_t)t - i * FX_PARTICLE_DELAY_MS;
        if (pt < 0 || pt >= FX_FLY_MS) {
            continue;
        }
        int dx = (i % 4 - 2) * 4, dy = (i / 4) * 6 - 3;
        int x1 = (x0 + x2) / 2 + dx, y1 = top + dy;
        int u = pt * 64 / FX_FLY_MS, v = 64 - u; /* parâmetro em 1/64 */
        int x = (v * v * x0 + 2 * u * v * x1 + u * u * (x2 + dx)) / 4096;
        int y = (v * v * y0 + 2 * u * v * y1 + u * u * (y2 + dy)) / 4096;
        gfx_pixel(g, x, y, GFX_INVERT);
        gfx_pixel(g, x - 1, y, GFX_INVERT);
        gfx_pixel(g, x + 1, y, GFX_INVERT);
        gfx_pixel(g, x, y - 1, GFX_INVERT);
        gfx_pixel(g, x, y + 1, GFX_INVERT);
    }
}

/*
 * Marca o fundo externo por inundação a partir da borda do retângulo que
 * envolve o sprite (com 1 px de folga) e inverte em `g` o resto: a silhueta.
 */
void fx_invert_silhouette(gfx_t *g, const gfx_t *sprite)
{
    int x0 = GFX_WIDTH, y0 = GFX_HEIGHT, x1 = -1, y1 = -1;
    for (int y = 0; y < GFX_HEIGHT; y++) {
        for (int x = 0; x < GFX_WIDTH; x++) {
            if (gfx_read_pixel(sprite, x, y)) {
                x0 = x < x0 ? x : x0;
                x1 = x > x1 ? x : x1;
                y0 = y < y0 ? y : y0;
                y1 = y > y1 ? y : y1;
            }
        }
    }
    if (x1 < 0) {
        return;
    }
    x0--, y0--, x1++, y1++; /* folga: a borda é sempre fundo externo */
    static gfx_t outside;
    gfx_init(&outside);
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            if (x == x0 || x == x1 || y == y0 || y == y1) {
                gfx_pixel(&outside, x, y, GFX_WHITE);
            }
        }
    }
    /* Varreduras de ida e volta até estabilizar (a silhueta é simples, bastam poucas) */
    int w = x1 - x0 + 1, h = y1 - y0 + 1;
    for (bool changed = true; changed;) {
        changed = false;
        for (int pass = 0; pass < 2; pass++) {
            for (int i = 0; i < w * h; i++) {
                int k = pass == 0 ? i : w * h - 1 - i;
                int x = x0 + k % w, y = y0 + k / w;
                if (gfx_read_pixel(&outside, x, y) || gfx_read_pixel(sprite, x, y)) {
                    continue;
                }
                if (gfx_read_pixel(&outside, x - 1, y) || gfx_read_pixel(&outside, x + 1, y) ||
                    gfx_read_pixel(&outside, x, y - 1) || gfx_read_pixel(&outside, x, y + 1)) {
                    gfx_pixel(&outside, x, y, GFX_WHITE);
                    changed = true;
                }
            }
        }
    }
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            if (!gfx_read_pixel(&outside, x, y)) {
                gfx_pixel(g, x, y, GFX_INVERT);
            }
        }
    }
}

/* Fase acesa das 3 piscadas. */
bool fx_flash_on(uint32_t t)
{
    return t < FX_FLASH_MS && (t / FX_FLASH_STEP_MS) % 2 == 0;
}

/* Adiciona "HP+10" se mudou e houver espaço. */
static void add_text(char texts[][FX_TEXT_SIZE], int *n, int max, const char *code, int delta)
{
    if (delta != 0 && *n < max) {
        snprintf(texts[(*n)++], FX_TEXT_SIZE, "%s%+d", code, delta);
    }
}

/*
 * Abreviações do CATALOG.md: HP, MP, EP, HU e FP em pontos (energia, humor e
 * alimentação em %), e "-PSN" etc. para estados que saíram.
 */
int fx_build_texts(const fx_values_t *before, const fx_values_t *after, char texts[][FX_TEXT_SIZE], int max)
{
    static const struct {
        state_t state;
        const char *code;
    } CURES[] = {{ST_POISONED, "PSN"}, {ST_BURNED, "BRN"},  {ST_PARALYZED, "PRL"}, {ST_FROZEN, "FRZ"},
                 {ST_CONFUSED, "CNF"}, {ST_CURSED, "CRS"}, {ST_TIRED, "FND"},     {ST_HUNGRY, "HGR"},
                 {ST_SLEEPING, "SLP"}};
    int n = 0;
    add_text(texts, &n, max, "HP", after->hp - before->hp);
    add_text(texts, &n, max, "MP", after->mp - before->mp);
    add_text(texts, &n, max, "EP", (after->energy - before->energy) / 100);
    add_text(texts, &n, max, "HU", (after->mood - before->mood) / 100);
    add_text(texts, &n, max, "FP", (after->satiety - before->satiety) / 100);
    uint32_t cured = before->states & ~after->states;
    for (size_t i = 0; i < sizeof(CURES) / sizeof(CURES[0]) && n < max; i++) {
        if (cured & STATE_BIT(CURES[i].state)) {
            snprintf(texts[n++], FX_TEXT_SIZE, "-%s", CURES[i].code);
        }
    }
    return n;
}

/* Textos subindo 1 px a cada 20 ms: com um a cada 200 ms, ficam 10 px (uma linha) um do outro. */
bool fx_texts(gfx_t *g, char texts[][FX_TEXT_SIZE], int n, uint32_t t, int x, int y)
{
    bool running = false;
    for (int i = 0; i < n; i++) {
        int32_t st = (int32_t)t - i * FX_TEXT_STEP_MS;
        if (st < 0 || st >= FX_TEXT_MS) {
            running |= st < 0;
            continue;
        }
        running = true;
        int ty = y - st / 20;
        int w = gfx_text_width(texts[i]);
        gfx_rect_rounded_filled(g, x - w / 2 - 2, ty - 1, w + 4, 10, 2, GFX_BLACK);
        gfx_text(g, x - w / 2, ty, texts[i], GFX_WHITE);
    }
    return running;
}

/* Duração dos textos: o último começa (n-1) passos depois e sobe por FX_TEXT_MS. */
uint32_t fx_texts_ms(int n)
{
    return n > 0 ? (uint32_t)(n - 1) * FX_TEXT_STEP_MS + FX_TEXT_MS : 0;
}
