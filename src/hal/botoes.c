#include "botoes.h"

#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "pinos.h"

#define PERIODO_MS 5
#define AMOSTRAS_ESTAVEL 4
#define REPETIR_APOS_MS 400
#define REPETIR_CADA_MS 110

static const gpio_num_t pinos[QTD_TECLAS] = {
    PINO_CIMA, PINO_BAIXO, PINO_TELA_ESQ, PINO_TELA_DIR, PINO_A, PINO_B,
};

static QueueHandle_t fila;
static volatile bool estado[QTD_TECLAS];
static volatile uint32_t ultimo_uso;

static uint32_t agora_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void publicar(tecla_t t, tipo_evento_t tipo)
{
    evento_tecla_t ev = {t, tipo};
    xQueueSend(fila, &ev, 0);
}

static void tarefa_botoes(void *arg)
{
    (void)arg;
    uint8_t contagem[QTD_TECLAS] = {0};
    uint32_t pressionou_em[QTD_TECLAS] = {0}, repetiu_em[QTD_TECLAS] = {0};
    for (;;) {
        uint32_t t = agora_ms();
        for (int i = 0; i < QTD_TECLAS; i++) {
            bool lido = gpio_get_level(pinos[i]) == 0;
            if (lido != estado[i]) {
                if (++contagem[i] >= AMOSTRAS_ESTAVEL) {
                    estado[i] = lido;
                    contagem[i] = 0;
                    if (lido) {
                        pressionou_em[i] = repetiu_em[i] = t;
                        ultimo_uso = t;
                        publicar((tecla_t)i, TECLA_PRESSIONOU);
                    } else {
                        publicar((tecla_t)i, TECLA_SOLTOU);
                    }
                }
            } else {
                contagem[i] = 0;
                bool direcional = i <= TECLA_DIR;
                if (lido && direcional && t - pressionou_em[i] >= REPETIR_APOS_MS && t - repetiu_em[i] >= REPETIR_CADA_MS) {
                    repetiu_em[i] = t;
                    ultimo_uso = t;
                    publicar((tecla_t)i, TECLA_REPETIU);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(PERIODO_MS));
    }
}

void botoes_iniciar(void)
{
    uint64_t mascara = 0;
    for (int i = 0; i < QTD_TECLAS; i++) {
        mascara |= 1ull << pinos[i];
    }
    gpio_config_t cfg = {
        .pin_bit_mask = mascara,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    /* Teclas já pressionadas no boot (ex.: a que acordou o aparelho) não geram evento. */
    for (int i = 0; i < QTD_TECLAS; i++) {
        estado[i] = gpio_get_level(pinos[i]) == 0;
    }
    fila = xQueueCreate(16, sizeof(evento_tecla_t));
    ultimo_uso = agora_ms();
    xTaskCreatePinnedToCore(tarefa_botoes, "botoes", 2048, NULL, 6, NULL, 1);
}

bool botoes_ler(evento_tecla_t *ev, uint32_t espera_ms)
{
    return fila && xQueueReceive(fila, ev, pdMS_TO_TICKS(espera_ms)) == pdTRUE;
}

bool botoes_segurando(tecla_t t)
{
    return t < QTD_TECLAS && estado[t];
}

uint32_t botoes_ultimo_uso_ms(void)
{
    return ultimo_uso;
}

void botoes_esvaziar(void)
{
    if (fila) {
        xQueueReset(fila);
    }
}

void botoes_injetar(tecla_t t)
{
    if (!fila || t >= QTD_TECLAS) {
        return;
    }
    ultimo_uso = agora_ms();
    publicar(t, TECLA_PRESSIONOU);
    publicar(t, TECLA_SOLTOU);
}
