/*
 * Gerenciamento de energia: motivo do despertar e deep sleep.
 */
#ifndef GUARA_ENERGY_H
#define GUARA_ENERGY_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    WOKE_COLD,     /* ligado ou reset */
    WOKE_BUTTON,
    WOKE_TIMER,
} wake_reason_t;

wake_reason_t power_wake_reason(void);
/*
 * Entra em deep sleep. Acorda pelos botões Cima/Baixo (GPIO32/33) e, se
 * `segundos` > 0, pelo timer. Não retorna.
 */
void power_deep_sleep(uint32_t seconds);

#endif
