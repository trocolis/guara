#include "depuracao.h"

#include <stdio.h>

#include "app.h"
#include "bateria.h"
#include "botoes.h"
#include "display.h"
#include "driver/uart.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "relogio.h"

static void imprimir_quadro(void)
{
    static uint8_t q[GFX_ALTURA / 8][GFX_LARGURA];
    static char linha[GFX_LARGURA * 2 + 1];
    display_copiar(q);
    printf("FB:");
    for (int p = 0; p < GFX_ALTURA / 8; p++) {
        for (int x = 0; x < GFX_LARGURA; x++) {
            snprintf(linha + x * 2, 3, "%02x", q[p][x]);
        }
        fputs(linha, stdout);
    }
    printf("\n");
    fflush(stdout);
}

static void imprimir_estado(void)
{
    const guara_t *g = &g_guara;
    char hora[8];
    relogio_formatar_hhmm(hora, sizeof(hora));
    printf("ST: guara=%d nome=%s nv=%d xp=%lu hp=%d/%d en=%d hu=%d al=%d sa=%d estados=0x%08lx nat=%d est=%d "
           "hora=%s bat=%dmV(%d%%) chrg=%d heap=%u\n",
           g_tem_guara, g->nome, g->nivel, (unsigned long)g->xp, g->hp, g->hp_max, g->energia / 100, g->humor / 100,
           g->alimentacao / 100, g->saude / 100, (unsigned long)g->estados, g->natureza, g->estagio, hora, bateria_mv(),
           bateria_porcento(), bateria_carregando(), (unsigned)esp_get_free_heap_size());
    fflush(stdout);
}

static void tarefa(void *arg)
{
    (void)arg;
    uint8_t c;
    for (;;) {
        if (uart_read_bytes(UART_NUM_0, &c, 1, portMAX_DELAY) != 1) {
            continue;
        }
        switch (c) {
        case 'w': botoes_injetar(TECLA_CIMA); break;
        case 's': botoes_injetar(TECLA_BAIXO); break;
        case 'a': botoes_injetar(TECLA_ESQ); break;
        case 'd': botoes_injetar(TECLA_DIR); break;
        case 'j': botoes_injetar(TECLA_A); break;
        case 'k': botoes_injetar(TECLA_B); break;
        case 'P': imprimir_quadro(); break;
        case 'S': imprimir_estado(); break;
        default: break;
        }
    }
}

void depuracao_iniciar(void)
{
    if (uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0) == ESP_OK) {
        xTaskCreatePinnedToCore(tarefa, "depuracao", 3072, NULL, 3, NULL, 0);
    }
}
