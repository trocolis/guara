#ifndef GUARA_CLOCK_H
#define GUARA_CLOCK_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

/*
 * Fonte de tempo: DS3231 (UTC) quando presente; senão o RTC interno do ESP32,
 * válido só depois de um ajuste manual ou do NTP. O fuso é uma string POSIX TZ.
 */
void clock_init(const char *timezone);
void clock_set_timezone(const char *timezone);
bool clock_has_ds3231(void);
/* A hora é confiável (DS3231 sem perda de energia, NTP ou ajuste manual). */
bool clock_is_valid(void);
uint32_t clock_now(void);             /* epoch UTC */
bool clock_local_time(struct tm *tm); /* hora local; false se inválida */
int clock_minute_of_day(void);        /* -1 se inválida */
/* Ajuste manual em hora local. */
void clock_set_local(int hour, int minute);
void clock_set_epoch(uint32_t epoch);
/* Sincroniza pelo NTP (exige Wi-Fi conectado). Bloqueia até `timeout_ms`. */
bool clock_sync_ntp(uint32_t timeout_ms);
void clock_format_hhmm(char *buf, int length);
/* Epoch da última sincronização NTP bem-sucedida (0 = nenhuma desde o boot frio). */
uint32_t clock_last_ntp(void);

#endif
