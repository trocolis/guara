#include "bateria.h"

#include "driver/gpio.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "pinos.h"

static const char *TAG = "bateria";

#define AMOSTRAS 16

static adc_oneshot_unit_handle_t adc;
static adc_cali_handle_t cali;
static bool calibrado;
static int media_mv = -1;

/* Curva de descarga LiPo (mV, %): 3,27 V = 0%, 4,20 V = 100% (README). */
static const int CURVA[][2] = {
    {3270, 0}, {3300, 2}, {3400, 6}, {3450, 10}, {3500, 13}, {3600, 22},
    {3700, 38}, {3800, 54}, {3900, 68}, {4000, 80}, {4100, 91}, {4200, 100},
};

void bateria_iniciar(void)
{
    adc_oneshot_unit_init_cfg_t unidade = {.unit_id = ADC_UNIT_1};
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unidade, &adc));
    adc_oneshot_chan_cfg_t canal = {.atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT};
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc, ADC_CHANNEL_6, &canal));

    adc_cali_line_fitting_config_t cfg = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    calibrado = adc_cali_create_scheme_line_fitting(&cfg, &cali) == ESP_OK;
    if (!calibrado) {
        ESP_LOGW(TAG, "Sem calibração de fábrica do ADC; leitura aproximada");
    }

    gpio_config_t chrg = {
        .pin_bit_mask = 1ull << PINO_CHRG,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE, /* GPIO35 não tem pull-up: resistor externo */
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&chrg);
    bateria_atualizar();
}

void bateria_atualizar(void)
{
    int soma = 0, n = 0;
    for (int i = 0; i < AMOSTRAS; i++) {
        int bruto, mv;
        if (adc_oneshot_read(adc, ADC_CHANNEL_6, &bruto) != ESP_OK) {
            continue;
        }
        if (calibrado && adc_cali_raw_to_voltage(cali, bruto, &mv) == ESP_OK) {
            soma += mv;
        } else {
            soma += bruto * 3100 / 4095;
        }
        n++;
    }
    if (n == 0) {
        return;
    }
    int mv = soma / n * 2; /* divisor 100k/100k */
    media_mv = media_mv < 0 ? mv : (media_mv * 3 + mv) / 4;
}

int bateria_mv(void)
{
    return media_mv;
}

int bateria_porcento(void)
{
    int mv = media_mv;
    const int n = sizeof(CURVA) / sizeof(CURVA[0]);
    if (mv <= CURVA[0][0]) {
        return 0;
    }
    if (mv >= CURVA[n - 1][0]) {
        return 100;
    }
    for (int i = 1; i < n; i++) {
        if (mv < CURVA[i][0]) {
            int dv = CURVA[i][0] - CURVA[i - 1][0];
            int dp = CURVA[i][1] - CURVA[i - 1][1];
            return CURVA[i - 1][1] + (mv - CURVA[i - 1][0]) * dp / dv;
        }
    }
    return 100;
}

bool bateria_carregando(void)
{
    return gpio_get_level(PINO_CHRG) == 0;
}

bool bateria_presente(void)
{
    return media_mv > 2800 && media_mv < 4500;
}

bool bateria_critica(void)
{
    return bateria_presente() && !bateria_carregando() && bateria_porcento() < 10;
}
