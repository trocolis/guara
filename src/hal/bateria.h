#ifndef GUARA_BATERIA_H
#define GUARA_BATERIA_H

#include <stdbool.h>

void bateria_iniciar(void);
/* Lê o ADC e atualiza a média. Chamar periodicamente (ex.: a cada segundo). */
void bateria_atualizar(void);
int bateria_mv(void);
int bateria_porcento(void);
bool bateria_carregando(void);  /* CHRG do TP4056 em nível baixo */
bool bateria_presente(void);
bool bateria_critica(void);     /* < 10% (3,45 V) e sem carregar */

#endif
