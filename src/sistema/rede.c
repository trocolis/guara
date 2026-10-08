#include "rede.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "relogio.h"

static const char *TAG = "rede";

#define CONEXAO_RAPIDA_MS 4000
#define CONEXAO_COMPLETA_MS 8000
#define NTP_ESPERA_MS 5000
#define NTP_NOVA_TENTATIVA_MS (30 * 60 * 1000) /* após falha */

typedef struct {
    rede_tarefa_fn fn;
    void *ctx;
} tarefa_t;

static SemaphoreHandle_t trava;
static QueueHandle_t fila;
static volatile int usuarios;
static volatile uint32_t ultimo_uso_ms;
static volatile bool conectando, suspenso;
static volatile int barras;
static uint32_t ntp_tentado_ms;
static bool ntp_tentado;

static uint32_t agora_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

bool rede_permitida(void)
{
    return g_cfg.wifi_ligado && !suspenso;
}

estado_rede_t rede_estado(void)
{
    if (!g_cfg.wifi_ligado) {
        return REDE_DESATIVADA;
    }
    if (conectando) {
        return REDE_CONECTANDO;
    }
    return wifi_conectado() ? REDE_CONECTADA : REDE_OCIOSA;
}

int rede_barras(void)
{
    return barras;
}

static void lembrar_roteador(const char *ssid)
{
    rede_rapida_t r = {0};
    if (wifi_ap_atual(r.bssid, &r.canal)) {
        snprintf(r.ssid, sizeof(r.ssid), "%s", ssid);
        config_salvar_rede_rapida(&r);
    }
}

/* Deve ser chamada com a trava. Tenta o último roteador (rápido) e depois as redes salvas. */
static bool garantir_conexao(uint32_t espera_ms)
{
    if (wifi_conectado()) {
        return true;
    }
    if (!rede_permitida()) {
        return false;
    }
    conectando = true;
    uint32_t inicio = agora_ms();
    rede_salva_t salvas[CONFIG_MAX_REDES];
    int n = config_redes(salvas);
    rede_rapida_t rapida;
    bool ok = false;
    if (config_rede_rapida(&rapida)) {
        for (int i = 0; i < n && !ok; i++) {
            if (strcmp(salvas[i].ssid, rapida.ssid) == 0) {
                ok = wifi_conectar(salvas[i].ssid, salvas[i].senha, CONEXAO_RAPIDA_MS, rapida.bssid, rapida.canal);
            }
        }
    }
    for (int i = n - 1; i >= 0 && !ok && agora_ms() - inicio < espera_ms; i--) {
        ok = wifi_conectar(salvas[i].ssid, salvas[i].senha, CONEXAO_COMPLETA_MS, NULL, 0);
        if (ok) {
            lembrar_roteador(salvas[i].ssid);
        }
    }
    if (!ok && usuarios == 0) {
        wifi_desligar();
    }
    conectando = false;
    ultimo_uso_ms = agora_ms();
    if (ok) {
        ESP_LOGI(TAG, "Conectado em %lu ms", (unsigned long)(agora_ms() - inicio));
    } else {
        ESP_LOGW(TAG, "Sem conexão (%lu ms)", (unsigned long)(agora_ms() - inicio));
    }
    return ok;
}

void rede_usar(void)
{
    xSemaphoreTake(trava, portMAX_DELAY);
    usuarios++;
    ultimo_uso_ms = agora_ms();
    xSemaphoreGive(trava);
}

bool rede_aguardar(uint32_t espera_ms)
{
    xSemaphoreTake(trava, portMAX_DELAY);
    bool ok = garantir_conexao(espera_ms);
    xSemaphoreGive(trava);
    return ok;
}

void rede_liberar(void)
{
    xSemaphoreTake(trava, portMAX_DELAY);
    if (usuarios > 0) {
        usuarios--;
    }
    ultimo_uso_ms = agora_ms();
    xSemaphoreGive(trava);
}

bool rede_executar(rede_tarefa_fn fn, void *ctx)
{
    tarefa_t t = {fn, ctx};
    return fila && xQueueSend(fila, &t, 0) == pdTRUE;
}

int rede_buscar(rede_wifi_t *redes, int max)
{
    xSemaphoreTake(trava, portMAX_DELAY);
    int n = suspenso ? 0 : wifi_buscar(redes, max);
    ultimo_uso_ms = agora_ms();
    xSemaphoreGive(trava);
    return n;
}

