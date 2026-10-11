#ifndef GUARA_PERSIST_H
#define GUARA_PERSIST_H

#include <stdbool.h>

#include "guara.h"
#include "storage.h"

/* Mapeia a partição 'storage' e abre VROM/catálogo. */
storage_status_t persist_open_storage(void);

/*
 * Os 3 pilares (README > Proteção da Memória Flash):
 * 1. espelho do Save State na RTC RAM (sobrevive ao deep sleep e a resets, sem desgaste);
 * 2. gravação na Flash só em eventos e a cada PERSIST_SAVE_INTERVAL_S (dirty flag);
 * 3. anel de 16 slots na partição 'save'.
 */
#define PERSIST_SAVE_INTERVAL_S (6 * 3600) /* save periódico, se houver alterações */

bool persist_init(void);
/* Carrega o Guará (RTC RAM após deep sleep, senão o anel). */
bool persist_load(guara_t *g);
void persist_mirror(const guara_t *g);
void persist_mark_dirty(void);
bool persist_dirty(void);
bool persist_save(const guara_t *g);
void persist_save_if_dirty(const guara_t *g);
/* Verdadeiro se há alterações e a última gravação tem `interval_s` segundos ou mais. */
bool persist_save_due(uint32_t interval_s);
/* Factory Rebirth: apaga o anel e o espelho. */
void persist_erase(void);
uint32_t persist_counter(void);

#endif
