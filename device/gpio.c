#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include "../include/gpio.h"

static int write_file(const char *path, const char *value)
{
    int fd = open(path, O_WRONLY);
    if (fd < 0)
    {
        perror(path);
        return -1;
    }

    write(fd, value, strlen(value));
    close(fd);
    return 0;
}

int gpio_export(int gpio)
{
    char buffer[16];
    sprintf(buffer, "%d", gpio);
    return write_file("/sys/class/gpio/export", buffer);
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