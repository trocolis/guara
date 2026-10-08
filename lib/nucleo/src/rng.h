#ifndef GUARA_RNG_H
#define GUARA_RNG_H

#include <stdint.h>

/* Gerador xorshift32: determinístico nos testes, semeado pelo hardware no aparelho. */
typedef struct {
    uint32_t estado;
} rng_t;

static inline void rng_semear(rng_t *r, uint32_t semente)
{
    r->estado = semente ? semente : 0x6A09E667u;
}

static inline uint32_t rng_proximo(rng_t *r)
{
    uint32_t x = r->estado;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return r->estado = x;
}

/* Inteiro uniforme em [min, max]. */
static inline int rng_faixa(rng_t *r, int min, int max)
{
    return min + (int)(rng_proximo(r) % (uint32_t)(max - min + 1));
}

/* Verdadeiro com probabilidade `porcento` (0..100). */
static inline int rng_chance(rng_t *r, int porcento)
{
    return rng_faixa(r, 0, 99) < porcento;
}

#endif
