/*
 * Atualização de firmware e storage a partir do cartão SD ou da última
 * release publicada no GitHub.
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
/* Repositório das releases (github.com/<dono>/<repo>), com firmware.bin e storage.bin anexados */
#define OTA_GITHUB_REPO "trocolis/guara"

/* Busca a tag da última release no GitHub (ex.: "v0.12.0"). Precisa da rede conectada. */
bool ota_github_latest(char *tag, int tag_size, char *error, int error_size);
/* Verdadeiro se a tag "v#.#.#" for mais nova que a versão do firmware atual. */
bool ota_is_newer(const char *tag);
/* Baixa o firmware (e o storage, se a release tiver) da tag e o marca para o próximo boot. */
bool ota_github_install(const char *tag, ota_progress_fn progress, char *error, int error_size);
/* Confirma o firmware atual (cancela o rollback automático). */
void ota_confirm_boot(void);

#endif
