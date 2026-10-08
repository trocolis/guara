#ifndef GUARA_ALARMS_H
#define GUARA_ALARMS_H

#include <stdbool.h>
#include <stdint.h>

/* Despertadores (gravados no cartão SD) e temporizador (RTC RAM). */
#define ALARMS_MAX 8
#define ALARMS_FILE "/sdcard/guara/alarms.txt"
/* Nome usado até a v0.2.0; é renomeado para ALARMS_FILE na primeira leitura */
#define LEGACY_ALARMS_FILE "/sdcard/guara/alarmes.txt"

void alarms_load(void);
int alarms_count(void);
int alarms_minute_at(int position);
/* Retorna false se não houver cartão SD ou a lista estiver cheia. */
bool alarms_add(int minute_of_day);
bool alarms_edit(int position, int minute_of_day);
bool alarms_remove(int position);
/* Verdadeiro uma vez quando algum despertador bate neste minuto. */
bool alarms_check(int minute_of_day);
/* Segundos até o próximo despertador (0 se nenhum ou hora desconhecida). */
uint32_t alarms_seconds_until_next(void);

/* Temporizador */
void timer_start(uint32_t seconds);
void timer_stop(void);
bool timer_active(void);
uint32_t timer_remaining(void);
uint32_t timer_duration(void);
/* Verdadeiro uma vez quando o temporizador termina. */
bool timer_check(void);

#endif
