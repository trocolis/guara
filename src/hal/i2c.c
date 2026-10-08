#include "i2c.h"

#include "esp_log.h"
#include "pinos.h"

static const char *TAG = "i2c";
static i2c_master_bus_handle_t barramento;

i2c_master_bus_handle_t i2c_barramento(void)
{
    if (barramento == NULL) {
        i2c_master_bus_config_t cfg = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = PINO_SDA,
            .scl_io_num = PINO_SCL,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .flags.enable_internal_pullup = true,
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&cfg, &barramento));
    }
    return barramento;
}

i2c_master_dev_handle_t i2c_dispositivo(uint8_t endereco)
{
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = endereco,
        .scl_speed_hz = I2C_FREQ_HZ,
    };
    i2c_master_dev_handle_t dev = NULL;
    if (i2c_master_bus_add_device(i2c_barramento(), &cfg, &dev) != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao registrar o dispositivo 0x%02X", endereco);
    }
    return dev;
}

bool i2c_presente(uint8_t endereco)
{
    return i2c_master_probe(i2c_barramento(), endereco, 50) == ESP_OK;
}
