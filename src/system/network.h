#ifndef GUARA_NETWORK_H
#define GUARA_NETWORK_H

#include <stdbool.h>
#include <stdint.h>

#include "wifi.h"

/*
 * Wi-Fi sob demanda (README > Conectividade: Wi-Fi sob Demanda).
 * Com o Wi-Fi em ON a rede é apenas permitida: o rádio só liga enquanto
 * alguém usa a rede (contador de uso) e desliga após NETWORK_IDLE_MS sem uso.
 */
#define NETWORK_IDLE_MS (30 * 1000)
#define NETWORK_NTP_INTERVAL_S (6 * 60 * 60)
/* Intervalo da verificação automática de nova release no GitHub. */
#define NETWORK_UPDATE_INTERVAL_S (24 * 60 * 60)

typedef enum {
    NETWORK_DISABLED, /* Wi-Fi em OFF */
    NETWORK_IDLE,     /* permitida, rádio desligado */
    NETWORK_CONNECTING,
    NETWORK_CONNECTED,
} network_state_t;

void network_init(void);
bool network_allowed(void);
network_state_t network_state(void);
/* Barras de sinal (0..3) em cache, seguro para a interface. */
int network_bars(void);

/* Uso síncrono (bloqueia quem chama): network_use(); if (network_wait(ms)) {...} network_release(); */
void network_use(void);
bool network_wait(uint32_t timeout_ms);
void network_release(void);

/* Tarefa em segundo plano: conecta, chama fn(conectado, ctx) e libera. */
typedef void (*network_job_fn)(bool connected, void *ctx);
bool network_run(network_job_fn fn, void *ctx);

/* Operações do menu Wi-Fi (ligam o rádio sob demanda). */
int network_scan(wifi_network_t *networks, int max);
bool network_connect_new(const char *ssid, const char *password);
/* Texto de status para Ajustes > Wi-Fi > Status. */
void network_status(char *buf, int length);

/* Release mais nova que o firmware achada pela verificação automática (NULL = nenhuma). */
const char *network_new_release(void);
/* Descarta o aviso da release achada (já foi oferecida ao usuário). */
void network_forget_release(void);

/* Desliga o rádio agora (deep sleep, Wi-Fi em OFF). */
void network_turn_off(void);
/* Bloqueia o uso da rede (jogos Lua precisam da memória do driver). */
void network_suspend(bool suspend);

#endif
