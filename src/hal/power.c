/*
 * Gerenciamento de energia: motivo do despertar, modo Ocioso (frequência
 * dinâmica e light sleep automático) e entrada em deep sleep com despertar
 * pelos botões Cima, Baixo, A e B, conferidos pelo coprocessador ULP.
 */
#include "power.h"

#include "driver/rtc_io.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_sleep.h"
#include "soc/rtc_io_reg.h"
#include "ulp.h"
#include "display.h"
#include "pins.h"

static const char *TAG = "energia";

#define CPU_MAX_MHZ 160
#define CPU_MIN_MHZ 80   /* mantém o APB em 80 MHz: UART, LEDC e I2C sem mudar de clock */
#define ULP_PERIOD_US 20000 /* o ULP confere os botões a cada 20 ms durante o deep sleep */

static esp_pm_lock_handle_t lock_cpu, lock_no_sleep;
static bool idle;

/* Configura o gerenciamento de energia; começa no modo Ativo (travas seguradas). */
void power_init(void)
{
    /* O ULP continua rodando depois de acordar o chip: só serve durante o sono */
    ulp_timer_stop();

    esp_pm_config_t config = {
        .max_freq_mhz = CPU_MAX_MHZ,
        .min_freq_mhz = CPU_MIN_MHZ,
        .light_sleep_enable = true,
    };
    esp_err_t err = esp_pm_configure(&config);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Gerenciamento de energia indisponível (%s)", esp_err_to_name(err));
        return;
    }
    esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "ativo", &lock_cpu);
    esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "ativo", &lock_no_sleep);
    if (lock_cpu) {
        esp_pm_lock_acquire(lock_cpu);
    }
    if (lock_no_sleep) {
        esp_pm_lock_acquire(lock_no_sleep);
    }
}

/* Entra ou sai do modo Ocioso: brilho mínimo e travas de CPU e light sleep liberadas. */
void power_set_idle(bool enabled)
{
    if (enabled == idle) {
        return;
    }
    idle = enabled;
    display_contrast(enabled ? 0x01 : 0xCF);
    if (lock_cpu && lock_no_sleep) {
        if (enabled) {
            esp_pm_lock_release(lock_cpu);
            esp_pm_lock_release(lock_no_sleep);
        } else {
            esp_pm_lock_acquire(lock_cpu);
            esp_pm_lock_acquire(lock_no_sleep);
        }
    }
    ESP_LOGI(TAG, "%s", enabled ? "Ocioso" : "Ativo");
}

/* Verdadeiro no modo Ocioso. */
bool power_is_idle(void)
{
    return idle;
}

/* Motivo do boot atual: frio, botão ou timer. */
wake_reason_t power_wake_reason(void)
{
    uint32_t causes = esp_sleep_get_wakeup_causes();
    if (causes & (BIT(ESP_SLEEP_WAKEUP_EXT0) | BIT(ESP_SLEEP_WAKEUP_ULP))) {
        return WOKE_BUTTON;
    }
    if (causes & BIT(ESP_SLEEP_WAKEUP_TIMER)) {
        return WOKE_TIMER;
    }
    return WOKE_COLD;
}

/*
 * Programa do ULP: lê os botões no RTC_GPIO_IN e acorda o chip se algum
 * estiver em nível baixo (apertado); senão, para até o próximo período.
 * No ESP32 o ULP não pode ser usado junto com o ext0, e o ext1 só acorda com
 * "todos os pinos em nível baixo", então o ULP cuida de todos os botões.
 */
static bool start_ulp_buttons(const gpio_num_t *pins)
{
    int bit[4];
    for (int i = 0; i < 4; i++) {
        bit[i] = RTC_GPIO_IN_NEXT_S + rtc_io_number_get(pins[i]);
    }
    const ulp_insn_t program[] = {
        I_RD_REG(RTC_GPIO_IN_REG, bit[0], bit[0]), /* R0 = nível do botão */
        M_BL(1, 1),                                /* R0 < 1: apertado */
        I_RD_REG(RTC_GPIO_IN_REG, bit[1], bit[1]),
        M_BL(1, 1),
        I_RD_REG(RTC_GPIO_IN_REG, bit[2], bit[2]),
        M_BL(1, 1),
        I_RD_REG(RTC_GPIO_IN_REG, bit[3], bit[3]),
        M_BL(1, 1),
        I_HALT(),
        M_LABEL(1),
        I_WAKE(),
        I_HALT(),
    };
    size_t size = sizeof(program) / sizeof(ulp_insn_t);
    esp_err_t err = ulp_process_macros_and_load(0, program, &size);
    if (err == ESP_OK) {
        err = ulp_set_wakeup_period(0, ULP_PERIOD_US);
    }
    if (err == ESP_OK) {
        err = esp_sleep_enable_ulp_wakeup();
    }
    if (err == ESP_OK) {
        err = ulp_run(0);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ULP: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

/* Desliga o display, mantém os pull-ups dos botões no domínio RTC e dorme. */
void power_deep_sleep(uint32_t seconds)
{
    ESP_LOGI(TAG, "Deep sleep (timer: %lu s)", (unsigned long)seconds);
    display_shutdown();

    /* Pull-ups dos botões mantidos pelo domínio RTC durante o sono */
    const gpio_num_t pins[] = {PIN_UP, PIN_DOWN, PIN_A, PIN_B};
    for (int i = 0; i < 4; i++) {
        rtc_gpio_init(pins[i]);
        rtc_gpio_set_direction(pins[i], RTC_GPIO_MODE_INPUT_ONLY);
        rtc_gpio_pullup_en(pins[i]);
        rtc_gpio_pulldown_dis(pins[i]);
    }
    /* O light sleep automático deixa fontes ligadas (timer, UART): começa do zero */
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
    if (!start_ulp_buttons(pins)) {
        /* Sem ULP, ao menos o Cima acorda (ext0) */
        esp_sleep_enable_ext0_wakeup(PIN_UP, 0);
    }
    if (seconds > 0) {
        esp_sleep_enable_timer_wakeup((uint64_t)seconds * 1000000ull);
    }
    esp_deep_sleep_start();
}
