#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include "../include/gpio.h"

static int write_file(const char *path, const char *value)
{
    static int open_warned;
    static int write_warned;
    int fd = open(path, O_WRONLY);
    ssize_t wr;
    size_t len;
    if (fd < 0)
    {
        if (!open_warned) {
            perror(path);
            open_warned = 1;
        }
        return -1;
    }

    len = strlen(value);
    wr = write(fd, value, len);
    if (wr < 0 || (size_t)wr != len)
    {
        int saved_errno = errno;
        if (!write_warned) {
            perror("write");
            write_warned = 1;
        }
        close(fd);
        errno = saved_errno;
        return -1;
    }

    close(fd);
    return 0;
}

int gpio_export(int gpio)
{
    char buffer[16];
    int fd;
    size_t len;
    ssize_t wr;

    sprintf(buffer, "%d", gpio);
    fd = open("/sys/class/gpio/export", O_WRONLY);
    if (fd < 0) {
        perror("/sys/class/gpio/export");
        return -1;
    }

    len = strlen(buffer);
    wr = write(fd, buffer, len);
    if (wr < 0 || (size_t)wr != len) {
        int saved_errno = errno;
        close(fd);
        if (saved_errno == EBUSY) {
            return 0;
        }
        errno = saved_errno;
        perror("write");
        return -1;
    }

    close(fd);
    return 0;
}

int gpio_unexport(int gpio)
{
    char buffer[16];
    sprintf(buffer, "%d", gpio);
    return write_file("/sys/class/gpio/unexport", buffer);
}

int gpio_set_direction(int gpio, char *dir)
{
    char path[64];
    sprintf(path, "/sys/class/gpio/gpio%d/direction", gpio);
    return write_file(path, dir);
}

int gpio_write(int gpio, int value)
{
    char path[64];
    char val[8];

    sprintf(path, "/sys/class/gpio/gpio%d/value", gpio);
    sprintf(val, "%d", value);

    return write_file(path, val);
}

int gpio_read(int gpio)
{
    char path[64];
    char value_str[8];
    int fd;

    sprintf(path, "/sys/class/gpio/gpio%d/value", gpio);

    fd = open(path, O_RDONLY);
    if (fd < 0)
    {
        perror(path);
        return -1;
    }

    if (read(fd, value_str, sizeof(value_str) - 1) < 0)
    {
        perror("read");
        close(fd);
        return -1;
    }

    close(fd);
    return atoi(value_str);
}
