#include "lvgl.h"
#include "gui_guider.h"
#include "events_init.h"
#include "custom.h"

void setup_scr_screen(lv_ui *ui)
{
    custom_build_screen(ui);
    lv_obj_update_layout(ui->screen);
    events_init_screen(ui);
}
