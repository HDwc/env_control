#include <unistd.h>
#include "../include/gpio.h"
#include "../include/led.h"

int led_init(int gpio)
{
    if (gpio_export(gpio) < 0)
        return -1;

    usleep(100000);

    if (gpio_set_direction(gpio, "out") < 0)
        return -1;

    return 0;
}

int led_on(int gpio)
{
    return gpio_write(gpio, 1);
}

int led_off(int gpio)
{
    return gpio_write(gpio, 0);
}

int led_deinit(int gpio)
{
    return gpio_unexport(gpio);
}