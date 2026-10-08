/*
 * Barramento I2C único, compartilhado pelo display e pelo DS3231.
 */
#include "i2c.h"

#include "esp_log.h"
#include "pins.h"

static const char *TAG = "i2c";
static i2c_master_bus_handle_t bus_handle;

/* Cria o barramento na primeira chamada e devolve o handle. */
i2c_master_bus_handle_t i2c_bus(void)
{
    if (bus_handle == NULL) {
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
