#ifndef GUARA_CONFIG_H
#define GUARA_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

/* Configurações do sistema, persistidas na NVS (namespace "guara"). */
typedef struct {
    bool mudo;
    uint8_t volume;        /* 1..3 */
    uint8_t economia;      /* índice em CONFIG_ECONOMIA_MIN */
    uint16_t dormir_min;   /* hora de dormir do Guará (minuto do dia) */
    uint16_t acordar_min;
    char fuso[32];         /* POSIX TZ */
    bool wifi_ligado;
    bool bt_ligado;
} config_t;

#define CONFIG_QTD_ECONOMIA 5
extern const uint8_t CONFIG_ECONOMIA_MIN[CONFIG_QTD_ECONOMIA]; /* 0 = desativado */

#define CONFIG_MAX_REDES 5
typedef struct {
    char ssid[33];
    char senha[65];
} rede_salva_t;

extern config_t g_cfg;

void config_carregar(void);
void config_salvar(void);

int config_redes(rede_salva_t *redes);
void config_salvar_rede(const char *ssid, const char *senha);
void config_esquecer_rede(int indice);

/* Último roteador conectado: permite reconectar sem busca (~1-2 s). */
typedef struct {
    char ssid[33];
    uint8_t bssid[6];
    uint8_t canal;
} rede_rapida_t;

bool config_rede_rapida(rede_rapida_t *r);
void config_salvar_rede_rapida(const rede_rapida_t *r);

#endif
