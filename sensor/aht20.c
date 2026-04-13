#include "aht20.h"

#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>

static uint8_t aht20_crc8(const uint8_t *data, int len)
{
    uint8_t crc = 0xFF;
    for (int i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) {
            if (crc & 0x80) crc = (uint8_t)((crc << 1) ^ 0x31);
            else crc <<= 1;
        }
    }
    return crc;
}

int aht20_init(i2c_bus_t *bus, uint8_t addr)
{
    uint8_t reset_cmd = 0xBA;
    uint8_t init_cmd[3] = {0xBE, 0x08, 0x00};
    uint8_t status = 0;

    if (i2c_bus_write(bus, addr, &reset_cmd, 1) < 0) return -1;
    usleep(20000);

    if (i2c_bus_write(bus, addr, init_cmd, sizeof(init_cmd)) < 0) return -1;
    usleep(10000);

    if (i2c_bus_read(bus, addr, &status, 1) < 0) return -1;
    printf("[aht20] init status=0x%02X\n", status);
    return 0;
}

int aht20_read(i2c_bus_t *bus, uint8_t addr, float *temp_c, float *hum_pct)
{
    uint8_t cmd[3] = {0xAC, 0x33, 0x00};
    uint8_t rx[7] = {0};
    uint32_t raw_h;
    uint32_t raw_t;
    uint8_t crc;
    bool crc_ok = true;

    if (!temp_c || !hum_pct) return -1;

    if (i2c_bus_write(bus, addr, cmd, sizeof(cmd)) < 0) return -1;
    usleep(90000);

    if (i2c_bus_read(bus, addr, rx, sizeof(rx)) < 0) return -1;
    if (rx[0] & 0x80) {
        fprintf(stderr, "[aht20] sensor busy, status=0x%02X\n", rx[0]);
        return -1;
    }

    crc = aht20_crc8(rx, 6);
    if (crc != rx[6]) {
        crc_ok = false;
        fprintf(stderr, "[aht20] crc mismatch calc=0x%02X read=0x%02X\n", crc, rx[6]);
    }

    raw_h = ((uint32_t)rx[1] << 12) | ((uint32_t)rx[2] << 4) | ((uint32_t)rx[3] >> 4);
    raw_t = (((uint32_t)rx[3] & 0x0F) << 16) | ((uint32_t)rx[4] << 8) | (uint32_t)rx[5];

    *hum_pct = ((float)raw_h * 100.0f) / 1048576.0f;
    *temp_c = ((float)raw_t * 200.0f) / 1048576.0f - 50.0f;

    return crc_ok ? 0 : -2;
}
