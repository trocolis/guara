#ifndef GUARA_RELOGIO_H
#define GUARA_RELOGIO_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

/*
 * Fonte de tempo: DS3231 (UTC) quando presente; senão o RTC interno do ESP32,
 * válido só depois de um ajuste manual ou do NTP. O fuso é uma string POSIX TZ.
 */
void relogio_iniciar(const char *fuso);
void relogio_definir_fuso(const char *fuso);
bool relogio_tem_ds3231(void);
/* A hora é confiável (DS3231 sem perda de energia, NTP ou ajuste manual). */
bool relogio_valido(void);
uint32_t relogio_agora(void);       /* epoch UTC */
bool relogio_local(struct tm *tm);  /* hora local; false se inválida */
int relogio_minuto_do_dia(void);    /* -1 se inválida */
/* Ajuste manual em hora local. */
void relogio_ajustar_local(int hora, int minuto);
void relogio_ajustar_epoch(uint32_t epoch);
/* Sincroniza pelo NTP (exige Wi-Fi conectado). Bloqueia até `espera_ms`. */
bool relogio_sincronizar_ntp(uint32_t espera_ms);
void relogio_formatar_hhmm(char *buf, int tam);
/* Epoch da última sincronização NTP bem-sucedida (0 = nenhuma desde o boot frio). */
uint32_t relogio_ultimo_ntp(void);

#endif
