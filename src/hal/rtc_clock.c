/*
 * Relógio do sistema: DS3231 (UTC) quando presente, senão o RTC interno.
 * A hora só é considerada válida após DS3231, NTP ou ajuste manual.
 */
#include "rtc_clock.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "esp_attr.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "i2c.h"
#include "pins.h"

static const char *TAG = "relogio";

static i2c_master_dev_handle_t ds3231;
/* Sobrevive ao deep sleep: o RTC interno continua contando. */
static RTC_DATA_ATTR bool time_valid;
static RTC_DATA_ATTR uint32_t last_ntp;

static uint8_t bcd(uint8_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }
static uint8_t from_bcd(uint8_t v) { return (uint8_t)((v >> 4) * 10 + (v & 0x0F)); }

/* Lê `n` registradores do DS3231 a partir de `reg`. */
static bool ds_read(uint8_t reg, uint8_t *buf, size_t n)
{
    return ds3231 && i2c_master_transmit_receive(ds3231, &reg, 1, buf, n, 100) == ESP_OK;
}

/* Escreve `n` registradores do DS3231 a partir de `reg`. */
static bool ds_write(uint8_t reg, const uint8_t *data, size_t n)
{
    uint8_t buf[8];
    buf[0] = reg;
    memcpy(buf + 1, data, n);
    return ds3231 && i2c_master_transmit(ds3231, buf, n + 1, 100) == ESP_OK;
}

/* Grava a hora UTC no DS3231 e limpa o flag de oscilador parado. */
static void write_ds3231(time_t t)
{
    struct tm u;
    gmtime_r(&t, &u);
    uint8_t r[7] = {
        bcd((uint8_t)u.tm_sec), bcd((uint8_t)u.tm_min), bcd((uint8_t)u.tm_hour), (uint8_t)(u.tm_wday + 1),
        bcd((uint8_t)u.tm_mday), bcd((uint8_t)(u.tm_mon + 1)), bcd((uint8_t)(u.tm_year - 100)),
    };
    if (ds_write(0x00, r, 7)) {
        uint8_t status;
        if (ds_read(0x0F, &status, 1)) {
            status &= (uint8_t)~0x80; /* limpa OSF (oscilador parou) */
            ds_write(0x0F, &status, 1);
        }
    }
}

/* Ajusta o relógio do sistema (epoch UTC). */
static void set_system_time(time_t t)
{
    struct timeval tv = {.tv_sec = t, .tv_usec = 0};
    settimeofday(&tv, NULL);
}

/* Define o fuso horário (string POSIX TZ). */
void clock_set_timezone(const char *timezone)
{
    setenv("TZ", timezone && *timezone ? timezone : "<-03>3", 1);
    tzset();
}

/* Detecta o DS3231 e, se a hora dele for confiável, copia para o sistema. */
void clock_init(const char *timezone)
{
    clock_set_timezone(timezone);
    if (i2c_is_present(ADDRESS_DS3231)) {
        ds3231 = i2c_add_device(ADDRESS_DS3231);
    }
    if (!ds3231) {
        ESP_LOGW(TAG, "DS3231 ausente: usando o RTC interno");
        return;
    }
    uint8_t r[7], status = 0x80;
    ds_read(0x0F, &status, 1);
    if (!ds_read(0x00, r, 7)) {
        return;
    }
    if (status & 0x80) {
        ESP_LOGW(TAG, "DS3231 perdeu a energia: hora inválida até ajustar");
        return;
    }
    struct tm u = {
        .tm_sec = from_bcd(r[0] & 0x7F),
        .tm_min = from_bcd(r[1] & 0x7F),
        .tm_hour = from_bcd(r[2] & 0x3F),
        .tm_mday = from_bcd(r[4] & 0x3F),
        .tm_mon = from_bcd(r[5] & 0x1F) - 1,
        .tm_year = from_bcd(r[6]) + 100,
    };
    /* timegm não existe na newlib: converte como UTC zerando o fuso temporariamente */
    char *tz = getenv("TZ");
    char saved[48];
    snprintf(saved, sizeof(saved), "%s", tz ? tz : "");
    setenv("TZ", "UTC0", 1);
    tzset();
    time_t t = mktime(&u);
    setenv("TZ", saved, 1);
    tzset();
    set_system_time(t);
    time_valid = true;
    ESP_LOGI(TAG, "Hora lida do DS3231");
}

/* Verdadeiro se o DS3231 respondeu no boot. */
bool clock_has_ds3231(void)
{
    return ds3231 != NULL;
}

/* Verdadeiro se a hora é confiável. */
bool clock_is_valid(void)
{
    return time_valid;
}

/* Epoch UTC atual. */
uint32_t clock_now(void)
{
    return (uint32_t)time(NULL);
}

/* Hora local; retorna false se a hora não for confiável. */
bool clock_local_time(struct tm *tm)
{
    time_t t = time(NULL);
    localtime_r(&t, tm);
    return time_valid;
}

/* Minuto do dia (0..1439) na hora local, ou -1 se desconhecido. */
int clock_minute_of_day(void)
{
    struct tm tm;
    if (!clock_local_time(&tm)) {
        return -1;
    }
    return tm.tm_hour * 60 + tm.tm_min;
}

/* Ajusta a hora (epoch UTC) no sistema e no DS3231. */
void clock_set_epoch(uint32_t epoch)
{
    set_system_time((time_t)epoch);
    write_ds3231((time_t)epoch);
    time_valid = true;
}

/* Ajuste manual em hora local (mantém a data, ou usa 01/01/2026 se desconhecida). */
void clock_set_local(int hour, int minute)
{
    struct tm tm;
    time_t t = time(NULL);
    localtime_r(&t, &tm);
    if (!time_valid) {
        tm.tm_year = 126; /* 2026-01-01 se a data for desconhecida */
        tm.tm_mon = 0;
        tm.tm_mday = 1;
    }
    tm.tm_hour = hour;
    tm.tm_min = minute;
    tm.tm_sec = 0;
    tm.tm_isdst = -1;
    clock_set_epoch((uint32_t)mktime(&tm));
}

/* Sincroniza pelo NTP (exige rede) e grava no DS3231. */
bool clock_sync_ntp(uint32_t timeout_ms)
{
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    if (esp_netif_sntp_init(&cfg) != ESP_OK) {
        return false;
    }
    bool ok = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(timeout_ms)) == ESP_OK;
    esp_netif_sntp_deinit();
    if (ok) {
        time_t t = time(NULL);
        write_ds3231(t);
        time_valid = true;
        last_ntp = (uint32_t)t;
        ESP_LOGI(TAG, "Hora sincronizada pelo NTP");
    }
    return ok;
}

/* Formata HH:MM, ou --:-- se a hora não for confiável. */
void clock_format_hhmm(char *buf, int length)
{
    struct tm tm;
    if (clock_local_time(&tm)) {
        snprintf(buf, (size_t)length, "%02d:%02d", tm.tm_hour, tm.tm_min);
    } else {
        snprintf(buf, (size_t)length, "--:--");
    }
}

/* Epoch da última sincronização NTP bem-sucedida. */
uint32_t clock_last_ntp(void)
{
    return last_ntp;
}
