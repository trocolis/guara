#include "audio.h"

#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "pinos.h"

typedef struct {
    nota_t nota;
    bool forcar;
} efeito_t;

static QueueHandle_t fila;
static SemaphoreHandle_t trava;
static fonte_musica_t musica;
static volatile bool musica_ativa, musica_pausada, mudo;
static volatile int volume = 3;

static const uint32_t DUTY_VOLUME[] = {0, 24, 96, 512}; /* de 1024 (10 bits) */

static void som(uint16_t freq, bool forcar)
{
    if (freq == 0 || (mudo && !forcar)) {
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
    } else {
        ledc_set_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0, freq);
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, forcar ? 512 : DUTY_VOLUME[volume]);
    }
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

static void fechar_musica(void)
{
    fonte_musica_t antiga = musica;
    musica_ativa = false;
    musica_pausada = false;
    musica = (fonte_musica_t){0};
    if (antiga.fechar) {
        antiga.fechar(antiga.ctx);
    }
}

static void tarefa_audio(void *arg)
{
    (void)arg;
    efeito_t ef;
    for (;;) {
        if (xQueueReceive(fila, &ef, 0) == pdTRUE) {
            som(ef.nota.freq, ef.forcar);
            vTaskDelay(pdMS_TO_TICKS(ef.nota.ms ? ef.nota.ms : 1));
            if (uxQueueMessagesWaiting(fila) == 0) {
                som(0, false);
            }
            continue;
        }
        xSemaphoreTake(trava, portMAX_DELAY);
        bool tocar = musica_ativa && !musica_pausada;
        nota_t n = {0, 0};
        if (tocar && !musica.proxima(musica.ctx, &n)) {
            fechar_musica();
            tocar = false;
        }
        xSemaphoreGive(trava);
        if (tocar) {
            som(n.freq, false);
            /* Toca a nota em fatias para atender efeitos rapidamente */
            uint32_t restante = n.ms;
            while (restante > 0 && uxQueueMessagesWaiting(fila) == 0) {
                uint32_t fatia = restante > 20 ? 20 : restante;
                vTaskDelay(pdMS_TO_TICKS(fatia));
                restante -= fatia;
            }
            som(0, false);
        } else {
            som(0, false);
            if (xQueuePeek(fila, &ef, pdMS_TO_TICKS(50)) != pdTRUE) {
                continue;
            }
        }
    }
}

void audio_iniciar(void)
{
    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 1000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer);
    ledc_channel_config_t canal = {
        .gpio_num = PINO_BUZZER,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
    };
    ledc_channel_config(&canal);
    fila = xQueueCreate(64, sizeof(efeito_t));
    trava = xSemaphoreCreateMutex();
    xTaskCreatePinnedToCore(tarefa_audio, "audio", 4096, NULL, 7, NULL, 0);
}

void audio_mudo(bool m) { mudo = m; }
bool audio_esta_mudo(void) { return mudo; }

void audio_volume(int nivel)
{
    volume = nivel < 1 ? 1 : (nivel > 3 ? 3 : nivel);
}

int audio_nivel_volume(void) { return volume; }

void audio_efeito(const nota_t *notas, int quantidade, bool forcar)
{
    if (!fila || (mudo && !forcar)) {
        return;
    }
    for (int i = 0; i < quantidade; i++) {
        efeito_t ef = {notas[i], forcar};
        xQueueSend(fila, &ef, 0);
    }
}

void audio_tom(uint16_t freq, uint16_t ms)
{
    nota_t n = {freq, ms};
    audio_efeito(&n, 1, false);
}

void audio_parar_efeitos(void)
{
    if (fila) {
        xQueueReset(fila);
    }
}

void audio_tocar_musica(const fonte_musica_t *fonte)
{
    xSemaphoreTake(trava, portMAX_DELAY);
    fechar_musica();
    musica = *fonte;
    musica_ativa = true;
    xSemaphoreGive(trava);
}

void audio_parar_musica(void)
{
    xSemaphoreTake(trava, portMAX_DELAY);
    fechar_musica();
    xSemaphoreGive(trava);
}

void audio_pausar_musica(bool pausar) { musica_pausada = pausar; }
bool audio_musica_ativa(void) { return musica_ativa; }
bool audio_musica_pausada(void) { return musica_pausada; }

void audio_bip(void)
{
    audio_tom(2000, 30);
}

void audio_erro(void)
{
    static const nota_t n[] = {{300, 80}, {0, 30}, {250, 120}};
    audio_efeito(n, 3, false);
}

void audio_mordida(void)
{
    static const nota_t n[] = {{180, 25}, {0, 15}, {120, 35}};
    audio_efeito(n, 3, false);
}

void audio_vitoria(void)
{
    static const nota_t n[] = {{523, 120}, {659, 120}, {784, 120}, {1047, 300}, {0, 60}, {784, 120}, {1047, 400}};
    audio_efeito(n, 7, false);
}

void audio_derrota(void)
{
    static const nota_t n[] = {{392, 200}, {349, 200}, {330, 200}, {262, 500}};
    audio_efeito(n, 4, false);
}

void audio_alarme(void)
{
    static const nota_t n[] = {{2093, 120}, {0, 80}, {2093, 120}, {0, 80}, {2093, 120}, {0, 500}};
    audio_efeito(n, 6, true);
}

void audio_level_up(void)
{
    static const nota_t n[] = {{659, 90}, {784, 90}, {988, 90}, {1319, 250}};
    audio_efeito(n, 4, false);
}
