#include "persist.h"

#include <string.h>

#include "esp_attr.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "save.h"

static const char *TAG = "persist";

#define SUBTIPO_STORAGE ((esp_partition_subtype_t)0x99)
#define SUBTIPO_SAVE ((esp_partition_subtype_t)0x9A)

static const esp_partition_t *part_save;
static anel_t anel;
static bool sujo;

static RTC_DATA_ATTR uint8_t espelho[SAVE_TAMANHO];

storage_status_t persist_abrir_storage(void)
{
    const esp_partition_t *p = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, SUBTIPO_STORAGE, "storage");
    if (!p) {
        return STORAGE_ERRO_LIMITES;
    }
    static const void *mapa;
    static esp_partition_mmap_handle_t handle;
    if (!mapa && esp_partition_mmap(p, 0, p->size, ESP_PARTITION_MMAP_DATA, &mapa, &handle) != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao mapear a partição storage");
        return STORAGE_ERRO_LIMITES;
    }
    storage_status_t st = storage_abrir(mapa, p->size);
    if (st == STORAGE_OK) {
        ESP_LOGI(TAG, "Storage: %d bitmaps, %d itens", vrom_quantidade(), catalogo_quantidade(CAT_ITENS));
    } else {
        ESP_LOGE(TAG, "Storage inválido (%d). Grave com: pio run -e esp32 -t upload_storage", st);
    }
    return st;
}

static bool f_ler(void *ctx, uint32_t off, void *buf, size_t n)
{
    return esp_partition_read(ctx, off, buf, n) == ESP_OK;
}

static bool f_escrever(void *ctx, uint32_t off, const void *buf, size_t n)
{
    return esp_partition_write(ctx, off, buf, n) == ESP_OK;
}

static bool f_apagar(void *ctx, uint32_t off)
{
    return esp_partition_erase_range(ctx, off, ANEL_SETOR) == ESP_OK;
}

bool persist_iniciar(void)
{
    part_save = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, SUBTIPO_SAVE, "save");
    if (!part_save) {
        ESP_LOGE(TAG, "Partição save não encontrada");
        return false;
    }
    flash_ops_t ops = {(void *)part_save, f_ler, f_escrever, f_apagar};
    anel_iniciar(&anel, &ops);
    return true;
}

bool persist_carregar(guara_t *g)
{
    uint32_t contador;
    guara_t flash;
    bool tem_flash = part_save && anel_carregar(&anel, &flash);
    if (save_desserializar(espelho, g, &contador)) {
        ESP_LOGI(TAG, "Guará restaurado da RTC RAM");
        return true;
    }
    if (tem_flash) {
        *g = flash;
        ESP_LOGI(TAG, "Guará restaurado da Flash (slot %d, gravação %lu)", anel.ultimo_slot, (unsigned long)anel.contador);
        return true;
    }
    return false;
}

void persist_espelhar(const guara_t *g)
{
    save_serializar(g, anel.contador, espelho);
}

void persist_marcar_sujo(void)
{
    sujo = true;
}

bool persist_sujo(void)
{
    return sujo;
}

bool persist_salvar(const guara_t *g)
{
    persist_espelhar(g);
    if (!part_save || !anel_salvar(&anel, g)) {
        ESP_LOGE(TAG, "Falha ao gravar o save");
        return false;
    }
    sujo = false;
    ESP_LOGI(TAG, "Save gravado (slot %d, gravação %lu)", anel.ultimo_slot, (unsigned long)anel.contador);
    return true;
}

void persist_salvar_se_sujo(const guara_t *g)
{
    if (sujo) {
        persist_salvar(g);
    }
}

void persist_apagar(void)
{
    memset(espelho, 0, sizeof(espelho));
    if (part_save) {
        anel_apagar(&anel);
    }
    sujo = false;
}

uint32_t persist_contador(void)
{
    return anel.contador;
}
