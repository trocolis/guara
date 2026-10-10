/*
 * Tela de espera com reticências animadas, redesenhada por um esp_timer
 * enquanto o laço principal está bloqueado numa operação demorada.
 */
#include "busy.h"

#include <stdio.h>
#include <string.h>

#include "display.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "widgets.h"

static struct {
    esp_timer_handle_t timer;
    SemaphoreHandle_t lock;
    bool active;
    char icon[32];
    char text[48];
    int percent; /* -1 = sem barra */
} busy;

/* Compõe e envia o quadro: ícone e texto (com o progresso, texto e barra). */
static void draw(void *arg)
{
    (void)arg;
    static gfx_t g;
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    xSemaphoreTake(busy.lock, portMAX_DELAY);
    if (!busy.active) {
        xSemaphoreGive(busy.lock);
        return;
    }
    gfx_init(&g);
    if (busy.percent >= 0) {
        ui_text_dots(&g, 0, GFX_WIDTH, 20, busy.text, now, GFX_WHITE);
        ui_progress(&g, 14, 34, 100, 8, busy.percent, 100);
    } else if (busy.icon[0]) {
        ui_icon(&g, busy.icon, 56, 14);
        ui_text_dots(&g, 0, GFX_WIDTH, 38, busy.text, now, GFX_WHITE);
    } else {
        ui_text_dots(&g, 0, GFX_WIDTH, 28, busy.text, now, GFX_WHITE);
    }
    xSemaphoreGive(busy.lock);
    display_send(&g);
}

/* Liga o timer (uma vez) e desenha o primeiro quadro na hora. */
static void start(void)
{
    if (!busy.timer) {
        const esp_timer_create_args_t args = {.callback = draw, .name = "espera"};
        esp_timer_create(&args, &busy.timer);
    }
    if (!busy.active) {
        busy.active = true;
        esp_timer_start_periodic(busy.timer, UI_BUSY_FRAME_MS * 1000);
    }
}

/* Mostra a espera com o ícone e o texto. */
void ui_busy_show(const char *icon, const char *text)
{
    if (!busy.lock) {
        busy.lock = xSemaphoreCreateMutex();
    }
    xSemaphoreTake(busy.lock, portMAX_DELAY);
    snprintf(busy.icon, sizeof(busy.icon), "%s", icon ? icon : "");
    snprintf(busy.text, sizeof(busy.text), "%s", text);
    busy.percent = -1;
    start();
    xSemaphoreGive(busy.lock);
    draw(NULL);
}

/* Atualiza a barra de progresso (e o texto). */
void ui_busy_progress(const char *text, int percent)
{
    if (!busy.lock) {
        busy.lock = xSemaphoreCreateMutex();
    }
    xSemaphoreTake(busy.lock, portMAX_DELAY);
    snprintf(busy.text, sizeof(busy.text), "%s", text);
    busy.icon[0] = '\0';
    busy.percent = percent;
    start();
    xSemaphoreGive(busy.lock);
    draw(NULL);
}

/* Para a espera: o laço principal voltou a desenhar. */
void ui_busy_hide(void)
{
    if (!busy.active) {
        return;
    }
    xSemaphoreTake(busy.lock, portMAX_DELAY);
    busy.active = false;
    esp_timer_stop(busy.timer);
    xSemaphoreGive(busy.lock);
}
