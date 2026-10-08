#include "display.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "i2c.h"
#include "pinos.h"

static const char *TAG = "display";

/* Defina como 1 se a imagem aparecer de cabeça para baixo. */
#ifndef DISPLAY_GIRAR_180
#define DISPLAY_GIRAR_180 0
#endif

/* O SH1106 tem RAM de 132 colunas; a área visível começa na coluna 2. */
#define DESLOCAMENTO_COLUNA 2

static i2c_master_dev_handle_t dev;
static SemaphoreHandle_t trava;
static TaskHandle_t tarefa;
static uint8_t pendente[GFX_ALTURA / 8][GFX_LARGURA];
static uint8_t enviado[GFX_ALTURA / 8][GFX_LARGURA];
static bool forcar = true;

static esp_err_t comandos(const uint8_t *cmds, size_t n)
{
    uint8_t buf[16];
    buf[0] = 0x00; /* Co=0, D/C=0: sequência de comandos */
    memcpy(buf + 1, cmds, n);
    return i2c_master_transmit(dev, buf, n + 1, 100);
}

static esp_err_t enviar_pagina(int pagina, const uint8_t *dados)
{
    const uint8_t endereco[] = {
        (uint8_t)(0xB0 | pagina),
        (uint8_t)(0x00 | (DESLOCAMENTO_COLUNA & 0x0F)),
        (uint8_t)(0x10 | (DESLOCAMENTO_COLUNA >> 4)),
    };
    esp_err_t err = comandos(endereco, sizeof(endereco));
    if (err != ESP_OK) {
        return err;
    }
    uint8_t buf[GFX_LARGURA + 1];
    buf[0] = 0x40; /* dados */
    memcpy(buf + 1, dados, GFX_LARGURA);
    return i2c_master_transmit(dev, buf, sizeof(buf), 100);
}

static void tarefa_display(void *arg)
{
    (void)arg;
    uint8_t local[GFX_ALTURA / 8][GFX_LARGURA];
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        xSemaphoreTake(trava, portMAX_DELAY);
        memcpy(local, pendente, sizeof(local));
        bool tudo = forcar;
        forcar = false;
        xSemaphoreGive(trava);
        for (int p = 0; p < GFX_ALTURA / 8; p++) {
            if (tudo || memcmp(local[p], enviado[p], GFX_LARGURA) != 0) {
                if (enviar_pagina(p, local[p]) == ESP_OK) {
                    memcpy(enviado[p], local[p], GFX_LARGURA);
                } else {
                    forcar = true;
                }
            }
        }
    }
}

bool display_iniciar(void)
{
    if (!i2c_presente(ENDERECO_SH1106)) {
        ESP_LOGE(TAG, "SH1106 não respondeu em 0x%02X", ENDERECO_SH1106);
        return false;
    }
    if (dev == NULL) {
        dev = i2c_dispositivo(ENDERECO_SH1106);
        trava = xSemaphoreCreateMutex();
    }
    static const uint8_t init[] = {
        0xAE,       /* display desligado */
        0xD5, 0x80, /* clock */
        0xA8, 0x3F, /* multiplex 64 */
        0xD3, 0x00, /* deslocamento vertical */
        0x40,       /* linha inicial 0 */
        0xAD, 0x8B, /* conversor DC-DC ligado */
#if DISPLAY_GIRAR_180
        0xA0, 0xC0, /* orientação girada */
#else
        0xA1,       /* segmentos invertidos (coluna 127 -> SEG0) */
        0xC8,       /* varredura COM de baixo para cima */
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
        if (comandos(init + i, n) != ESP_OK) {
            ESP_LOGE(TAG, "Falha na inicialização");
            return false;
        }
    }
    memset(pendente, 0, sizeof(pendente));
    forcar = true;
    if (tarefa == NULL) {
        xTaskCreatePinnedToCore(tarefa_display, "display", 3072, NULL, 5, &tarefa, 1);
    }
    xTaskNotifyGive(tarefa);
    display_ligar(true);
    return true;
}

void display_enviar(const gfx_t *g)
{
    if (tarefa == NULL) {
        return;
    }
    xSemaphoreTake(trava, portMAX_DELAY);
    memcpy(pendente, g->buf, sizeof(pendente));
    xSemaphoreGive(trava);
    xTaskNotifyGive(tarefa);
}

void display_ligar(bool ligado)
{
    if (dev) {
        const uint8_t c = ligado ? 0xAF : 0xAE;
        comandos(&c, 1);
    }
}

void display_contraste(uint8_t valor)
{
    if (dev) {
        const uint8_t c[] = {0x81, valor};
        comandos(c, sizeof(c));
    }
}

void display_copiar(uint8_t destino[GFX_ALTURA / 8][GFX_LARGURA])
{
    if (trava) {
        xSemaphoreTake(trava, portMAX_DELAY);
        memcpy(destino, pendente, sizeof(pendente));
        xSemaphoreGive(trava);
    }
}
