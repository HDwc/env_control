#ifndef AHT20_H
#define AHT20_H

#include <stdint.h>

#include "i2c_bus.h"

#define AHT20_I2C_ADDR 0x38

int aht20_init(i2c_bus_t *bus, uint8_t addr);
int aht20_read(i2c_bus_t *bus, uint8_t addr, float *temp_c, float *hum_pct);

#endif
