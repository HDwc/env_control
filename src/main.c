#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>
#include <stdint.h>

#include "lvgl.h"
#include "fbdev.h"

#include "gui_guider.h"
#include "events_init.h"
#include "custom.h"

#include "led.h"
#include "fan.h"
#include "board.h"

#include <fcntl.h>
#include <errno.h>
#include <linux/input.h>
#include <string.h>

#define DISP_BUF_LINES  80

static lv_disp_draw_buf_t draw_buf;
static lv_color_t *buf1 = NULL;

lv_ui guider_ui;

#define LCD_HOR_RES  1080
#define LCD_VER_RES   600

#define TP_MIN_X  150
#define TP_MAX_X  4010
#define TP_MIN_Y  430
#define TP_MAX_Y  3780

static int clamp_int(int v, int min, int max)
{
    if (v < min) return min;
    if (v > max) return max;
    return v;
}

static int map_value(int v, int in_min, int in_max, int out_min, int out_max)
{
    if (in_max == in_min) return out_min;
    return (v - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

static void hardware_init(void)
{
    if (led_init(LED_GPIO) < 0) {
        printf("led init failed\n");
    }

    if (fan_init(FAN_GPIO) < 0) {
        printf("fan init failed\n");
    }
}

static void lv_port_disp_init(void)
{
    static lv_disp_drv_t disp_drv;
    uint32_t fb_w = 0, fb_h = 0, fb_dpi = 0;

    fbdev_init();
    fbdev_get_sizes(&fb_w, &fb_h, &fb_dpi);

    printf("fb size: %u x %u, dpi=%u\n", fb_w, fb_h, fb_dpi);

    buf1 = malloc(sizeof(lv_color_t) * fb_w * DISP_BUF_LINES);
    if (!buf1) {
        printf("malloc draw buffer failed\n");
        return;
    }

    lv_disp_draw_buf_init(&draw_buf, buf1, NULL, fb_w * DISP_BUF_LINES);

    lv_disp_drv_init(&disp_drv);
    disp_drv.draw_buf = &draw_buf;
    disp_drv.flush_cb = fbdev_flush;
    disp_drv.hor_res = fb_w;
    disp_drv.ver_res = fb_h;

    lv_disp_drv_register(&disp_drv);
}

static int touch_fd = -1;
static int touch_x = 0;
static int touch_y = 0;
static int touch_pressed = 0;

static int touch_init(const char *dev)
{
    touch_fd = open(dev, O_RDONLY | O_NONBLOCK);
    if (touch_fd < 0) {
        perror("open touch device failed");
        return -1;
    }

    printf("touch device opened: %s, fd=%d\n", dev, touch_fd);
    return 0;
}

static void touch_deinit(void)
{
    if (touch_fd >= 0) {
        close(touch_fd);
        touch_fd = -1;
    }
}

static void touch_read(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    (void)drv;

    if (touch_fd < 0) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    struct input_event ev;
    ssize_t rb;

    while ((rb = read(touch_fd, &ev, sizeof(ev))) > 0) {
        if (ev.type == EV_ABS) {
            if (ev.code == ABS_X || ev.code == ABS_MT_POSITION_X) {
                touch_x = ev.value;
            } else if (ev.code == ABS_Y || ev.code == ABS_MT_POSITION_Y) {
                touch_y = ev.value;
            }
        } else if (ev.type == EV_KEY) {
            if (ev.code == BTN_TOUCH) {
                touch_pressed = ev.value ? 1 : 0;
            }
        }
    }

    if (rb < 0 && errno != EAGAIN) {
        perror("read touch event failed");
    }

    int px = clamp_int(touch_x, TP_MIN_X, TP_MAX_X);
    int py = clamp_int(touch_y, TP_MIN_Y, TP_MAX_Y);

    px = map_value(px, TP_MIN_X, TP_MAX_X, 0, LCD_HOR_RES - 1);
    py = map_value(py, TP_MIN_Y, TP_MAX_Y, 0, LCD_VER_RES - 1);

    data->point.x = px;
    data->point.y = py;
    data->state = touch_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;

    printf("raw(%d,%d) -> lcd(%d,%d), state=%d\n",
           touch_x, touch_y, data->point.x, data->point.y, data->state);
}

static lv_indev_t * indev_touch = NULL;

static void lv_port_indev_init(void)
{
    static lv_indev_drv_t indev_drv;

    printf("touch init start\n");
    if (touch_init("/dev/input/event2") < 0) {
        printf("touch init failed\n");
        return;
    }
    printf("touch init done\n");

    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touch_read;

    indev_touch = lv_indev_drv_register(&indev_drv);
    printf("touch indev register done, ptr=%p\n", indev_touch);
}

static void test_btn_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_PRESSED) {
        printf("TEST button pressed\n");
    } else if (code == LV_EVENT_CLICKED) {
        printf("TEST button clicked\n");
    }
}

static void create_test_btn(void)
{
    lv_obj_t *btn = lv_btn_create(lv_scr_act());
    lv_obj_set_size(btn, 160, 80);
    lv_obj_center(btn);
    lv_obj_add_event_cb(btn, test_btn_cb, LV_EVENT_ALL, NULL);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, "TEST");
    lv_obj_center(label);
}

static uint64_t get_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000ULL + tv.tv_usec / 1000;
}

int main(void)
{
    printf("1. hardware_init start\n");
    hardware_init();
    printf("2. hardware_init ok\n");

    printf("3. lv_init start\n");
    lv_init();
    printf("4. lv_init ok\n");

    printf("5. disp init start\n");
    lv_port_disp_init();
    printf("6. disp init ok\n");

    printf("7. indev init start\n");
    lv_port_indev_init();
    printf("8. indev init ok\n");

    printf("9. setup_ui start\n");
    setup_ui(&guider_ui);
    printf("10. setup_ui ok\n");

    printf("11. events_init start\n");
    events_init(&guider_ui);
    printf("12. events_init ok\n");

    printf("13. custom_init start\n");
    custom_init();
    printf("14. custom_init ok\n");

    create_test_btn();

    uint64_t last = get_ms();

    while (1) {
        uint64_t now = get_ms();
        lv_tick_inc((uint32_t)(now - last));
        last = now;

        lv_timer_handler();
        usleep(5000);
    }

    touch_deinit();
    return 0;
}