/*
 * Driver Wi-Fi em modo estação. Use o serviço de rede (system/network.h) nas telas.
 */
#ifndef GUARA_WIFI_H
#define GUARA_WIFI_H

#include <stdbool.h>
#include <stdint.h>

#define WIFI_MAX_NETWORKS 16

typedef struct {
    char ssid[33];
    int8_t rssi;
    bool is_open;
} wifi_network_t;

/* Liga o rádio em modo estação (aloca o driver). */
bool wifi_turn_on(void);
/* Desliga e libera a memória do driver (antes dos jogos Lua). */
void wifi_turn_off(void);
bool wifi_on(void);
int wifi_scan(wifi_network_t *networks, int max);
/* Conecta. Com `bssid` (6 bytes) e `canal` conhecidos, pula a busca (reconexão rápida). */
bool wifi_connect(const char *ssid, const char *password, uint32_t timeout_ms, const uint8_t *bssid, uint8_t channel);
/* BSSID e canal do roteador atual (para a próxima reconexão rápida). */
bool wifi_current_ap(uint8_t bssid[6], uint8_t *channel);
bool wifi_connected(void);
/* Fixa o canal do rádio (sem conexão; usado pelo ESP-NOW do Conectar). */
bool wifi_set_channel(uint8_t channel);
int wifi_rssi(void);
/* 0..3 barras para o indicador da barra superior. */
int wifi_bars(void);
const char *wifi_current_ssid(void);
void wifi_ip(char *buf, int length);

#endif