bool rede_conectar_nova(const char *ssid, const char *senha)
{
    xSemaphoreTake(trava, portMAX_DELAY);
    conectando = true;
    bool ok = !suspenso && wifi_conectar(ssid, senha, 10000, NULL, 0);
    conectando = false;
    if (ok) {
        config_salvar_rede(ssid, senha);
        lembrar_roteador(ssid);
        relogio_sincronizar_ntp(NTP_ESPERA_MS);
    }
    ultimo_uso_ms = agora_ms();
    xSemaphoreGive(trava);
    return ok;
}

void rede_status(char *buf, int tam)
{
    static const char *const nomes[] = {"desativada (OFF)", "permitida, rádio desligado", "conectando", "conectada"};
    if (xSemaphoreTake(trava, pdMS_TO_TICKS(200)) != pdTRUE) {
        snprintf(buf, (size_t)tam, "Rede: conectando...");
        return;
    }
    estado_rede_t e = rede_estado();
    char ip[20];
    wifi_ip(ip, sizeof(ip));
    uint32_t ntp = relogio_ultimo_ntp();
    char ntp_txt[24] = "nunca";
    if (ntp) {
        struct tm tm;
        time_t t = ntp;
        localtime_r(&t, &tm);
        snprintf(ntp_txt, sizeof(ntp_txt), "%02d/%02d %02d:%02d", tm.tm_mday, tm.tm_mon + 1, tm.tm_hour, tm.tm_min);
    }
    snprintf(buf, (size_t)tam, "Rede: %s\nRoteador: %s\nIP: %s\nSinal: %d dBm\nÚltimo NTP: %s", nomes[e],
             e == REDE_CONECTADA ? wifi_ssid_atual() : "-", ip, e == REDE_CONECTADA ? wifi_rssi() : 0, ntp_txt);
    xSemaphoreGive(trava);
}

void rede_desligar(void)
{
    xSemaphoreTake(trava, portMAX_DELAY);
    wifi_desligar();
    barras = 0;
    xSemaphoreGive(trava);
}

void rede_suspender(bool suspender)
{
    xSemaphoreTake(trava, portMAX_DELAY);
    suspenso = suspender;
    if (suspender) {
        wifi_desligar();
        barras = 0;
    }
    xSemaphoreGive(trava);
}

/* ------------------------------------------------------------------------- */

static void tarefa_ntp(bool conectado, void *ctx)
{
    (void)ctx;
    if (conectado) {
        relogio_sincronizar_ntp(NTP_ESPERA_MS);
    }
}

static bool ntp_vencido(void)
{
    if (!relogio_valido() || relogio_ultimo_ntp() == 0) {
        return true;
    }
    return relogio_agora() - relogio_ultimo_ntp() >= REDE_NTP_INTERVALO_S;
}

static void manutencao(void)
{
    uint32_t agora = agora_ms();
    xSemaphoreTake(trava, portMAX_DELAY);
    if (wifi_ligado() && usuarios == 0 && !conectando && agora - ultimo_uso_ms >= REDE_OCIOSO_MS) {
        ESP_LOGI(TAG, "Ociosa: desligando o rádio");
        wifi_desligar();
    }
    barras = wifi_conectado() ? wifi_barras() : 0;
    static rede_salva_t salvas[CONFIG_MAX_REDES];
    bool agendar = rede_permitida() && ntp_vencido() &&
                   (!ntp_tentado || agora - ntp_tentado_ms >= NTP_NOVA_TENTATIVA_MS) && config_redes(salvas) > 0;
    if (agendar) {
        ntp_tentado = true;
        ntp_tentado_ms = agora;
    }
    xSemaphoreGive(trava);
    if (agendar) {
        rede_executar(tarefa_ntp, NULL);
    }
}

static void tarefa_rede(void *arg)
{
    (void)arg;
    tarefa_t t;
    for (;;) {
        if (xQueueReceive(fila, &t, pdMS_TO_TICKS(1000)) == pdTRUE) {
            while (suspenso) {
                vTaskDelay(pdMS_TO_TICKS(500)); /* espera o jogo terminar */
            }
            rede_usar();
            bool ok = rede_aguardar(CONEXAO_RAPIDA_MS + CONEXAO_COMPLETA_MS);
            t.fn(ok, t.ctx);
            rede_liberar();
        }
        manutencao();
    }
}

void rede_iniciar(void)
{
    trava = xSemaphoreCreateMutex();
    fila = xQueueCreate(8, sizeof(tarefa_t));
    ultimo_uso_ms = agora_ms();
    xTaskCreatePinnedToCore(tarefa_rede, "rede", 4096, NULL, 4, NULL, 0);
}
