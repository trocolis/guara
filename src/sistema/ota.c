#include "ota.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"

static const char *TAG = "ota";
#define BLOCO 4096

void ota_confirmar_boot(void)
{
    esp_ota_img_states_t estado;
    const esp_partition_t *atual = esp_ota_get_running_partition();
    if (esp_ota_get_state_partition(atual, &estado) == ESP_OK && estado == ESP_OTA_IMG_PENDING_VERIFY) {
        esp_ota_mark_app_valid_cancel_rollback();
        ESP_LOGI(TAG, "Firmware novo confirmado");
    }
}

static long tamanho_arquivo(FILE *f)
{
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    return n;
}

bool ota_firmware_do_sd(const char *caminho, ota_progresso_fn progresso, char *erro, int tam_erro)
{
    FILE *f = fopen(caminho, "rb");
    if (!f) {
        snprintf(erro, (size_t)tam_erro, "Arquivo não encontrado");
        return false;
    }
    long total = tamanho_arquivo(f);
    const esp_partition_t *destino = esp_ota_get_next_update_partition(NULL);
    if (!destino || total <= 0 || (size_t)total > destino->size) {
        fclose(f);
        snprintf(erro, (size_t)tam_erro, "Firmware inválido");
        return false;
    }
    esp_ota_handle_t h;
    if (esp_ota_begin(destino, (size_t)total, &h) != ESP_OK) {
        fclose(f);
        snprintf(erro, (size_t)tam_erro, "Falha ao preparar");
        return false;
    }
    uint8_t *buf = malloc(BLOCO);
    long feito = 0;
    bool ok = buf != NULL;
    while (ok && feito < total) {
        size_t n = fread(buf, 1, BLOCO, f);
        if (n == 0 || esp_ota_write(h, buf, n) != ESP_OK) {
            ok = false;
            break;
        }
        feito += (long)n;
        if (progresso) {
            progresso((int)(feito * 100 / total));
        }
    }
    free(buf);
    fclose(f);
    if (!ok || esp_ota_end(h) != ESP_OK || esp_ota_set_boot_partition(destino) != ESP_OK) {
        snprintf(erro, (size_t)tam_erro, "Imagem inválida");
        return false;
    }
    ESP_LOGI(TAG, "Firmware gravado em %s", destino->label);
    return true;
}

bool ota_storage_do_sd(const char *caminho, ota_progresso_fn progresso, char *erro, int tam_erro)
{
    FILE *f = fopen(caminho, "rb");
    if (!f) {
        snprintf(erro, (size_t)tam_erro, "Arquivo não encontrado");
        return false;
    }
    long total = tamanho_arquivo(f);
    const esp_partition_t *p = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, (esp_partition_subtype_t)0x99, "storage");
    uint8_t *buf = malloc(BLOCO);
    if (!p || !buf || total < 64 || (size_t)total > p->size) {
        fclose(f);
        free(buf);
        snprintf(erro, (size_t)tam_erro, "storage.bin inválido");
        return false;
    }
    if (fread(buf, 1, 4, f) != 4 || memcmp(buf, "GSTR", 4) != 0) {
        fclose(f);
        free(buf);
        snprintf(erro, (size_t)tam_erro, "storage.bin inválido");
        return false;
    }
    fseek(f, 0, SEEK_SET);
    size_t apagar = ((size_t)total + BLOCO - 1) / BLOCO * BLOCO;
    bool ok = esp_partition_erase_range(p, 0, apagar) == ESP_OK;
    long feito = 0;
    while (ok && feito < total) {
        size_t n = fread(buf, 1, BLOCO, f);
        if (n == 0 || esp_partition_write(p, (size_t)feito, buf, n) != ESP_OK) {
            ok = false;
            break;
        }
        feito += (long)n;
        if (progresso) {
            progresso((int)(feito * 100 / total));
        }
    }
    free(buf);
    fclose(f);
    if (!ok) {
        snprintf(erro, (size_t)tam_erro, "Falha ao gravar");
    }
    return ok;
}
