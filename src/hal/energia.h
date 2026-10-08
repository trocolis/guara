#ifndef GUARA_ENERGIA_H
#define GUARA_ENERGIA_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    ACORDOU_FRIO,     /* ligado ou reset */
    ACORDOU_BOTAO,
    ACORDOU_TIMER,
} motivo_acordar_t;

motivo_acordar_t energia_motivo(void);
/*
 * Entra em deep sleep. Acorda pelos botões Cima/Baixo (GPIO32/33) e, se
 * `segundos` > 0, pelo timer. Não retorna.
 */
void energia_dormir(uint32_t segundos);

#endif
