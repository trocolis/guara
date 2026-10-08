#ifndef GUARA_PERSIST_H
#define GUARA_PERSIST_H

#include <stdbool.h>

#include "guara.h"
#include "storage.h"

/* Mapeia a partição 'storage' e abre VROM/catálogo. */
storage_status_t persist_abrir_storage(void);

/*
 * Os 3 pilares (README > Proteção da Memória Flash):
 * 1. espelho do Save State na RTC RAM (sobrevive ao deep sleep, sem desgaste);
 * 2. gravação na Flash só em eventos (dirty flag);
 * 3. anel de 16 slots na partição 'save'.
 */
bool persist_iniciar(void);
/* Carrega o Guará (RTC RAM após deep sleep, senão o anel). */
bool persist_carregar(guara_t *g);
void persist_espelhar(const guara_t *g);
void persist_marcar_sujo(void);
bool persist_sujo(void);
bool persist_salvar(const guara_t *g);
void persist_salvar_se_sujo(const guara_t *g);
/* Factory Rebirth: apaga o anel e o espelho. */
void persist_apagar(void);
uint32_t persist_contador(void);

#endif
