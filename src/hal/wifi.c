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

#define BIT_CONECTADO BIT0
#define BIT_FALHOU BIT1

static EventGroupHandle_t eventos;
static esp_netif_t *netif;
static bool ligado, base_iniciada;
static char ssid_atual[33];

static void ao_evento(void *arg, esp_event_base_t base, int32_t id, void *dados)
{
    (void)arg;
    (void)dados;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(eventos, BIT_CONECTADO);
        xEventGroupSetBits(eventos, BIT_FALHOU);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(eventos, BIT_CONECTADO);
    }
}

bool wifi_ligar(void)
{
    if (ligado) {
        return true;
    }
    if (!base_iniciada) {
        esp_netif_init();
        esp_event_loop_create_default();
        eventos = xEventGroupCreate();
        esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, ao_evento, NULL);
        esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, ao_evento, NULL);
        base_iniciada = true;
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
    ligado = true;
    return true;
}

void wifi_desligar(void)
{
    if (!ligado) {
        return;
    }
    esp_wifi_disconnect();
    esp_wifi_stop();
    esp_wifi_deinit();
    /* a interface padrão é recriada na próxima religação */
    esp_netif_destroy_default_wifi(netif);
    netif = NULL;
    xEventGroupClearBits(eventos, BIT_CONECTADO | BIT_FALHOU);
    ssid_atual[0] = '\0';
    ligado = false;
}

bool wifi_ligado(void)
{
    return ligado;
}

static int por_rssi(const void *a, const void *b)
{
    return ((const rede_wifi_t *)b)->rssi - ((const rede_wifi_t *)a)->rssi;
}

int wifi_buscar(rede_wifi_t *redes, int max)
{
    if (!wifi_ligar()) {
        return 0;
    }
    if (esp_wifi_scan_start(NULL, true) != ESP_OK) {
        return 0;
    }
    uint16_t n = WIFI_MAX_REDES;
    wifi_ap_record_t registros[WIFI_MAX_REDES];
    esp_wifi_scan_get_ap_records(&n, registros);
    int saida = 0;
    for (int i = 0; i < n && saida < max; i++) {
        if (registros[i].ssid[0] == '\0') {
            continue;
        }
        snprintf(redes[saida].ssid, sizeof(redes[saida].ssid), "%s", (const char *)registros[i].ssid);
        redes[saida].rssi = registros[i].rssi;
        redes[saida].aberta = registros[i].authmode == WIFI_AUTH_OPEN;
        saida++;
    }
    qsort(redes, (size_t)saida, sizeof(rede_wifi_t), por_rssi);
    return saida;
}

bool wifi_conectar(const char *ssid, const char *senha, uint32_t espera_ms, const uint8_t *bssid, uint8_t canal)
{
    if (!wifi_ligar()) {
        return false;
    }
    wifi_config_t cfg = {0};
    snprintf((char *)cfg.sta.ssid, sizeof(cfg.sta.ssid), "%s", ssid);
    snprintf((char *)cfg.sta.password, sizeof(cfg.sta.password), "%s", senha ? senha : "");
    cfg.sta.threshold.authmode = (senha && *senha) ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
    if (bssid && canal) {
        memcpy(cfg.sta.bssid, bssid, 6);
        cfg.sta.bssid_set = true;
        cfg.sta.channel = canal;
    }
    esp_wifi_disconnect();
    xEventGroupClearBits(eventos, BIT_CONECTADO | BIT_FALHOU);
    esp_wifi_set_config(WIFI_IF_STA, &cfg);
    if (esp_wifi_connect() != ESP_OK) {
        return false;
    }
    EventBits_t bits = xEventGroupWaitBits(eventos, BIT_CONECTADO | BIT_FALHOU, pdFALSE, pdFALSE, pdMS_TO_TICKS(espera_ms));
    if (bits & BIT_CONECTADO) {
        snprintf(ssid_atual, sizeof(ssid_atual), "%s", ssid);
        ESP_LOGI(TAG, "Conectado a %s", ssid);
        return true;
    }
    esp_wifi_disconnect();
    return false;
}

bool wifi_conectado(void)
{
    return ligado && (xEventGroupGetBits(eventos) & BIT_CONECTADO);
}

int wifi_rssi(void)
{
    int rssi = -127;
    if (wifi_conectado()) {
        esp_wifi_sta_get_rssi(&rssi);
    }
    return rssi;
}

int wifi_barras(void)
{
    if (!wifi_conectado()) {
        return 0;
    }
    int r = wifi_rssi();
    return r > -60 ? 3 : (r > -72 ? 2 : 1);
}

const char *wifi_ssid_atual(void)
{
    return ssid_atual;
}

void wifi_ip(char *buf, int tam)
{
    esp_netif_ip_info_t ip;
    if (wifi_conectado() && esp_netif_get_ip_info(netif, &ip) == ESP_OK) {
        snprintf(buf, (size_t)tam, IPSTR, IP2STR(&ip.ip));
    } else {
        snprintf(buf, (size_t)tam, "-");
    }
}

bool wifi_ap_atual(uint8_t bssid[6], uint8_t *canal)
{
    wifi_ap_record_t ap;
    if (!wifi_conectado() || esp_wifi_sta_get_ap_info(&ap) != ESP_OK) {
        return false;
    }
    memcpy(bssid, ap.bssid, 6);
    *canal = ap.primary;
    return true;
}
