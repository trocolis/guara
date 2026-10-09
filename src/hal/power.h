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

/* Configura a frequência dinâmica e o light sleep automático (começa no modo Ativo). */
void power_init(void);
/* Modo Ocioso: brilho mínimo, CPU entre 80 e 160 MHz e light sleep automático. */
void power_set_idle(bool enabled);
bool power_is_idle(void);

wake_reason_t power_wake_reason(void);
/*
 * Entra em deep sleep. Acorda por qualquer um dos quatro botões (Cima/Baixo
 * pelo RTC, A/B pelo ULP) e, se `seconds` > 0, pelo timer. Não retorna.
 */
void power_deep_sleep(uint32_t seconds);

#endif
