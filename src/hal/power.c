/*
 * Gerenciamento de energia: motivo do despertar e entrada em deep sleep.
 */
#include "power.h"

#include "driver/rtc_io.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "display.h"
#include "pins.h"

static const char *TAG = "energia";

/* Motivo do boot atual: frio, botão ou timer. */
wake_reason_t power_wake_reason(void)
{
    uint32_t causes = esp_sleep_get_wakeup_causes();
    if (causes & (BIT(ESP_SLEEP_WAKEUP_EXT0) | BIT(ESP_SLEEP_WAKEUP_EXT1))) {
        return WOKE_BUTTON;
    }
    if (causes & BIT(ESP_SLEEP_WAKEUP_TIMER)) {
        return WOKE_TIMER;
    }
    return WOKE_COLD;
}

/* Desliga o display, mantém os pull-ups dos botões no domínio RTC e dorme. */
void power_deep_sleep(uint32_t seconds)
{
    ESP_LOGI(TAG, "Deep sleep (timer: %lu s)", (unsigned long)seconds);
    display_set_power(false);

    /* Pull-ups dos botões mantidos pelo domínio RTC durante o sono */
    const gpio_num_t pins[] = {PIN_UP, PIN_DOWN};
    for (int i = 0; i < 2; i++) {
        rtc_gpio_init(pins[i]);
        rtc_gpio_set_direction(pins[i], RTC_GPIO_MODE_INPUT_ONLY);
        rtc_gpio_pullup_en(pins[i]);
        rtc_gpio_pulldown_dis(pins[i]);
    }
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
    esp_sleep_enable_ext0_wakeup(PIN_UP, 0);
    /* No ESP32, ALL_LOW com um único pino equivale a "pino em nível baixo" */
    esp_sleep_enable_ext1_wakeup_io(1ull << PIN_DOWN, ESP_EXT1_WAKEUP_ALL_LOW);
    if (seconds > 0) {
        esp_sleep_enable_timer_wakeup((uint64_t)seconds * 1000000ull);
    }
    esp_deep_sleep_start();
}
