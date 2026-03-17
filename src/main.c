#include <stdio.h>
#include <unistd.h>

#include "lvgl.h"
#include "fbdev.h"
#include "evdev.h"

#include "gui_guider.h"
#include "events_init.h"
#include "custom.h"

#include "led.h"
#include "fan.h"
#include "board.h"

/* 按你的屏幕分辨率修改 */
#define SCREEN_WIDTH  1280
#define SCREEN_HEIGHT 720

#define DISP_BUF_SIZE  (SCREEN_WIDTH * 100)

static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf1[DISP_BUF_SIZE];

lv_ui guider_ui;

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

    fbdev_init();

    lv_disp_draw_buf_init(&draw_buf, buf1, NULL, DISP_BUF_SIZE);

    lv_disp_drv_init(&disp_drv);
    disp_drv.draw_buf = &draw_buf;
    disp_drv.flush_cb = fbdev_flush;
    disp_drv.hor_res = SCREEN_WIDTH;
    disp_drv.ver_res = SCREEN_HEIGHT;

    lv_disp_drv_register(&disp_drv);
}

static void lv_port_indev_init(void)
{
    static lv_indev_drv_t indev_drv;

    evdev_init();

    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = evdev_read;
    lv_indev_drv_register(&indev_drv);
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

    while (1) {
        lv_timer_handler();
        usleep(5000);
    }

    return 0;
}