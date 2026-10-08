/*
 * Atualização pelo cartão SD: firmware (partição OTA livre) e storage.
 */
#include "ota.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"

static const char *TAG = "ota";
#define BLOCK 4096

/* Confirma o firmware novo após um boot bem-sucedido (cancela o rollback). */
void ota_confirm_boot(void)
{
    esp_ota_img_states_t state;
    const esp_partition_t *current = esp_ota_get_running_partition();
    if (esp_ota_get_state_partition(current, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
        esp_ota_mark_app_valid_cancel_rollback();
        ESP_LOGI(TAG, "Firmware novo confirmado");
    }
}

/* Tamanho do arquivo aberto. */
static long file_size(FILE *f)
{
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    return n;
}

/* Grava o firmware do SD na partição OTA livre e a marca para o próximo boot. */
bool ota_firmware_from_sd(const char *path, ota_progress_fn progress, char *error, int error_size)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        snprintf(error, (size_t)error_size, "Arquivo não encontrado");
        return false;
    }
    long total = file_size(f);
    const esp_partition_t *destination = esp_ota_get_next_update_partition(NULL);
    if (!destination || total <= 0 || (size_t)total > destination->size) {
        fclose(f);
        snprintf(error, (size_t)error_size, "Firmware inválido");
        return false;
    }
    esp_ota_handle_t h;
    if (esp_ota_begin(destination, (size_t)total, &h) != ESP_OK) {
        fclose(f);
        snprintf(error, (size_t)error_size, "Falha ao preparar");
        return false;
    }
    uint8_t *buf = malloc(BLOCK);
    long written = 0;
    bool ok = buf != NULL;
    while (ok && written < total) {
        size_t n = fread(buf, 1, BLOCK, f);
        if (n == 0 || esp_ota_write(h, buf, n) != ESP_OK) {
            ok = false;
            break;
        }
        written += (long)n;
        if (progress) {
            progress((int)(written * 100 / total));
        }
    }
    free(buf);
    fclose(f);
    if (!ok || esp_ota_end(h) != ESP_OK || esp_ota_set_boot_partition(destination) != ESP_OK) {
        snprintf(error, (size_t)error_size, "Imagem inválida");
        return false;
    }
    ESP_LOGI(TAG, "Firmware gravado em %s", destination->label);
    return true;
}

/* Regrava a partição storage a partir de um storage.bin válido. */
bool ota_storage_from_sd(const char *path, ota_progress_fn progress, char *error, int error_size)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        snprintf(error, (size_t)error_size, "Arquivo não encontrado");
        return false;
    }
    long total = file_size(f);
    const esp_partition_t *p = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, (esp_partition_subtype_t)0x99, "storage");
    uint8_t *buf = malloc(BLOCK);
    if (!p || !buf || total < 64 || (size_t)total > p->size) {
        fclose(f);
        free(buf);
        snprintf(error, (size_t)error_size, "storage.bin inválido");
        return false;
    }
    if (fread(buf, 1, 4, f) != 4 || memcmp(buf, "GSTR", 4) != 0) {
        fclose(f);
        free(buf);
        snprintf(error, (size_t)error_size, "storage.bin inválido");
        return false;
    }
    fseek(f, 0, SEEK_SET);
    size_t erase = ((size_t)total + BLOCK - 1) / BLOCK * BLOCK;
    bool ok = esp_partition_erase_range(p, 0, erase) == ESP_OK;
    long written = 0;
    while (ok && written < total) {
        size_t n = fread(buf, 1, BLOCK, f);
        if (n == 0 || esp_partition_write(p, (size_t)written, buf, n) != ESP_OK) {
            ok = false;
            break;
        }
        written += (long)n;
        if (progress) {
            progress((int)(written * 100 / total));
        }
    }
    free(buf);
    fclose(f);
    if (!ok) {
        snprintf(error, (size_t)error_size, "Falha ao gravar");
    }
    return ok;
}
