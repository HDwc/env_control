#ifndef LV_DRV_CONF_H
#define LV_DRV_CONF_H

#define USE_FBDEV 1
#define FBDEV_PATH "/dev/fb0"
#define USE_BSD_FBDEV 0

#define USE_EVDEV 1
#define EVDEV_NAME "/dev/input/event2"

#define EVDEV_CALIBRATE 0
#define EVDEV_SWAP_AXES 0
#define EVDEV_INVERT_X 0
#define EVDEV_INVERT_Y 0

#include "lv_conf.h"

#endif /* LV_DRV_CONF_H */