#ifndef LED_H
#define LED_H

int led_init(int gpio);
int led_on(int gpio);
int led_off(int gpio);
int led_deinit(int gpio);

#endif
