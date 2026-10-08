#ifndef GUARA_SDCARD_H
#define GUARA_SDCARD_H

#include <stdbool.h>
#include <stdint.h>

#define SD_RAIZ "/sdcard"

bool sd_montar(void);
void sd_desmontar(void);
bool sd_montado(void);
/* Capacidade e espaço livre em bytes. */
bool sd_espaco(uint64_t *total, uint64_t *livre);
/* Cria a pasta (e as intermediárias) se não existir. */
bool sd_garantir_pasta(const char *caminho);

#endif
