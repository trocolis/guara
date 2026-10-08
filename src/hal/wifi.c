/*
 * Driver Wi-Fi em modo estação. As telas não usam este módulo direto: o acesso
 * passa pelo serviço de rede (system/network.c), que liga o rádio sob demanda.
 */
#include "wifi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

static const char *TAG = "wifi";

#define BIT_CONNECTED BIT0
#define BIT_FAILED BIT1

static EventGroupHandle_t events;
static esp_netif_t *netif;
static bool enabled, network_stack_started;
static char current_ssid[33];

/* Eventos do Wi-Fi: marca conexão obtida ou perdida. */
static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)data;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(events, BIT_CONNECTED);
        xEventGroupSetBits(events, BIT_FAILED);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(events, BIT_CONNECTED);
    }
}

/* Inicializa a pilha de rede (uma vez) e liga o rádio em modo estação. */
bool wifi_turn_on(void)
{
    if (enabled) {
        return true;
    }
    if (!network_stack_started) {
        esp_netif_init();
        esp_event_loop_create_default();
        events = xEventGroupCreate();
        esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL);
        esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL);
        network_stack_started = true;
    }
    if (!netif) {
        netif = esp_netif_create_default_wifi_sta();
    }
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao iniciar o driver");
        return false;
    }
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_wifi_set_mode(WIFI_MODE_STA);
    if (esp_wifi_start() != ESP_OK) {
        esp_wifi_deinit();
        return false;
    }
    enabled = true;
    return true;
}

/* Desconecta, desliga o rádio e libera a memória do driver. */
void wifi_turn_off(void)
{
    if (!enabled) {
        return;
    }
    esp_wifi_disconnect();
    esp_wifi_stop();
    esp_wifi_deinit();
    /* a interface padrão é recriada na próxima religação */
    esp_netif_destroy_default_wifi(netif);
    netif = NULL;
    xEventGroupClearBits(events, BIT_CONNECTED | BIT_FAILED);
    current_ssid[0] = '\0';
    enabled = false;
}

/* Verdadeiro se o rádio está ligado. */
bool wifi_on(void)
{
    return enabled;
}

/* Ordena redes do sinal mais forte para o mais fraco. */
static int by_rssi(const void *a, const void *b)
{
    return ((const wifi_network_t *)b)->rssi - ((const wifi_network_t *)a)->rssi;
}

/* Busca redes próximas (bloqueante). */
int wifi_scan(wifi_network_t *networks, int max)
{
    if (!wifi_turn_on()) {
        return 0;
    }
    if (esp_wifi_scan_start(NULL, true) != ESP_OK) {
        return 0;
    }
    uint16_t n = WIFI_MAX_NETWORKS;
    wifi_ap_record_t records[WIFI_MAX_NETWORKS];
    esp_wifi_scan_get_ap_records(&n, records);
    int output = 0;
    for (int i = 0; i < n && output < max; i++) {
        if (records[i].ssid[0] == '\0') {
            continue;
        }
        snprintf(networks[output].ssid, sizeof(networks[output].ssid), "%s", (const char *)records[i].ssid);
        networks[output].rssi = records[i].rssi;
        networks[output].is_open = records[i].authmode == WIFI_AUTH_OPEN;
        output++;
    }
    qsort(networks, (size_t)output, sizeof(wifi_network_t), by_rssi);
    return output;
}

/* Conecta; com BSSID e canal conhecidos, pula a busca (reconexão rápida). */
bool wifi_connect(const char *ssid, const char *password, uint32_t timeout_ms, const uint8_t *bssid, uint8_t channel)
{
    if (!wifi_turn_on()) {
        return false;
    }
    wifi_config_t cfg = {0};
    snprintf((char *)cfg.sta.ssid, sizeof(cfg.sta.ssid), "%s", ssid);
    snprintf((char *)cfg.sta.password, sizeof(cfg.sta.password), "%s", password ? password : "");
    cfg.sta.threshold.authmode = (password && *password) ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
    if (bssid && channel) {
        memcpy(cfg.sta.bssid, bssid, 6);
        cfg.sta.bssid_set = true;
        cfg.sta.channel = channel;
    }
    esp_wifi_disconnect();
    xEventGroupClearBits(events, BIT_CONNECTED | BIT_FAILED);
    esp_wifi_set_config(WIFI_IF_STA, &cfg);
    if (esp_wifi_connect() != ESP_OK) {
        return false;
    }
    EventBits_t bits = xEventGroupWaitBits(events, BIT_CONNECTED | BIT_FAILED, pdFALSE, pdFALSE, pdMS_TO_TICKS(timeout_ms));
    if (bits & BIT_CONNECTED) {
        snprintf(current_ssid, sizeof(current_ssid), "%s", ssid);
        ESP_LOGI(TAG, "Conectado a %s", ssid);
        return true;
    }
    esp_wifi_disconnect();
    return false;
}

/* Verdadeiro se há IP atribuído. */
bool wifi_connected(void)
{
    return enabled && (xEventGroupGetBits(events) & BIT_CONNECTED);
}

/* Intensidade do sinal (dBm) da rede atual. */
int wifi_rssi(void)
{
    int rssi = -127;
    if (wifi_connected()) {
        esp_wifi_sta_get_rssi(&rssi);
    }
    return rssi;
}

/* Barras de sinal (0..3) para o ícone da barra superior. */
int wifi_bars(void)
{
    if (!wifi_connected()) {
        return 0;
    }
    int r = wifi_rssi();
    return r > -60 ? 3 : (r > -72 ? 2 : 1);
}

/* Nome da rede conectada. */
const char *wifi_current_ssid(void)
{
    return current_ssid;
}

/* Endereço IP atual como texto. */
void wifi_ip(char *buf, int length)
{
    esp_netif_ip_info_t ip;
    if (wifi_connected() && esp_netif_get_ip_info(netif, &ip) == ESP_OK) {
        snprintf(buf, (size_t)length, IPSTR, IP2STR(&ip.ip));
    } else {
        snprintf(buf, (size_t)length, "-");
    }
}

/* BSSID e canal do roteador atual (para a próxima reconexão rápida). */
bool wifi_current_ap(uint8_t bssid[6], uint8_t *channel)
{
    wifi_ap_record_t ap;
    if (!wifi_connected() || esp_wifi_sta_get_ap_info(&ap) != ESP_OK) {
        return false;
    }
    memcpy(bssid, ap.bssid, 6);
    *channel = ap.primary;
    return true;
}
