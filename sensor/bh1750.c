#include "bh1750.h"

#include <stdio.h>
#include <unistd.h>

/* BH1750 commands */
#define BH1750_CMD_POWER_ON 0x01
#define BH1750_CMD_RESET 0x07
#define BH1750_CMD_CONT_H_RES 0x10

int bh1750_init(i2c_bus_t *bus, uint8_t addr)
{
    uint8_t cmd = BH1750_CMD_POWER_ON;

    if (i2c_bus_write(bus, addr, &cmd, 1) < 0) return -1;
    usleep(1000);

    cmd = BH1750_CMD_RESET;
    if (i2c_bus_write(bus, addr, &cmd, 1) < 0) return -1;
    usleep(1000);

    cmd = BH1750_CMD_CONT_H_RES;
    if (i2c_bus_write(bus, addr, &cmd, 1) < 0) return -1;
    usleep(180000);

    printf("[bh1750] init done\n");
    return 0;
}

int bh1750_read_lux(i2c_bus_t *bus, uint8_t addr, float *lux)
{
    uint8_t rx[2];
    uint16_t raw;

    if (!lux) return -1;
    if (i2c_bus_read(bus, addr, rx, sizeof(rx)) < 0) return -1;

    raw = ((uint16_t)rx[0] << 8) | (uint16_t)rx[1];
    *lux = (float)raw / 1.2f;
    return 0;
}
