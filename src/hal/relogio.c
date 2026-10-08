#include "relogio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "esp_attr.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "i2c.h"
#include "pinos.h"

static const char *TAG = "relogio";

static i2c_master_dev_handle_t ds3231;
/* Sobrevive ao deep sleep: o RTC interno continua contando. */
static RTC_DATA_ATTR bool hora_valida;
static RTC_DATA_ATTR uint32_t ultimo_ntp;

static uint8_t bcd(uint8_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }
static uint8_t de_bcd(uint8_t v) { return (uint8_t)((v >> 4) * 10 + (v & 0x0F)); }

static bool ds_ler(uint8_t reg, uint8_t *buf, size_t n)
{
    return ds3231 && i2c_master_transmit_receive(ds3231, &reg, 1, buf, n, 100) == ESP_OK;
}

static bool ds_escrever(uint8_t reg, const uint8_t *dados, size_t n)
{
    uint8_t buf[8];
    buf[0] = reg;
    memcpy(buf + 1, dados, n);
    return ds3231 && i2c_master_transmit(ds3231, buf, n + 1, 100) == ESP_OK;
}

static void gravar_ds3231(time_t t)
{
    struct tm u;
    gmtime_r(&t, &u);
    uint8_t r[7] = {
        bcd((uint8_t)u.tm_sec), bcd((uint8_t)u.tm_min), bcd((uint8_t)u.tm_hour), (uint8_t)(u.tm_wday + 1),
        bcd((uint8_t)u.tm_mday), bcd((uint8_t)(u.tm_mon + 1)), bcd((uint8_t)(u.tm_year - 100)),
    };
    if (ds_escrever(0x00, r, 7)) {
        uint8_t status;
        if (ds_ler(0x0F, &status, 1)) {
            status &= (uint8_t)~0x80; /* limpa OSF (oscilador parou) */
            ds_escrever(0x0F, &status, 1);
        }
    }
}

static void definir_sistema(time_t t)
{
    struct timeval tv = {.tv_sec = t, .tv_usec = 0};
    settimeofday(&tv, NULL);
}

void relogio_definir_fuso(const char *fuso)
{
    setenv("TZ", fuso && *fuso ? fuso : "<-03>3", 1);
    tzset();
}

void relogio_iniciar(const char *fuso)
{
    relogio_definir_fuso(fuso);
    if (i2c_presente(ENDERECO_DS3231)) {
        ds3231 = i2c_dispositivo(ENDERECO_DS3231);
    }
    if (!ds3231) {
        ESP_LOGW(TAG, "DS3231 ausente: usando o RTC interno");
        return;
    }
    uint8_t r[7], status = 0x80;
    ds_ler(0x0F, &status, 1);
    if (!ds_ler(0x00, r, 7)) {
        return;
    }
    if (status & 0x80) {
        ESP_LOGW(TAG, "DS3231 perdeu a energia: hora inválida até ajustar");
        return;
    }
    struct tm u = {
        .tm_sec = de_bcd(r[0] & 0x7F),
        .tm_min = de_bcd(r[1] & 0x7F),
        .tm_hour = de_bcd(r[2] & 0x3F),
        .tm_mday = de_bcd(r[4] & 0x3F),
        .tm_mon = de_bcd(r[5] & 0x1F) - 1,
        .tm_year = de_bcd(r[6]) + 100,
    };
    /* timegm não existe na newlib: converte como UTC zerando o fuso temporariamente */
    char *tz = getenv("TZ");
    char salvo[48];
    snprintf(salvo, sizeof(salvo), "%s", tz ? tz : "");
    setenv("TZ", "UTC0", 1);
    tzset();
    time_t t = mktime(&u);
    setenv("TZ", salvo, 1);
    tzset();
    definir_sistema(t);
    hora_valida = true;
    ESP_LOGI(TAG, "Hora lida do DS3231");
}

bool relogio_tem_ds3231(void)
{
    return ds3231 != NULL;
}

bool relogio_valido(void)
{
    return hora_valida;
}

uint32_t relogio_agora(void)
{
    return (uint32_t)time(NULL);
}

bool relogio_local(struct tm *tm)
{
    time_t t = time(NULL);
    localtime_r(&t, tm);
    return hora_valida;
}

int relogio_minuto_do_dia(void)
{
    struct tm tm;
    if (!relogio_local(&tm)) {
        return -1;
    }
    return tm.tm_hour * 60 + tm.tm_min;
}

void relogio_ajustar_epoch(uint32_t epoch)
{
    definir_sistema((time_t)epoch);
    gravar_ds3231((time_t)epoch);
    hora_valida = true;
}

void relogio_ajustar_local(int hora, int minuto)
{
    struct tm tm;
    time_t t = time(NULL);
    localtime_r(&t, &tm);
    if (!hora_valida) {
        tm.tm_year = 126; /* 2026-01-01 se a data for desconhecida */
        tm.tm_mon = 0;
        tm.tm_mday = 1;
    }
    tm.tm_hour = hora;
    tm.tm_min = minuto;
    tm.tm_sec = 0;
    tm.tm_isdst = -1;
    relogio_ajustar_epoch((uint32_t)mktime(&tm));
}

bool relogio_sincronizar_ntp(uint32_t espera_ms)
{
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    if (esp_netif_sntp_init(&cfg) != ESP_OK) {
        return false;
    }
    bool ok = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(espera_ms)) == ESP_OK;
    esp_netif_sntp_deinit();
    if (ok) {
        time_t t = time(NULL);
        gravar_ds3231(t);
        hora_valida = true;
        ultimo_ntp = (uint32_t)t;
        ESP_LOGI(TAG, "Hora sincronizada pelo NTP");
    }
    return ok;
}

void relogio_formatar_hhmm(char *buf, int tam)
{
    struct tm tm;
    if (relogio_local(&tm)) {
        snprintf(buf, (size_t)tam, "%02d:%02d", tm.tm_hour, tm.tm_min);
    } else {
        snprintf(buf, (size_t)tam, "--:--");
    }
}

uint32_t relogio_ultimo_ntp(void)
{
    return ultimo_ntp;
}
