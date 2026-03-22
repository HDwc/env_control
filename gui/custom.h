#ifndef CUSTOM_H
#define CUSTOM_H

#include <stdint.h>
#include "gui_guider.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Optional:
 * add compile flag `-DCUSTOM_CN_FONT=lv_font_xxx` to use your own Chinese font.
 */
int app_hardware_init(void);
int app_lvgl_port_init(void);
void app_lvgl_port_deinit(void);
uint64_t app_get_ms(void);

void custom_build_screen(lv_ui *ui);
void custom_init(void);

#ifdef __cplusplus
}
#endif

#endif
