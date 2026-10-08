#ifndef GUARA_WIFI_H
#define GUARA_WIFI_H

#include <stdbool.h>
#include <stdint.h>

#define WIFI_MAX_REDES 16

typedef struct {
    char ssid[33];
    int8_t rssi;
    bool aberta;
} rede_wifi_t;

/* Liga o rádio em modo estação (aloca o driver). */
bool wifi_ligar(void);
/* Desliga e libera a memória do driver (antes dos jogos Lua). */
void wifi_desligar(void);
bool wifi_ligado(void);
int wifi_buscar(rede_wifi_t *redes, int max);
/* Conecta. Com `bssid` (6 bytes) e `canal` conhecidos, pula a busca (reconexão rápida). */
bool wifi_conectar(const char *ssid, const char *senha, uint32_t espera_ms, const uint8_t *bssid, uint8_t canal);
/* BSSID e canal do roteador atual (para a próxima reconexão rápida). */
bool wifi_ap_atual(uint8_t bssid[6], uint8_t *canal);
bool wifi_conectado(void);
int wifi_rssi(void);
/* 0..3 barras para o indicador da barra superior. */
int wifi_barras(void);
const char *wifi_ssid_atual(void);
void wifi_ip(char *buf, int tam);

#endif
