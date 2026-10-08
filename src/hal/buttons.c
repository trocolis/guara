/*
 * Botões: leitura periódica com debounce, auto-repetição dos direcionais e
 * fila de eventos para a interface.
 */
#include "buttons.h"

#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "pins.h"

#define PERIOD_MS 5
#define SAMPLES_STABLE 4
#define REPEAT_AFTER_MS 400
#define REPEAT_EVERY_MS 110

static const gpio_num_t pins[COUNT_KEYS] = {
    PIN_UP, PIN_DOWN, PIN_SCREEN_LEFT, PIN_SCREEN_RIGHT, PIN_A, PIN_B,
};

static QueueHandle_t queue;
static volatile bool state[COUNT_KEYS];
static volatile uint32_t last_use;

/* Milissegundos desde o boot. */
static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* Coloca um evento na fila (descarta se estiver cheia). */
static void publish(input_key_t t, key_event_type_t type)
{
    key_event_t ev = {t, type};
    xQueueSend(queue, &ev, 0);
}

/* Task de leitura: a cada 5 ms, 4 leituras iguais confirmam a mudança. */
static void job_buttons(void *arg)
{
    (void)arg;
    uint8_t debounce_count[COUNT_KEYS] = {0};
    uint32_t pressed_at[COUNT_KEYS] = {0}, repeated_at[COUNT_KEYS] = {0};
    for (;;) {
        uint32_t t = now_ms();
        for (int i = 0; i < COUNT_KEYS; i++) {
            bool bytes_read = gpio_get_level(pins[i]) == 0;
            if (bytes_read != state[i]) {
                if (++debounce_count[i] >= SAMPLES_STABLE) {
                    state[i] = bytes_read;
                    debounce_count[i] = 0;
                    if (bytes_read) {
                        pressed_at[i] = repeated_at[i] = t;
                        last_use = t;
                        publish((input_key_t)i, KEY_PRESSED);
                    } else {
                        publish((input_key_t)i, KEY_RELEASED);
                    }
                }
            } else {
                debounce_count[i] = 0;
                bool dpad = i <= KEY_RIGHT;
                if (bytes_read && dpad && t - pressed_at[i] >= REPEAT_AFTER_MS && t - repeated_at[i] >= REPEAT_EVERY_MS) {
                    repeated_at[i] = t;
                    last_use = t;
                    publish((input_key_t)i, KEY_REPEATED);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(PERIOD_MS));
    }
}

/* Configura os GPIOs com pull-up e inicia a task de leitura. */
void buttons_init(void)
{
    uint64_t mask = 0;
    for (int i = 0; i < COUNT_KEYS; i++) {
        mask |= 1ull << pins[i];
    }
    gpio_config_t cfg = {
        .pin_bit_mask = mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    /* Teclas já pressionadas no boot (ex.: a que acordou o aparelho) não geram evento. */
    for (int i = 0; i < COUNT_KEYS; i++) {
        state[i] = gpio_get_level(pins[i]) == 0;
    }
    queue = xQueueCreate(16, sizeof(key_event_t));
    last_use = now_ms();
    xTaskCreatePinnedToCore(job_buttons, "botoes", 2048, NULL, 6, NULL, 1);
}

/* Retira um evento da fila, esperando até `timeout_ms`. */
bool buttons_read(key_event_t *ev, uint32_t timeout_ms)
{
    return queue && xQueueReceive(queue, ev, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

/* Verdadeiro enquanto a tecla está pressionada. */
bool buttons_is_held(input_key_t t)
{
    return t < COUNT_KEYS && state[t];
}

/* Momento do último uso, para o timeout de inatividade. */
uint32_t buttons_last_use_ms(void)
{
    return last_use;
}

/* Descarta os eventos pendentes. */
void buttons_flush(void)
{
    if (queue) {
        xQueueReset(queue);
    }
}

/* Simula pressionar e soltar uma tecla (console de depuração). */
void buttons_inject(input_key_t t)
{
    if (!queue || t >= COUNT_KEYS) {
        return;
    }
    last_use = now_ms();
    publish(t, KEY_PRESSED);
    publish(t, KEY_RELEASED);
}
