/*
 * Bateria LiPo: tensão pelo divisor 100k/100k no ADC1_CH6, curva de descarga
 * e detecção de carga pelo pino CHRG do TP4056.
 */
#include "battery.h"

#include "driver/gpio.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "pins.h"

static const char *TAG = "bateria";

#define SAMPLES 16

static adc_oneshot_unit_handle_t adc;
static adc_cali_handle_t cali;
static bool calibrated;
static int average_mv = -1;

/* Curva de descarga LiPo (mV, %): 3,27 V = 0%, 4,20 V = 100% (README). */
static const int CURVE[][2] = {
    {3270, 0}, {3300, 2}, {3400, 6}, {3450, 10}, {3500, 13}, {3600, 22},
    {3700, 38}, {3800, 54}, {3900, 68}, {4000, 80}, {4100, 91}, {4200, 100},
};

/* Configura o ADC (com calibração de fábrica, se houver) e o pino CHRG. */
void battery_init(void)
{
    adc_oneshot_unit_init_cfg_t unit = {.unit_id = ADC_UNIT_1};
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit, &adc));
    adc_oneshot_chan_cfg_t channel = {.atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT};
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc, ADC_CHANNEL_6, &channel));

    adc_cali_line_fitting_config_t cfg = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    calibrated = adc_cali_create_scheme_line_fitting(&cfg, &cali) == ESP_OK;
    if (!calibrated) {
        ESP_LOGW(TAG, "Sem calibração de fábrica do ADC; leitura aproximada");
    }

    gpio_config_t chrg = {
        .pin_bit_mask = 1ull << PIN_CHRG,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE, /* GPIO35 não tem pull-up: resistor externo */
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&chrg);
    battery_update();
}

/* Lê 16 amostras e atualiza a média móvel da tensão. */
void battery_update(void)
{
    int sum = 0, n = 0;
    for (int i = 0; i < SAMPLES; i++) {
        int raw, mv;
        if (adc_oneshot_read(adc, ADC_CHANNEL_6, &raw) != ESP_OK) {
            continue;
        }
        if (calibrated && adc_cali_raw_to_voltage(cali, raw, &mv) == ESP_OK) {
            sum += mv;
        } else {
            sum += raw * 3100 / 4095;
        }
        n++;
    }
    if (n == 0) {
        return;
    }
    int mv = sum / n * 2; /* divisor 100k/100k */
    average_mv = average_mv < 0 ? mv : (average_mv * 3 + mv) / 4;
}

/* Tensão da bateria em mV (já compensando o divisor). */
int battery_mv(void)
{
    return average_mv;
}

/* Carga (%) interpolada na curva de descarga LiPo. */
int battery_percent(void)
{
    int mv = average_mv;
    const int n = sizeof(CURVE) / sizeof(CURVE[0]);
    if (mv <= CURVE[0][0]) {
        return 0;
    }
    if (mv >= CURVE[n - 1][0]) {
        return 100;
    }
    for (int i = 1; i < n; i++) {
        if (mv < CURVE[i][0]) {
            int dv = CURVE[i][0] - CURVE[i - 1][0];
            int dp = CURVE[i][1] - CURVE[i - 1][1];
            return CURVE[i - 1][1] + (mv - CURVE[i - 1][0]) * dp / dv;
        }
    }
    return 100;
}

/* Verdadeiro enquanto o TP4056 indica carga (CHRG em nível baixo). */
bool battery_charging(void)
{
    return gpio_get_level(PIN_CHRG) == 0;
}

/* Verdadeiro se a tensão lida é plausível para uma bateria conectada. */
bool battery_present(void)
{
    return average_mv > 2800 && average_mv < 4500;
}

/* Bateria abaixo de 10% e sem carregar: dispara o salvamento de emergência. */
bool battery_critical(void)
{
    return battery_present() && !battery_charging() && battery_percent() < 10;
}
