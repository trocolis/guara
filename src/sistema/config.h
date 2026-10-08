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

#endif
