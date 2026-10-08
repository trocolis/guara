#include "energia.h"

#include "driver/rtc_io.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "display.h"
#include "pinos.h"

static const char *TAG = "energia";

motivo_acordar_t energia_motivo(void)
{
    uint32_t causas = esp_sleep_get_wakeup_causes();
    if (causas & (BIT(ESP_SLEEP_WAKEUP_EXT0) | BIT(ESP_SLEEP_WAKEUP_EXT1))) {
        return ACORDOU_BOTAO;
    }
    if (causas & BIT(ESP_SLEEP_WAKEUP_TIMER)) {
        return ACORDOU_TIMER;
    }
    return ACORDOU_FRIO;
}

void energia_dormir(uint32_t segundos)
{
    ESP_LOGI(TAG, "Deep sleep (timer: %lu s)", (unsigned long)segundos);
    display_ligar(false);

    /* Pull-ups dos botões mantidos pelo domínio RTC durante o sono */
    const gpio_num_t pinos[] = {PINO_CIMA, PINO_BAIXO};
    for (int i = 0; i < 2; i++) {
        rtc_gpio_init(pinos[i]);
        rtc_gpio_set_direction(pinos[i], RTC_GPIO_MODE_INPUT_ONLY);
        rtc_gpio_pullup_en(pinos[i]);
        rtc_gpio_pulldown_dis(pinos[i]);
    }
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
    esp_sleep_enable_ext0_wakeup(PINO_CIMA, 0);
    /* No ESP32, ALL_LOW com um único pino equivale a "pino em nível baixo" */
    esp_sleep_enable_ext1_wakeup_io(1ull << PINO_BAIXO, ESP_EXT1_WAKEUP_ALL_LOW);
    if (segundos > 0) {
        esp_sleep_enable_timer_wakeup((uint64_t)segundos * 1000000ull);
    }
    esp_deep_sleep_start();
}
