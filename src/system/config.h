#ifndef GUARA_CONFIG_H
#define GUARA_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

/* Configurações do system, persistidas na NVS (namespace "guara"). */
typedef struct {
    bool mute;
    uint8_t volume;          /* 1..3 */
    uint8_t power_save;      /* índice em CONFIG_POWER_SAVE_MIN */
    uint16_t bedtime_minute; /* hora de dormir do Guará (minuto do dia) */
    uint16_t wake_minute;
    char timezone[32];         /* POSIX TZ */
    bool wifi_on;
} config_t;

#define CONFIG_COUNT_POWER_SAVE 5
extern const uint8_t CONFIG_POWER_SAVE_MIN[CONFIG_COUNT_POWER_SAVE]; /* 0 = desativado */

#define CONFIG_MAX_NETWORKS 5
typedef struct {
    char ssid[33];
    char password[65];
} saved_network_t;

extern config_t g_cfg;

void config_load(void);
void config_save(void);

int config_networks(saved_network_t *networks);
void config_save_network(const char *ssid, const char *password);
void config_forget_network(int position);

/* Último roteador conectado: permite reconectar sem busca (~1-2 s). */
typedef struct {
    char ssid[33];
    uint8_t bssid[6];
    uint8_t channel;
} fast_reconnect_t;

bool config_load_fast_reconnect(fast_reconnect_t *r);
void config_save_fast_reconnect(const fast_reconnect_t *r);

#endif
