#ifndef BH1750_H
#define BH1750_H

#include <stdint.h>

#include "i2c_bus.h"

#define BH1750_I2C_ADDR 0x23

int bh1750_init(i2c_bus_t *bus, uint8_t addr);
int bh1750_read_lux(i2c_bus_t *bus, uint8_t addr, float *lux);

#endif
