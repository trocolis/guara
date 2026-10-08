/*
 * Persistência em 3 pilares (README > Proteção da Memória Flash): espelho na
 * RTC RAM, gravação por eventos e anel de 16 slots na partição 'save'.
 * Também mapeia a partição 'storage' (VROM + catálogo).
 */
#include "persistence.h"

#include <string.h>

#include "esp_attr.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "save.h"

static const char *TAG = "persist";

#define SUBTYPE_STORAGE ((esp_partition_subtype_t)0x99)
#define SUBTYPE_SAVE ((esp_partition_subtype_t)0x9A)

static const esp_partition_t *part_save;
static ring_t ring;
static bool dirty;

static RTC_DATA_ATTR uint8_t rtc_mirror[SAVE_SIZE];

/* Mapeia a partição storage na memória e abre VROM e catálogo. */
storage_status_t persist_open_storage(void)
{
    const esp_partition_t *p = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, SUBTYPE_STORAGE, "storage");
    if (!p) {
        return STORAGE_ERROR_BOUNDS;
    }
    static const void *map;
    static esp_partition_mmap_handle_t handle;
    if (!map && esp_partition_mmap(p, 0, p->size, ESP_PARTITION_MMAP_DATA, &map, &handle) != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao mapear a partição storage");
        return STORAGE_ERROR_BOUNDS;
    }
    storage_status_t st = storage_open(map, p->size);
    if (st == STORAGE_OK) {
        ESP_LOGI(TAG, "Storage: %d bitmaps, %d itens", vrom_count(), catalog_record_count(CAT_ITEMS));
    } else {
        ESP_LOGE(TAG, "Storage inválido (%d). Grave com: pio run -e esp32 -t upload_storage", st);
    }
    return st;
}

/* Lê da partição save (operação do anel). */
static bool flash_read(void *ctx, uint32_t off, void *buf, size_t n)
{
    return esp_partition_read(ctx, off, buf, n) == ESP_OK;
}

/* Escreve na partição save (operação do anel). */
static bool flash_write(void *ctx, uint32_t off, const void *buf, size_t n)
{
    return esp_partition_write(ctx, off, buf, n) == ESP_OK;
}

/* Apaga um setor de 4 KB da partição save. */
static bool flash_erase_sector(void *ctx, uint32_t off)
{
    return esp_partition_erase_range(ctx, off, RING_SECTOR) == ESP_OK;
}

/* Localiza a partição save e prepara o anel. */
bool persist_init(void)
{
    part_save = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, SUBTYPE_SAVE, "save");
    if (!part_save) {
        ESP_LOGE(TAG, "Partição save não encontrada");
        return false;
    }
    flash_ops_t ops = {(void *)part_save, flash_read, flash_write, flash_erase_sector};
    ring_init(&ring, &ops);
    return true;
}

/* Carrega o Guará: espelho da RTC RAM (após deep sleep) ou o save mais recente da Flash. */
bool persist_load(guara_t *g)
{
    uint32_t counter;
    guara_t flash;
    bool has_flash = part_save && ring_load(&ring, &flash);
    if (save_deserialize(rtc_mirror, g, &counter)) {
        ESP_LOGI(TAG, "Guará restaurado da RTC RAM");
        return true;
    }
    if (has_flash) {
        *g = flash;
        ESP_LOGI(TAG, "Guará restaurado da Flash (slot %d, gravação %lu)", ring.last_slot, (unsigned long)ring.counter);
        return true;
    }
    return false;
}

/* Copia o estado para a RTC RAM (sem desgaste de Flash). */
void persist_mirror(const guara_t *g)
{
    save_serialize(g, ring.counter, rtc_mirror);
}

/* Marca que há alterações a gravar. */
void persist_mark_dirty(void)
{
    dirty = true;
}

/* Verdadeiro se há alterações não gravadas. */
bool persist_dirty(void)
{
    return dirty;
}

/* Grava no anel e atualiza o espelho. */
bool persist_save(const guara_t *g)
{
    persist_mirror(g);
    if (!part_save || !ring_save(&ring, g)) {
        ESP_LOGE(TAG, "Falha ao gravar o save");
        return false;
    }
    dirty = false;
    ESP_LOGI(TAG, "Save gravado (slot %d, gravação %lu)", ring.last_slot, (unsigned long)ring.counter);
    return true;
}

/* Grava só se houver alterações pendentes. */
void persist_save_if_dirty(const guara_t *g)
{
    if (dirty) {
        persist_save(g);
    }
}

/* Apaga o anel e invalida o espelho (Factory Rebirth). */
void persist_erase(void)
{
    memset(rtc_mirror, 0, sizeof(rtc_mirror));
    if (part_save) {
        ring_erase(&ring);
    }
    dirty = false;
}

/* Quantidade de gravações feitas no anel. */
uint32_t persist_counter(void)
{
    return ring.counter;
}
