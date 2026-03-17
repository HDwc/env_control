#ifndef GPIO_H
#define GPIO_H

int gpio_export(int gpio);
int gpio_unexport(int gpio);
int gpio_set_direction(int gpio, char *dir);
int gpio_write(int gpio, int value);
int gpio_read(int gpio);

#endif