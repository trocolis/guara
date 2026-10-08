#ifndef GUARA_I2C_H
#define GUARA_I2C_H

#include "driver/i2c_master.h"

/* Barramento I2C único, compartilhado pelo display e pelo RTC. */
i2c_master_bus_handle_t i2c_bus(void);
i2c_master_dev_handle_t i2c_add_device(uint8_t address);
bool i2c_is_present(uint8_t address);

#endif
