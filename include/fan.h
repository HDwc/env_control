#ifndef FAN_H
#define FAN_H

int fan_init(int gpio);
int fan_on(int gpio);
int fan_off(int gpio);
int fan_get_state(int gpio);
int fan_deinit(int gpio);
const char *fan_get_channel(void);

#endif
