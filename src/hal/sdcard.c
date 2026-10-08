#include "sdcard.h"

#include <errno.h>
#include <string.h>
#include <sys/stat.h>

#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "pinos.h"
#include "sdmmc_cmd.h"

static const char *TAG = "sd";
static sdmmc_card_t *cartao;
static bool barramento_ok;

bool sd_montar(void)
{
    if (cartao) {
        return true;
    }
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI3_HOST;
    if (!barramento_ok) {
        spi_bus_config_t bus = {
            .mosi_io_num = PINO_SD_MOSI,
            .miso_io_num = PINO_SD_MISO,
            .sclk_io_num = PINO_SD_SCK,
            .quadwp_io_num = -1,
            .quadhd_io_num = -1,
            .max_transfer_sz = 4096,
        };
        if (spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) {
            ESP_LOGE(TAG, "Falha ao iniciar o barramento SPI");
            return false;
        }
        barramento_ok = true;
    }
    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.gpio_cs = PINO_SD_CS;
    slot.host_id = SPI3_HOST;
    esp_vfs_fat_sdmmc_mount_config_t cfg = {
        .format_if_mount_failed = false,
        .max_files = 6,
        .allocation_unit_size = 16 * 1024,
    };
    /* Cabos longos (protótipo com fios/IDE) não aguentam os 20 MHz padrão */
    static const int frequencias_khz[] = {10000, 4000};
    esp_err_t err = ESP_FAIL;
    for (int i = 0; i < 2 && err != ESP_OK; i++) {
        host.max_freq_khz = frequencias_khz[i];
        err = esp_vfs_fat_sdspi_mount(SD_RAIZ, &host, &slot, &cfg, &cartao);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Cartão não montado: %s", esp_err_to_name(err));
        cartao = NULL;
        return false;
    }
    ESP_LOGI(TAG, "Cartão montado a %d kHz: %s", host.max_freq_khz, cartao->cid.name);
    return true;
}

void sd_desmontar(void)
{
    if (cartao) {
        esp_vfs_fat_sdcard_unmount(SD_RAIZ, cartao);
        cartao = NULL;
    }
}

bool sd_montado(void)
{
    return cartao != NULL;
}

bool sd_espaco(uint64_t *total, uint64_t *livre)
{
    if (!cartao) {
        return false;
    }
    return esp_vfs_fat_info(SD_RAIZ, total, livre) == ESP_OK;
}

bool sd_garantir_pasta(const char *caminho)
{
    char buf[128];
    strncpy(buf, caminho, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    for (char *p = buf + strlen(SD_RAIZ) + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(buf, 0775);
            *p = '/';
        }
    }
    return mkdir(buf, 0775) == 0 || errno == EEXIST;
}
