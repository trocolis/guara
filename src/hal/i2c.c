/*
 * Barramento I2C único, compartilhado pelo display e pelo DS3231.
 */
#include "i2c.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "pins.h"

static const char *TAG = "i2c";
static i2c_master_bus_handle_t bus_handle;

/*
 * Destrava o barramento: se um dispositivo ficou segurando o SDA em nível
 * baixo (transmissão cortada por um reset ou deep sleep), até 9 pulsos no SCL
 * terminam o byte dele, e uma condição de STOP libera o barramento.
 */
static void clear_bus(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ull << PIN_SDA) | (1ull << PIN_SCL),
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&cfg);
    gpio_set_level(PIN_SDA, 1);
    gpio_set_level(PIN_SCL, 1);
    esp_rom_delay_us(10);
    if (gpio_get_level(PIN_SDA) == 0) {
        ESP_LOGW(TAG, "SDA preso em nível baixo: destravando o barramento");
        for (int i = 0; i < 9 && gpio_get_level(PIN_SDA) == 0; i++) {
            gpio_set_level(PIN_SCL, 0);
            esp_rom_delay_us(10);
            gpio_set_level(PIN_SCL, 1);
            esp_rom_delay_us(10);
        }
        /* STOP: SDA sobe com o SCL em nível alto */
        gpio_set_level(PIN_SCL, 0);
        gpio_set_level(PIN_SDA, 0);
        esp_rom_delay_us(10);
        gpio_set_level(PIN_SCL, 1);
        esp_rom_delay_us(10);
        gpio_set_level(PIN_SDA, 1);
        esp_rom_delay_us(10);
    }
    gpio_reset_pin(PIN_SDA);
    gpio_reset_pin(PIN_SCL);
}

/* Cria o barramento na primeira chamada e devolve o handle. */
i2c_master_bus_handle_t i2c_bus(void)
{
    if (bus_handle == NULL) {
        clear_bus();
        i2c_master_bus_config_t cfg = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = PIN_SDA,
            .scl_io_num = PIN_SCL,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .flags.enable_internal_pullup = true,
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&cfg, &bus_handle));
    }
    return bus_handle;
}

/* Registra um dispositivo no barramento. */
i2c_master_dev_handle_t i2c_add_device(uint8_t address)
{
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = I2C_FREQ_HZ,
    };
    i2c_master_dev_handle_t dev = NULL;
    if (i2c_master_bus_add_device(i2c_bus(), &cfg, &dev) != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao registrar o dispositivo 0x%02X", address);
    }
    return dev;
}

/* Verdadeiro se o endereço responde (ACK). */
bool i2c_is_present(uint8_t address)
{
    return i2c_master_probe(i2c_bus(), address, 50) == ESP_OK;
}
