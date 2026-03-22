#include <stdint.h>
#include <unistd.h>

#include "lvgl.h"

#include "custom.h"
#include "events_init.h"
#include "gui_guider.h"

lv_ui guider_ui;

int main(void)
{
    (void)app_hardware_init();

    lv_init();
    if (app_lvgl_port_init() < 0) {
        return -1;
    }

    setup_ui(&guider_ui);
    events_init(&guider_ui);
    custom_init();

    uint64_t last = app_get_ms();

    while (1) {
        uint64_t now = app_get_ms();
        lv_tick_inc((uint32_t)(now - last));
        last = now;

        lv_timer_handler();
        usleep(5000);
    }

    app_lvgl_port_deinit();
    return 0;
}
