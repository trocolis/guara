/*
 * Driver do OLED SH1106 128x64 por I2C. Uma task envia só as páginas que
 * mudaram desde o último quadro, sem travar a interface.
 */
#include "display.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "i2c.h"
#include "pins.h"

static const char *TAG = "display";

/* Defina como 1 se a imagem aparecer de cabeça para baixo. */
#ifndef DISPLAY_ROTATE_180
#define DISPLAY_ROTATE_180 0
#endif

/* O SH1106 tem RAM de 132 colunas; a área visível começa na coluna 2. */
#define OFFSET_COLUMN 2

static i2c_master_dev_handle_t dev;
static SemaphoreHandle_t lock;
static TaskHandle_t job;
static uint8_t pending[GFX_HEIGHT / 8][GFX_WIDTH];
static uint8_t sent[GFX_HEIGHT / 8][GFX_WIDTH];
static bool force = true;

/* Envia uma sequência de comandos (byte de controle 0x00). */
static esp_err_t commands(const uint8_t *cmds, size_t n)
{
    uint8_t buf[16];
    buf[0] = 0x00; /* Co=0, D/C=0: sequência de comandos */
    memcpy(buf + 1, cmds, n);
    return i2c_master_transmit(dev, buf, n + 1, 100);
}

/* Envia uma página (8 linhas) do framebuffer. */
static esp_err_t send_page(int page, const uint8_t *data)
{
    const uint8_t address[] = {
        (uint8_t)(0xB0 | page),
        (uint8_t)(0x00 | (OFFSET_COLUMN & 0x0F)),
        (uint8_t)(0x10 | (OFFSET_COLUMN >> 4)),
    };
    esp_err_t err = commands(address, sizeof(address));
    if (err != ESP_OK) {
        return err;
    }
    uint8_t buf[GFX_WIDTH + 1];
    buf[0] = 0x40; /* dados */
    memcpy(buf + 1, data, GFX_WIDTH);
    return i2c_master_transmit(dev, buf, sizeof(buf), 100);
}

/* Task de envio: copia o quadro pendente e transmite as páginas alteradas. */
static void job_display(void *arg)
{
    (void)arg;
    uint8_t local[GFX_HEIGHT / 8][GFX_WIDTH];
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        xSemaphoreTake(lock, portMAX_DELAY);
        memcpy(local, pending, sizeof(local));
        bool all = force;
        force = false;
        xSemaphoreGive(lock);
        for (int p = 0; p < GFX_HEIGHT / 8; p++) {
            if (all || memcmp(local[p], sent[p], GFX_WIDTH) != 0) {
                if (send_page(p, local[p]) == ESP_OK) {
                    memcpy(sent[p], local[p], GFX_WIDTH);
                } else {
                    force = true;
                }
            }
        }
    }
}

/* Detecta o display, envia a sequência de inicialização e cria a task. */
bool display_init(void)
{
    if (!i2c_is_present(ADDRESS_SH1106)) {
        ESP_LOGE(TAG, "SH1106 não respondeu em 0x%02X", ADDRESS_SH1106);
        return false;
    }
    if (dev == NULL) {
        dev = i2c_add_device(ADDRESS_SH1106);
        lock = xSemaphoreCreateMutex();
    }
    static const uint8_t init[] = {
        0xAE,       /* display desligado */
        0xD5, 0x80, /* clock */
        0xA8, 0x3F, /* multiplex 64 */
        0xD3, 0x00, /* deslocamento vertical */
        0x40,       /* linha inicial 0 */
        0xAD, 0x8B, /* conversor DC-DC ligado */
#if DISPLAY_ROTATE_180
        0xA0, 0xC0, /* orientação girada */
#else
        0xA1, /* segmentos invertidos (coluna 127 -> SEG0) */
        0xC8, /* varredura COM de baixo para cima */
#endif
        0xDA, 0x12, /* configuração dos pinos COM */
        0x81, 0xCF, /* contraste */
        0xD9, 0x22, /* pré-carga */
        0xDB, 0x35, /* VCOMH */
        0xA4,       /* exibe a RAM */
        0xA6,       /* modo normal (não invertido) */
    };
    for (size_t i = 0; i < sizeof(init); i += 8) {
        size_t n = sizeof(init) - i < 8 ? sizeof(init) - i : 8;
        if (commands(init + i, n) != ESP_OK) {
            ESP_LOGE(TAG, "Falha na inicialização");
            return false;
        }
    }
    memset(pending, 0, sizeof(pending));
    force = true;
    if (job == NULL) {
        xTaskCreatePinnedToCore(job_display, "display", 3072, NULL, 5, &job, 1);
    }
    xTaskNotifyGive(job);
    display_set_power(true);
    return true;
}

/* Copia o framebuffer para envio e acorda a task. */
void display_send(const gfx_t *g)
{
    if (job == NULL) {
        return;
    }
    xSemaphoreTake(lock, portMAX_DELAY);
    memcpy(pending, g->buf, sizeof(pending));
    xSemaphoreGive(lock);
    xTaskNotifyGive(job);
}

/* Liga ou desliga o painel (o conteúdo da RAM é mantido). */
void display_set_power(bool enabled)
{
    if (dev) {
        const uint8_t c = enabled ? 0xAF : 0xAE;
        commands(&c, 1);
    }
}

/* Ajusta o contraste (0..255). */
void display_contrast(uint8_t value)
{
    if (dev) {
        const uint8_t c[] = {0x81, value};
        commands(c, sizeof(c));
    }
}

/* Copia o último quadro enviado (usado pela captura de tela). */
void display_copy(uint8_t destination[GFX_HEIGHT / 8][GFX_WIDTH])
{
    if (lock) {
        xSemaphoreTake(lock, portMAX_DELAY);
        memcpy(destination, pending, sizeof(pending));
        xSemaphoreGive(lock);
    }
}
