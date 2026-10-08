/*
 * Bateria LiPo: tensão, carga (%), detecção de carregamento e de nível crítico.
 */
#ifndef GUARA_BATTERY_H
#define GUARA_BATTERY_H

#include <stdbool.h>

void battery_init(void);
/* Lê o ADC e atualiza a média. Chamar periodicamente (ex.: a cada segundo). */
void battery_update(void);
int battery_mv(void);
int battery_percent(void);
bool battery_charging(void);  /* CHRG do TP4056 em nível baixo */
bool battery_present(void);
bool battery_critical(void);     /* < 10% (3,45 V) e sem carregar */

#endif
