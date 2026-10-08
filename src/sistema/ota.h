#ifndef GUARA_OTA_H
#define GUARA_OTA_H

#include <stdbool.h>

#define OTA_FIRMWARE_SD "/sdcard/update/firmware.bin"
#define OTA_STORAGE_SD "/sdcard/update/storage.bin"

typedef void (*ota_progresso_fn)(int porcento);

/* Grava o firmware do SD na partição OTA livre e a marca para o próximo boot. */
bool ota_firmware_do_sd(const char *caminho, ota_progresso_fn progresso, char *erro, int tam_erro);
/* Regrava a partição storage a partir de um storage.bin no SD. */
bool ota_storage_do_sd(const char *caminho, ota_progresso_fn progresso, char *erro, int tam_erro);
/* Confirma o firmware atual (cancela o rollback automático). */
void ota_confirmar_boot(void);

#endif
