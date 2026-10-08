/*
 * Atualização de firmware e storage a partir do cartão SD.
 */
#ifndef GUARA_OTA_H
#define GUARA_OTA_H

#include <stdbool.h>

#define OTA_FIRMWARE_SD "/sdcard/update/firmware.bin"
#define OTA_STORAGE_SD "/sdcard/update/storage.bin"

typedef void (*ota_progress_fn)(int percent);

/* Grava o firmware do SD na partição OTA livre e a marca para o próximo boot. */
bool ota_firmware_from_sd(const char *path, ota_progress_fn progress, char *error, int error_size);
/* Regrava a partição storage a partir de um storage.bin no SD. */
bool ota_storage_from_sd(const char *path, ota_progress_fn progress, char *error, int error_size);
/* Confirma o firmware atual (cancela o rollback automático). */
void ota_confirm_boot(void);

#endif
