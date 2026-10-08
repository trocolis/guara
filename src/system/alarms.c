/*
 * Despertadores (gravados no cartão SD) e temporizador (na RTC RAM, sobrevive
 * ao deep sleep).
 */
#include "alarms.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "esp_attr.h"
#include "rtc_clock.h"
#include "sdcard.h"

static int list[ALARMS_MAX];
static int count;
static int last_trigger = -1;

static RTC_DATA_ATTR uint32_t timer_end;
static RTC_DATA_ATTR uint32_t timer_total;

/* Ordenação crescente de minutos do dia. */
static int compare(const void *a, const void *b)
{
    return *(const int *)a - *(const int *)b;
}

/* Grava a lista de despertadores no cartão (um HH:MM por linha). */
static bool write_to_disk(void)
{
    if (!sd_mounted() || !sd_ensure_folder(SD_ROOT "/guara")) {
        return false;
    }
    FILE *f = fopen(ALARMS_FILE, "w");
    if (!f) {
        return false;
    }
    for (int i = 0; i < count; i++) {
        fprintf(f, "%02d:%02d\n", list[i] / 60, list[i] % 60);
    }
    fclose(f);
    return true;
}

/* Lê os despertadores do cartão (migrando o arquivo antigo, se houver). */
void alarms_load(void)
{
    count = 0;
    /* Migração: versões antigas gravavam os despertadores em alarmes.txt */
    struct stat info;
    if (sd_mounted() && stat(ALARMS_FILE, &info) != 0 && stat(LEGACY_ALARMS_FILE, &info) == 0) {
        rename(LEGACY_ALARMS_FILE, ALARMS_FILE);
    }
    FILE *f = sd_mounted() ? fopen(ALARMS_FILE, "r") : NULL;
    if (!f) {
        return;
    }
    int h, m;
    while (count < ALARMS_MAX && fscanf(f, "%d:%d", &h, &m) == 2) {
        if (h >= 0 && h < 24 && m >= 0 && m < 60) {
            list[count++] = h * 60 + m;
        }
    }
    fclose(f);
    qsort(list, (size_t)count, sizeof(int), compare);
}

int alarms_count(void) { return count; }
int alarms_minute_at(int i) { return i >= 0 && i < count ? list[i] : 0; }

/* Adiciona um despertador e grava no cartão. */
bool alarms_add(int minute)
{
    if (count >= ALARMS_MAX || !sd_mounted()) {
        return false;
    }
    list[count++] = minute;
    qsort(list, (size_t)count, sizeof(int), compare);
    return write_to_disk();
}

/* Altera um despertador e grava no cartão. */
bool alarms_edit(int i, int minute)
{
    if (i < 0 || i >= count) {
        return false;
    }
    list[i] = minute;
    qsort(list, (size_t)count, sizeof(int), compare);
    return write_to_disk();
}

/* Remove um despertador e grava no cartão. */
bool alarms_remove(int i)
{
    if (i < 0 || i >= count) {
        return false;
    }
    for (int j = i; j < count - 1; j++) {
        list[j] = list[j + 1];
    }
    count--;
    return write_to_disk();
}

/* Verdadeiro uma única vez quando algum despertador bate no minuto atual. */
bool alarms_check(int minute)
{
    if (minute < 0 || minute == last_trigger) {
        return false;
    }
    for (int i = 0; i < count; i++) {
        if (list[i] == minute) {
            last_trigger = minute;
            return true;
        }
    }
    if (last_trigger != minute) {
        last_trigger = -1;
    }
    return false;
}

/* Segundos até o próximo despertador (para o timer do deep sleep). */
uint32_t alarms_seconds_until_next(void)
{
    struct tm tm;
    if (count == 0 || !clock_local_time(&tm)) {
        return 0;
    }
    int now = tm.tm_hour * 60 + tm.tm_min;
    int best = 24 * 60;
    for (int i = 0; i < count; i++) {
        int d = (list[i] - now + 1440) % 1440;
        if (d == 0) {
            d = 1440;
        }
        if (d < best) {
            best = d;
        }
    }
    return (uint32_t)(best * 60 - tm.tm_sec);
}

/* Inicia o temporizador com a duração em segundos. */
void timer_start(uint32_t seconds)
{
    timer_total = seconds;
    timer_end = clock_now() + seconds;
}

/* Cancela o temporizador. */
void timer_stop(void)
{
    timer_end = 0;
}

/* Verdadeiro se o temporizador está contando. */
bool timer_active(void)
{
    return timer_end != 0;
}

/* Segundos restantes. */
uint32_t timer_remaining(void)
{
    if (!timer_end) {
        return 0;
    }
    uint32_t now = clock_now();
    return now >= timer_end ? 0 : timer_end - now;
}

/* Duração configurada na última partida. */
uint32_t timer_duration(void)
{
    return timer_total;
}

/* Verdadeiro uma única vez quando o temporizador termina. */
bool timer_check(void)
{
    if (timer_end && clock_now() >= timer_end) {
        timer_end = 0;
        return true;
    }
    return false;
}
