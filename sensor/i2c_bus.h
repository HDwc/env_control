#ifndef I2C_BUS_H
#define I2C_BUS_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    int fd;
    int bus_id;
} i2c_bus_t;

int i2c_bus_open(i2c_bus_t *bus, int bus_id);
void i2c_bus_close(i2c_bus_t *bus);
int i2c_bus_write(i2c_bus_t *bus, uint8_t addr, const uint8_t *buf, size_t len);
int i2c_bus_read(i2c_bus_t *bus, uint8_t addr, uint8_t *buf, size_t len);
int i2c_bus_write_read(i2c_bus_t *bus, uint8_t addr, const uint8_t *wbuf, size_t wlen, uint8_t *rbuf, size_t rlen);

#endif
