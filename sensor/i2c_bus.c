#include "i2c_bus.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int i2c_bus_set_addr(i2c_bus_t *bus, uint8_t addr)
{
    if (!bus || bus->fd < 0) return -1;
    if (ioctl(bus->fd, I2C_SLAVE, addr) < 0) {
        fprintf(stderr, "[i2c] ioctl I2C_SLAVE 0x%02X failed: %s\n", addr, strerror(errno));
        return -1;
    }
    return 0;
}

int i2c_bus_open(i2c_bus_t *bus, int bus_id)
{
    char dev[32];

    if (!bus) return -1;

    memset(bus, 0, sizeof(*bus));
    bus->fd = -1;
    bus->bus_id = bus_id;

    snprintf(dev, sizeof(dev), "/dev/i2c-%d", bus_id);
    bus->fd = open(dev, O_RDWR);
    if (bus->fd < 0) {
        fprintf(stderr, "[i2c] open %s failed: %s\n", dev, strerror(errno));
        return -1;
    }

    printf("[i2c] bus opened: %s\n", dev);
    return 0;
}

void i2c_bus_close(i2c_bus_t *bus)
{
    if (!bus) return;
    if (bus->fd >= 0) {
        close(bus->fd);
        bus->fd = -1;
        printf("[i2c] bus closed: /dev/i2c-%d\n", bus->bus_id);
    }
}

int i2c_bus_write(i2c_bus_t *bus, uint8_t addr, const uint8_t *buf, size_t len)
{
    ssize_t wb;

    if (!bus || !buf || len == 0) return -1;
    if (i2c_bus_set_addr(bus, addr) < 0) return -1;

    wb = write(bus->fd, buf, len);
    if (wb != (ssize_t)len) {
        fprintf(stderr, "[i2c] write to 0x%02X failed: %s\n", addr, strerror(errno));
        return -1;
    }
    return 0;
}

int i2c_bus_read(i2c_bus_t *bus, uint8_t addr, uint8_t *buf, size_t len)
{
    ssize_t rb;

    if (!bus || !buf || len == 0) return -1;
    if (i2c_bus_set_addr(bus, addr) < 0) return -1;

    rb = read(bus->fd, buf, len);
    if (rb != (ssize_t)len) {
        fprintf(stderr, "[i2c] read from 0x%02X failed: %s\n", addr, strerror(errno));
        return -1;
    }
    return 0;
}

int i2c_bus_write_read(i2c_bus_t *bus, uint8_t addr, const uint8_t *wbuf, size_t wlen, uint8_t *rbuf, size_t rlen)
{
    if (wbuf && wlen > 0) {
        if (i2c_bus_write(bus, addr, wbuf, wlen) < 0) return -1;
    }
    if (rbuf && rlen > 0) {
        if (i2c_bus_read(bus, addr, rbuf, rlen) < 0) return -1;
    }
    return 0;
}
