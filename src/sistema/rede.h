#ifndef GUARA_REDE_H
#define GUARA_REDE_H

#include <stdbool.h>
#include <stdint.h>

#include "wifi.h"

/*
 * Wi-Fi sob demanda (README > Conectividade: Wi-Fi sob Demanda).
 * Com o Wi-Fi em ON a rede é apenas permitida: o rádio só liga enquanto
 * alguém usa a rede (contador de uso) e desliga após REDE_OCIOSO_MS sem uso.
 */
#define REDE_OCIOSO_MS (30 * 1000)
#define REDE_NTP_INTERVALO_S (24 * 60 * 60)

typedef enum {
    REDE_DESATIVADA,  /* Wi-Fi em OFF */
    REDE_OCIOSA,      /* permitida, rádio desligado */
    REDE_CONECTANDO,
    REDE_CONECTADA,
} estado_rede_t;

void rede_iniciar(void);
bool rede_permitida(void);
estado_rede_t rede_estado(void);
/* Barras de sinal (0..3) em cache, seguro para a interface. */
int rede_barras(void);

/* Uso síncrono (bloqueia quem chama): rede_usar(); if (rede_aguardar(ms)) {...} rede_liberar(); */
void rede_usar(void);
bool rede_aguardar(uint32_t espera_ms);
void rede_liberar(void);

/* Tarefa em segundo plano: conecta, chama fn(conectado, ctx) e libera. */
typedef void (*rede_tarefa_fn)(bool conectado, void *ctx);
bool rede_executar(rede_tarefa_fn fn, void *ctx);

/* Operações do menu Wi-Fi (ligam o rádio sob demanda). */
int rede_buscar(rede_wifi_t *redes, int max);
bool rede_conectar_nova(const char *ssid, const char *senha);
/* Texto de status para Ajustes > Wi-Fi > Status. */
void rede_status(char *buf, int tam);

/* Desliga o rádio agora (deep sleep, Wi-Fi em OFF). */
void rede_desligar(void);
/* Bloqueia o uso da rede (jogos Lua precisam da memória do driver). */
void rede_suspender(bool suspender);

#endif
