/*
 * Console de depuração pela UART0: injeta teclas e imprime a tela e o estado.
 * Usado por tools/console.py.
 */
#include "debug_console.h"

#include <stdio.h>

#include "app.h"
#include "battery.h"
#include "buttons.h"
#include "display.h"
#include "driver/uart.h"
#include "driver/uart_wakeup.h"
#include "esp_sleep.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rtc_clock.h"

/* Imprime o último quadro como 'FB:' + 2048 dígitos hexadecimais. */
static void print_framebuffer(void)
{
    static uint8_t q[GFX_HEIGHT / 8][GFX_WIDTH];
    static char line[GFX_WIDTH * 2 + 1];
    display_copy(q);
    printf("FB:");
    for (int p = 0; p < GFX_HEIGHT / 8; p++) {
        for (int x = 0; x < GFX_WIDTH; x++) {
            snprintf(line + x * 2, 3, "%02x", q[p][x]);
        }
        fputs(line, stdout);
    }
    printf("\n");
    fflush(stdout);
}

/* Imprime uma linha 'ST:' com o estado do Guará e do sistema. */
static void print_state(void)
{
    const guara_t *g = &g_guara;
    char hour[8];
    clock_format_hhmm(hour, sizeof(hour));
    printf("ST: guara=%d nome=%s nv=%d xp=%lu hp=%d/%d en=%d hu=%d al=%d sa=%d estados=0x%08lx nat=%d est=%d "
           "hora=%s bat=%dmV(%d%%) chrg=%d heap=%u\n",
           g_has_guara, g->name, g->level, (unsigned long)g->xp, g->hp, g->hp_max, g->energy / 100, g->mood / 100,
           g->satiety / 100, g->health / 100, (unsigned long)g->states, g->nature, g->stage, hour, battery_mv(),
           battery_percent(), battery_charging(), (unsigned)esp_get_free_heap_size());
    fflush(stdout);
}

/* Task que lê comandos de um caractere da serial. */
static void job(void *arg)
{
    (void)arg;
    uint8_t c;
    for (;;) {
        if (uart_read_bytes(UART_NUM_0, &c, 1, portMAX_DELAY) != 1) {
            continue;
        }
        switch (c) {
        case 'w': buttons_inject(KEY_UP); break;
        case 's': buttons_inject(KEY_DOWN); break;
        case 'a': buttons_inject(KEY_LEFT); break;
        case 'd': buttons_inject(KEY_RIGHT); break;
        case 'j': buttons_inject(KEY_A); break;
        case 'k': buttons_inject(KEY_B); break;
        case 'P': print_framebuffer(); break;
        case 'S': print_state(); break;
        default: break;
        }
    }
}

/* Instala o driver da UART0 e inicia a task do console. */
void debug_console_init(void)
{
    if (uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0) == ESP_OK) {
        /* No modo Ocioso o chip entra em light sleep: bordas na RX acordam,
           mas os bytes do despertar se perdem (o console.py manda um preâmbulo). */
        uart_wakeup_cfg_t wake = {.wakeup_mode = UART_WK_MODE_ACTIVE_THRESH, .rx_edge_threshold = 3};
        uart_wakeup_setup(UART_NUM_0, &wake);
        esp_sleep_enable_uart_wakeup(UART_NUM_0);
        xTaskCreatePinnedToCore(job, "depuracao", 3072, NULL, 3, NULL, 0);
    }
}
