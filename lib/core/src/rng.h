#ifndef GUARA_RNG_H
#define GUARA_RNG_H

#include <stdint.h>

/* Gerador xorshift32: determinístico nos testes, semeado pelo hardware no aparelho. */
typedef struct {
    uint32_t state;
} rng_t;

/* Define a semente (0 vira uma constante, pois o xorshift não sai do zero). */
static inline void rng_seed(rng_t *r, uint32_t seed)
{
    r->state = seed ? seed : 0x6A09E667u;
}

/* Próximo número pseudoaleatório de 32 bits. */
static inline uint32_t rng_next(rng_t *r)
{
    uint32_t x = r->state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return r->state = x;
}

/* Inteiro uniforme em [min, max]. */
static inline int rng_range(rng_t *r, int min, int max)
{
    return min + (int)(rng_next(r) % (uint32_t)(max - min + 1));
}

/* Verdadeiro com probabilidade `porcento` (0..100). */
static inline int rng_chance(rng_t *r, int percent)
{
    return rng_range(r, 0, 99) < percent;
}

#endif
