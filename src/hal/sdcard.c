/*
 * Cartão MicroSD por SPI (VSPI) montado em /sdcard com FATFS.
 */
#include "sdcard.h"

#include <errno.h>
#include <string.h>
#include <sys/stat.h>

#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "pins.h"
#include "sdmmc_cmd.h"

static const char *TAG = "sd";
static sdmmc_card_t *card;
static bool spi_bus_ready;

/* Monta o cartão, tentando 10 MHz e depois 4 MHz (fios longos no protótipo). */
bool sd_mount(void)
{
    if (card) {
        return true;
    }
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI3_HOST;
    if (!spi_bus_ready) {
        spi_bus_config_t bus = {
            .mosi_io_num = PIN_SD_MOSI,
            .miso_io_num = PIN_SD_MISO,
            .sclk_io_num = PIN_SD_SCK,
            .quadwp_io_num = -1,
            .quadhd_io_num = -1,
            .max_transfer_sz = 4096,
        };
        if (spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) {
            ESP_LOGE(TAG, "Falha ao iniciar o barramento SPI");
            return false;
        }
        spi_bus_ready = true;
    }
    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.gpio_cs = PIN_SD_CS;
    slot.host_id = SPI3_HOST;
    esp_vfs_fat_sdmmc_mount_config_t cfg = {
        .format_if_mount_failed = false,
        .max_files = 6,
        .allocation_unit_size = 16 * 1024,
    };
    /* Cabos longos (protótipo com fios/IDE) não aguentam os 20 MHz padrão */
    static const int frequencies_khz[] = {10000, 4000};
    esp_err_t err = ESP_FAIL;
    for (int i = 0; i < 2 && err != ESP_OK; i++) {
        host.max_freq_khz = frequencies_khz[i];
        err = esp_vfs_fat_sdspi_mount(SD_ROOT, &host, &slot, &cfg, &card);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Cartão não montado: %s", esp_err_to_name(err));
        card = NULL;
        return false;
    }
    ESP_LOGI(TAG, "Cartão montado a %d kHz: %s", host.max_freq_khz, card->cid.name);
    return true;
}

/* Desmonta o cartão (antes do deep sleep). */
void sd_unmount(void)
{
    if (card) {
        esp_vfs_fat_sdcard_unmount(SD_ROOT, card);
        card = NULL;
    }
}

/* Verdadeiro se o cartão está montado. */
bool sd_mounted(void)
{
    return card != NULL;
}

/* Capacidade e espaço livre em bytes. */
bool sd_get_space(uint64_t *total, uint64_t *free_bytes)
{
    if (!card) {
        return false;
    }
    return esp_vfs_fat_info(SD_ROOT, total, free_bytes) == ESP_OK;
}

/* Cria a pasta e as intermediárias, se não existirem. */
bool sd_ensure_folder(const char *path)
{
    char buf[128];
    strncpy(buf, path, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    for (char *p = buf + strlen(SD_ROOT) + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(buf, 0775);
            *p = '/';
        }
    }
    return mkdir(buf, 0775) == 0 || errno == EEXIST;
}
