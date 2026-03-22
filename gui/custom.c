#include "custom.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <linux/input.h>

#include "fbdev.h"
#include "board.h"
#include "fan.h"
#include "led.h"

LV_FONT_DECLARE(lv_font_kaiti_16);
LV_IMG_DECLARE(_icon_title_alpha_60x50);
LV_IMG_DECLARE(_icon_temp_alpha_150x150);
LV_IMG_DECLARE(_icon_hum_alpha_150x150);
LV_IMG_DECLARE(_icon_fan_alpha_180x180);
LV_IMG_DECLARE(_icon_light_alpha_200x180);

#define DISP_BUF_LINES      80
#define LCD_HOR_RES         1080
#define LCD_VER_RES         600

#define TP_MIN_X            150
#define TP_MAX_X            4010
#define TP_MIN_Y            430
#define TP_MAX_Y            3780

#define TOUCH_DEV_PATH      "/dev/input/event2"

#define COLOR_BG            0x0D1524
#define COLOR_PANEL         0x142136
#define COLOR_CARD          0x1B2A42
#define COLOR_PRIMARY       0x2B6CFF
#define COLOR_ACCENT_WARM   0xFF9933
#define COLOR_ACCENT_COOL   0x2FD5FF
#define COLOR_ON            0x19C37D
#define COLOR_OFF           0x4D5D78
#define COLOR_TEXT_MAIN     0xEAF2FF
#define COLOR_TEXT_SUB      0x9DB0CF

#define ICON_TITLE_PATH     "S:gui/images/icon_title.png"
#define ICON_TEMP_PATH      "S:gui/images/icon_temp.png"
#define ICON_HUM_PATH       "S:gui/images/icon_hum.png"
#define ICON_FAN_PATH       "S:gui/images/icon_fan.png"
#define ICON_LIGHT_PATH     "S:gui/images/icon_light.png"

#define UI_OFFSET_X         -26
#define UI_OFFSET_Y         6

#ifdef CUSTOM_CN_FONT
#define FONT_CN (&CUSTOM_CN_FONT)
#else
#define FONT_CN (&lv_font_kaiti_16)
#endif
#define FONT_UI FONT_CN

typedef enum {
    MODULE_TEMP = 0,
    MODULE_LIGHT,
    MODULE_HUM_FAN,
    MODULE_LAMP
} module_type_t;

static lv_disp_draw_buf_t draw_buf;
static lv_color_t *buf1;

static int touch_fd = -1;
static int touch_x;
static int touch_y;
static int touch_pressed;

static bool led_ready;
static bool fan_ready;

static lv_timer_t *clock_timer;
static lv_timer_t *sensor_sim_timer;

static int g_temp = 26;
static int g_humidity = 52;
static int g_lux = 387;
static int g_fan_speed;
static bool g_light_on;
static module_type_t g_active_module = MODULE_TEMP;

typedef struct {
    lv_obj_t *time_label;
    lv_obj_t *status_led;

    lv_obj_t *temp_value;
    lv_obj_t *temp_bar;
    lv_obj_t *humidity_value;
    lv_obj_t *fan_state_label;
    lv_obj_t *fan_slider;
    lv_obj_t *light_arc;
    lv_obj_t *light_value;
    lv_obj_t *light_state_label;
    lv_obj_t *light_on_btn;
    lv_obj_t *light_off_btn;

    lv_obj_t *detail_mask;
    lv_obj_t *detail_panel;
    lv_obj_t *detail_title;
    lv_obj_t *detail_desc;
    lv_obj_t *detail_data_map;
    lv_obj_t *detail_value;
    lv_obj_t *detail_image_slot;
    lv_obj_t *detail_image_hint;

    lv_obj_t *detail_fan_slider;
    lv_obj_t *detail_fan_slider_label;

    lv_obj_t *detail_light_on_btn;
    lv_obj_t *detail_light_off_btn;
} custom_widgets_t;

static custom_widgets_t g_widgets;

static const char *module_icon_path(module_type_t module);
static lv_obj_t *create_image_slot(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h);
static void set_slot_icon(lv_obj_t *slot, const char *icon_path, const char *fallback);
static bool try_set_slot_image(lv_obj_t *slot, const char *path);
static bool try_set_slot_image_dsc(lv_obj_t *slot, const lv_img_dsc_t *dsc);

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

static void freeze_obj(lv_obj_t *obj)
{
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(obj, LV_DIR_NONE);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

static void set_obj_bg(lv_obj_t *obj, uint32_t color, lv_opa_t opa)
{
    lv_obj_set_style_bg_opa(obj, opa, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(obj, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void style_panel(lv_obj_t *obj, uint32_t color, lv_coord_t radius)
{
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(obj, radius, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    set_obj_bg(obj, color, LV_OPA_COVER);
    freeze_obj(obj);
}

static void style_label(lv_obj_t *obj, uint32_t color, const lv_font_t *font)
{
    lv_obj_set_style_text_color(obj, lv_color_hex(color), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(obj, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(obj, font, LV_PART_MAIN | LV_STATE_DEFAULT);
}

static const char *fan_level_text_cn(int speed)
{
    if (speed <= 0) return "关闭";
    if (speed < 34) return "低速";
    if (speed < 67) return "中速";
    return "高速";
}

static const char *module_title_cn(module_type_t module)
{
    switch (module) {
    case MODULE_TEMP: return "温度模块";
    case MODULE_LIGHT: return "光照模块";
    case MODULE_HUM_FAN: return "湿度/风扇模块";
    case MODULE_LAMP: return "灯光模块";
    default: return "详情";
    }
}

static const char *module_image_slot(module_type_t module)
{
    switch (module) {
    case MODULE_TEMP: return "图片资源: _icon_temp_alpha_150x150.c";
    case MODULE_LIGHT: return "图片资源: _icon_light_alpha_200x180.c";
    case MODULE_HUM_FAN: return "图片资源: _icon_hum_alpha_150x150.c + _icon_fan_alpha_180x180.c";
    case MODULE_LAMP: return "灯光模块无图标";
    default: return "图片资源: gui/images/*.c";
    }
}

static const char *module_icon_path(module_type_t module)
{
    switch (module) {
    case MODULE_TEMP: return ICON_TEMP_PATH;
    case MODULE_LIGHT: return ICON_LIGHT_PATH;
    case MODULE_HUM_FAN: return ICON_HUM_PATH;
    case MODULE_LAMP: return NULL;
    default: return NULL;
    }
}

static void update_time_label(void)
{
    if (!g_widgets.time_label) return;

    time_t now = time(NULL);
    struct tm *tm_now = localtime(&now);
    if (tm_now == NULL) {
        lv_label_set_text(g_widgets.time_label, "--:--:--");
        return;
    }

    char text[16];
    strftime(text, sizeof(text), "%H:%M:%S", tm_now);
    lv_label_set_text(g_widgets.time_label, text);
}
static void update_fan_state_labels(void)
{
    if (g_widgets.fan_state_label) {
        char text[48];
        snprintf(text, sizeof(text), "风扇: %s (%d%%)", fan_level_text_cn(g_fan_speed), g_fan_speed);
        lv_label_set_text(g_widgets.fan_state_label, text);
    }

    if (g_widgets.detail_fan_slider_label) {
        char text[48];
        snprintf(text, sizeof(text), "档位: %s (%d%%)", fan_level_text_cn(g_fan_speed), g_fan_speed);
        lv_label_set_text(g_widgets.detail_fan_slider_label, text);
    }
}

static void update_light_button_style(void)
{
    if (!g_widgets.light_on_btn || !g_widgets.light_off_btn || !g_widgets.light_state_label) {
        return;
    }

    set_obj_bg(g_widgets.light_on_btn, g_light_on ? COLOR_ON : COLOR_OFF, LV_OPA_COVER);
    set_obj_bg(g_widgets.light_off_btn, g_light_on ? COLOR_OFF : COLOR_ON, LV_OPA_COVER);
    lv_label_set_text(g_widgets.light_state_label, g_light_on ? "灯光: 开" : "灯光: 关");

    if (g_widgets.detail_light_on_btn && g_widgets.detail_light_off_btn) {
        set_obj_bg(g_widgets.detail_light_on_btn, g_light_on ? COLOR_ON : COLOR_OFF, LV_OPA_COVER);
        set_obj_bg(g_widgets.detail_light_off_btn, g_light_on ? COLOR_OFF : COLOR_ON, LV_OPA_COVER);
    }
}

static void apply_light_hardware(void)
{
    if (!led_ready) return;

    if (g_light_on) {
        (void)led_on(LED_GPIO);
    } else {
        (void)led_off(LED_GPIO);
    }
}

static void apply_fan_hardware(void)
{
    if (!fan_ready) return;

    if (g_fan_speed > 0) {
        (void)fan_on(FAN_GPIO);
    } else {
        (void)fan_off(FAN_GPIO);
    }
}

static void update_detail_dynamic_value(void)
{
    if (!g_widgets.detail_value) return;

    char text[80];
    switch (g_active_module) {
    case MODULE_TEMP:
        snprintf(text, sizeof(text), "温度(模拟): %d C", g_temp);
        break;
    case MODULE_LIGHT:
        snprintf(text, sizeof(text), "光照(模拟): %d LUX", g_lux);
        break;
    case MODULE_HUM_FAN:
        snprintf(text, sizeof(text), "湿度(模拟): %d%%", g_humidity);
        break;
    case MODULE_LAMP:
        snprintf(text, sizeof(text), "灯光(模拟): %s", g_light_on ? "开" : "关");
        break;
    default:
        text[0] = '\0';
        break;
    }

    lv_label_set_text(g_widgets.detail_value, text);
}

static void update_sensor_widgets(void)
{
    if (g_widgets.temp_value) {
        char temp[24];
        snprintf(temp, sizeof(temp), "%d C", g_temp);
        lv_label_set_text(g_widgets.temp_value, temp);
    }

    if (g_widgets.temp_bar) {
        int temp_pct = clamp_int(map_value(g_temp, 0, 50, 0, 100), 0, 100);
        lv_bar_set_value(g_widgets.temp_bar, temp_pct, LV_ANIM_OFF);
    }

    if (g_widgets.humidity_value) {
        char hum[24];
        snprintf(hum, sizeof(hum), "%d%%", g_humidity);
        lv_label_set_text(g_widgets.humidity_value, hum);
    }

    if (g_widgets.light_value) {
        char lux[24];
        snprintf(lux, sizeof(lux), "%d LUX", g_lux);
        lv_label_set_text(g_widgets.light_value, lux);
    }

    if (g_widgets.light_arc) {
        int arc_val = clamp_int(map_value(g_lux, 0, 1000, 0, 100), 0, 100);
        lv_arc_set_value(g_widgets.light_arc, arc_val);
    }

    update_detail_dynamic_value();
}

static void set_detail_controls_visible(bool fan_visible, bool light_visible)
{
    if (g_widgets.detail_fan_slider) {
        if (fan_visible) lv_obj_clear_flag(g_widgets.detail_fan_slider, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(g_widgets.detail_fan_slider, LV_OBJ_FLAG_HIDDEN);
    }
    if (g_widgets.detail_fan_slider_label) {
        if (fan_visible) lv_obj_clear_flag(g_widgets.detail_fan_slider_label, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(g_widgets.detail_fan_slider_label, LV_OBJ_FLAG_HIDDEN);
    }

    if (g_widgets.detail_light_on_btn) {
        if (light_visible) lv_obj_clear_flag(g_widgets.detail_light_on_btn, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(g_widgets.detail_light_on_btn, LV_OBJ_FLAG_HIDDEN);
    }
    if (g_widgets.detail_light_off_btn) {
        if (light_visible) lv_obj_clear_flag(g_widgets.detail_light_off_btn, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(g_widgets.detail_light_off_btn, LV_OBJ_FLAG_HIDDEN);
    }
}

static void update_detail_layout(void)
{
    if (!g_widgets.detail_title) return;

    lv_label_set_text(g_widgets.detail_title, module_title_cn(g_active_module));
    lv_label_set_text(g_widgets.detail_image_hint, module_image_slot(g_active_module));
    set_slot_icon(g_widgets.detail_image_slot, module_icon_path(g_active_module), "无图标");

    switch (g_active_module) {
    case MODULE_TEMP:
        lv_label_set_text(g_widgets.detail_desc, "显示环境温度，当前为模拟值。");
        lv_label_set_text(g_widgets.detail_data_map, "对接: TEMP_SENSOR_VALUE (AHT20)\n逻辑: app_logic/temp_control.c");
        set_detail_controls_visible(false, false);
        break;
    case MODULE_LIGHT:
        lv_label_set_text(g_widgets.detail_desc, "显示光照强度，当前为模拟值。");
        lv_label_set_text(g_widgets.detail_data_map, "对接: LIGHT_SENSOR_VALUE (BH1750)\n逻辑: app_logic/light_strategy.c");
        set_detail_controls_visible(false, false);
        break;
    case MODULE_HUM_FAN:
        lv_label_set_text(g_widgets.detail_desc, "显示湿度并控制风扇转速。");
        lv_label_set_text(g_widgets.detail_data_map, "对接: HUM_SENSOR_VALUE (AHT20)\n输出: FAN_CTRL_OUTPUT (GPIO41)");
        set_detail_controls_visible(true, false);
        break;
    case MODULE_LAMP:
        lv_label_set_text(g_widgets.detail_desc, "控制灯光开关并同步状态。");
        lv_label_set_text(g_widgets.detail_data_map, "输出: LIGHT_CTRL_OUTPUT (GPIO40)\n同步: LIGHT_STATE_UI_LABEL");
        set_detail_controls_visible(false, true);
        break;
    default:
        set_detail_controls_visible(false, false);
        break;
    }

    update_detail_dynamic_value();
    update_fan_state_labels();
    update_light_button_style();
}

static void show_detail(module_type_t module)
{
    g_active_module = module;
    update_detail_layout();

    if (g_widgets.detail_mask) {
        lv_obj_clear_flag(g_widgets.detail_mask, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(g_widgets.detail_mask);
    }
}

static void hide_detail(void)
{
    if (g_widgets.detail_mask) {
        lv_obj_add_flag(g_widgets.detail_mask, LV_OBJ_FLAG_HIDDEN);
    }
}

static void clock_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    update_time_label();
}

static void sensor_sim_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    static uint8_t phase;
    static const int temp_table[] = {25, 26, 27, 28, 27, 26, 25, 24};
    static const int hum_table[]  = {50, 51, 53, 55, 56, 54, 53, 52};
    static const int lux_table[]  = {320, 360, 410, 470, 520, 480, 430, 380};

    g_temp = temp_table[phase];
    g_humidity = hum_table[phase];
    g_lux = lux_table[phase];

    phase = (phase + 1U) % 8U;
    update_sensor_widgets();
}
static void fan_slider_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_VALUE_CHANGED && code != LV_EVENT_RELEASED) {
        return;
    }

    lv_obj_t *slider = lv_event_get_target(e);
    g_fan_speed = lv_slider_get_value(slider);

    if (slider != g_widgets.fan_slider && g_widgets.fan_slider) {
        lv_slider_set_value(g_widgets.fan_slider, g_fan_speed, LV_ANIM_OFF);
    }
    if (slider != g_widgets.detail_fan_slider && g_widgets.detail_fan_slider) {
        lv_slider_set_value(g_widgets.detail_fan_slider, g_fan_speed, LV_ANIM_OFF);
    }

    apply_fan_hardware();
    update_fan_state_labels();
    update_detail_dynamic_value();
}

static void light_on_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    g_light_on = true;
    apply_light_hardware();
    update_light_button_style();
    update_detail_dynamic_value();
}

static void light_off_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    g_light_on = false;
    apply_light_hardware();
    update_light_button_style();
    update_detail_dynamic_value();
}

static void nav_button_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    module_type_t module = (module_type_t)(intptr_t)lv_event_get_user_data(e);
    show_detail(module);
}

static void detail_close_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    hide_detail();
}

static lv_obj_t *create_card(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, w, h);
    style_panel(card, COLOR_CARD, 16);
    return card;
}

static lv_obj_t *create_image_slot(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h)
{
    lv_obj_t *slot = lv_obj_create(parent);
    lv_obj_set_pos(slot, x, y);
    lv_obj_set_size(slot, w, h);
    lv_obj_set_style_border_width(slot, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(slot, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(slot, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    set_obj_bg(slot, COLOR_PANEL, LV_OPA_TRANSP);
    freeze_obj(slot);

    return slot;
}

static bool try_set_slot_image(lv_obj_t *slot, const char *path)
{
#if LV_USE_PNG && LV_USE_FS_POSIX
    if (!path || path[0] == '\0') return false;

    lv_img_header_t header;
    if (lv_img_decoder_get_info(path, &header) != LV_RES_OK || header.w == 0 || header.h == 0) {
        return false;
    }

    lv_obj_t *img = lv_img_create(slot);
    lv_img_set_src(img, path);
    lv_obj_center(img);

    lv_coord_t sw = lv_obj_get_width(slot) - 4;
    lv_coord_t sh = lv_obj_get_height(slot) - 4;
    uint32_t zoom_w = ((uint32_t)sw * 256U) / header.w;
    uint32_t zoom_h = ((uint32_t)sh * 256U) / header.h;
    uint32_t zoom = (zoom_w < zoom_h) ? zoom_w : zoom_h;
    if (zoom > 256U) zoom = 256U;
    if (zoom < 64U) zoom = 64U;
    lv_img_set_zoom(img, (uint16_t)zoom);
    return true;
#else
    (void)slot;
    (void)path;
    return false;
#endif
}

static bool try_set_slot_image_dsc(lv_obj_t *slot, const lv_img_dsc_t *dsc)
{
    if (!dsc) return false;

    lv_obj_t *img = lv_img_create(slot);
    lv_img_set_src(img, dsc);
    lv_obj_center(img);

    if (dsc->header.w > 0 && dsc->header.h > 0) {
        lv_coord_t sw = lv_obj_get_width(slot) - 4;
        lv_coord_t sh = lv_obj_get_height(slot) - 4;
        uint32_t zoom_w = ((uint32_t)sw * 256U) / dsc->header.w;
        uint32_t zoom_h = ((uint32_t)sh * 256U) / dsc->header.h;
        uint32_t zoom = (zoom_w < zoom_h) ? zoom_w : zoom_h;
        if (zoom > 256U) zoom = 256U;
        if (zoom < 64U) zoom = 64U;
        lv_img_set_zoom(img, (uint16_t)zoom);
    }

    return true;
}

static void set_slot_icon(lv_obj_t *slot, const char *icon_path, const char *fallback)
{
    lv_obj_clean(slot);

    if (icon_path && strcmp(icon_path, ICON_TITLE_PATH) == 0) {
        if (try_set_slot_image_dsc(slot, &_icon_title_alpha_60x50)) return;
    }
    if (icon_path && strcmp(icon_path, ICON_TEMP_PATH) == 0) {
        if (try_set_slot_image_dsc(slot, &_icon_temp_alpha_150x150)) return;
    }
    if (icon_path && strcmp(icon_path, ICON_HUM_PATH) == 0) {
        if (try_set_slot_image_dsc(slot, &_icon_hum_alpha_150x150)) return;
    }
    if (icon_path && strcmp(icon_path, ICON_FAN_PATH) == 0) {
        if (try_set_slot_image_dsc(slot, &_icon_fan_alpha_180x180)) return;
    }
    if (icon_path && strcmp(icon_path, ICON_LIGHT_PATH) == 0) {
        if (try_set_slot_image_dsc(slot, &_icon_light_alpha_200x180)) return;
    }

    if (try_set_slot_image(slot, icon_path)) return;
    if (icon_path && icon_path[0] == 'S' && icon_path[1] == ':') {
        if (try_set_slot_image(slot, icon_path + 2)) return;
    }

    lv_obj_t *txt = lv_label_create(slot);
    lv_label_set_text(txt, fallback);
    lv_obj_center(txt);
    style_label(txt, COLOR_TEXT_SUB, FONT_CN);
}

static void build_header(lv_obj_t *root)
{
    lv_obj_t *header = lv_obj_create(root);
    lv_obj_set_pos(header, 16 + UI_OFFSET_X, 10 + UI_OFFSET_Y);
    lv_obj_set_size(header, 1030, 68);
    style_panel(header, COLOR_PANEL, 12);

    lv_obj_t *title_icon = create_image_slot(header, 14, 9, 60, 50);
    set_slot_icon(title_icon, ICON_TITLE_PATH, "图标");

    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "环境控制系统");
    lv_obj_set_pos(title, 86, 20);
    style_label(title, COLOR_TEXT_MAIN, FONT_CN);

    g_widgets.time_label = lv_label_create(header);
    lv_obj_set_pos(g_widgets.time_label, 900, 24);
    style_label(g_widgets.time_label, COLOR_TEXT_MAIN, FONT_CN);

    g_widgets.status_led = lv_led_create(header);
    lv_obj_set_pos(g_widgets.status_led, 984, 24);
    lv_obj_set_size(g_widgets.status_led, 18, 18);
    lv_led_set_brightness(g_widgets.status_led, LV_LED_BRIGHT_MAX);
    lv_led_set_color(g_widgets.status_led, lv_color_hex(COLOR_ON));
}

static void build_temperature_card(lv_obj_t *parent)
{
    lv_obj_t *card = create_card(parent, 0, 0, 520, 200);

    lv_obj_t *icon_slot = create_image_slot(card, 24, 20, 136, 136);
    set_slot_icon(icon_slot, ICON_TEMP_PATH, "温度");
    lv_obj_add_event_cb(icon_slot, nav_button_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)MODULE_TEMP);

    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text(title, "温度");
    lv_obj_set_pos(title, 184, 22);
    style_label(title, COLOR_TEXT_SUB, FONT_CN);

    g_widgets.temp_value = lv_label_create(card);
    lv_obj_set_pos(g_widgets.temp_value, 184, 60);
    style_label(g_widgets.temp_value, COLOR_TEXT_MAIN, FONT_UI);

    g_widgets.temp_bar = lv_bar_create(card);
    lv_obj_set_pos(g_widgets.temp_bar, 184, 118);
    lv_obj_set_size(g_widgets.temp_bar, 310, 18);
    lv_bar_set_range(g_widgets.temp_bar, 0, 100);
    lv_obj_set_style_radius(g_widgets.temp_bar, 9, LV_PART_MAIN | LV_STATE_DEFAULT);
    set_obj_bg(g_widgets.temp_bar, COLOR_OFF, LV_OPA_30);
    lv_obj_set_style_bg_color(g_widgets.temp_bar, lv_color_hex(COLOR_ACCENT_WARM), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(g_widgets.temp_bar, LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(g_widgets.temp_bar, 9, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    freeze_obj(g_widgets.temp_bar);

    lv_obj_t *state = lv_label_create(card);
    lv_label_set_text(state, "温度传感: 模拟值");
    lv_obj_set_pos(state, 184, 150);
    style_label(state, COLOR_ACCENT_WARM, FONT_CN);

    lv_obj_t *map = lv_label_create(card);
    lv_label_set_text(map, "数据位: TEMP_SENSOR_VALUE(AHT20)");
    lv_obj_set_pos(map, 24, 176);
    style_label(map, COLOR_TEXT_SUB, FONT_CN);
}

static void build_light_card(lv_obj_t *parent)
{
    lv_obj_t *card = create_card(parent, 0, 208, 520, 246);

    lv_obj_t *icon_slot = create_image_slot(card, 24, 26, 226, 170);
    set_slot_icon(icon_slot, ICON_LIGHT_PATH, "光照");
    lv_obj_add_event_cb(icon_slot, nav_button_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)MODULE_LIGHT);

    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text(title, "光照");
    lv_obj_set_pos(title, 270, 28);
    style_label(title, COLOR_TEXT_SUB, FONT_CN);

    g_widgets.light_value = lv_label_create(card);
    lv_obj_set_pos(g_widgets.light_value, 270, 74);
    style_label(g_widgets.light_value, COLOR_TEXT_MAIN, FONT_UI);

    lv_obj_t *state = lv_label_create(card);
    lv_label_set_text(state, "光照传感: 模拟值");
    lv_obj_set_pos(state, 270, 112);
    style_label(state, COLOR_ACCENT_COOL, FONT_CN);

    g_widgets.light_arc = lv_arc_create(card);
    lv_obj_set_pos(g_widgets.light_arc, 272, 146);
    lv_obj_set_size(g_widgets.light_arc, 124, 88);
    lv_arc_set_rotation(g_widgets.light_arc, 180);
    lv_arc_set_bg_angles(g_widgets.light_arc, 0, 180);
    lv_arc_set_range(g_widgets.light_arc, 0, 100);
    lv_obj_remove_style(g_widgets.light_arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(g_widgets.light_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(g_widgets.light_arc, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_color(g_widgets.light_arc, lv_color_hex(COLOR_OFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_opa(g_widgets.light_arc, LV_OPA_30, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_width(g_widgets.light_arc, 8, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_color(g_widgets.light_arc, lv_color_hex(COLOR_ACCENT_COOL), LV_PART_INDICATOR | LV_STATE_DEFAULT);

    lv_obj_t *map = lv_label_create(card);
    lv_label_set_text(map, "数据位: LIGHT_SENSOR_VALUE(BH1750)");
    lv_obj_set_pos(map, 24, 214);
    style_label(map, COLOR_TEXT_SUB, FONT_CN);
}

static void build_humidity_fan_card(lv_obj_t *parent)
{
    lv_obj_t *card = create_card(parent, 0, 0, 500, 314);

    lv_obj_t *hum_icon_slot = create_image_slot(card, 22, 24, 122, 122);
    set_slot_icon(hum_icon_slot, ICON_HUM_PATH, "湿度");
    lv_obj_add_event_cb(hum_icon_slot, nav_button_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)MODULE_HUM_FAN);

    lv_obj_t *fan_icon_slot = create_image_slot(card, 318, 26, 158, 158);
    set_slot_icon(fan_icon_slot, ICON_FAN_PATH, "风扇");
    lv_obj_add_event_cb(fan_icon_slot, nav_button_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)MODULE_HUM_FAN);

    lv_obj_t *hum_title = lv_label_create(card);
    lv_label_set_text(hum_title, "湿度");
    lv_obj_set_pos(hum_title, 164, 36);
    style_label(hum_title, COLOR_TEXT_SUB, FONT_CN);

    g_widgets.humidity_value = lv_label_create(card);
    lv_obj_set_pos(g_widgets.humidity_value, 164, 72);
    style_label(g_widgets.humidity_value, COLOR_TEXT_MAIN, FONT_UI);

    lv_obj_t *fan_title = lv_label_create(card);
    lv_label_set_text(fan_title, "风扇控制");
    lv_obj_set_pos(fan_title, 24, 188);
    style_label(fan_title, COLOR_TEXT_SUB, FONT_CN);

    g_widgets.fan_state_label = lv_label_create(card);
    lv_obj_set_pos(g_widgets.fan_state_label, 132, 188);
    style_label(g_widgets.fan_state_label, COLOR_TEXT_MAIN, FONT_CN);

    g_widgets.fan_slider = lv_slider_create(card);
    lv_obj_set_pos(g_widgets.fan_slider, 24, 224);
    lv_obj_set_size(g_widgets.fan_slider, 452, 18);
    lv_slider_set_range(g_widgets.fan_slider, 0, 100);
    lv_obj_set_style_bg_color(g_widgets.fan_slider, lv_color_hex(COLOR_OFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(g_widgets.fan_slider, LV_OPA_30, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(g_widgets.fan_slider, 9, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(g_widgets.fan_slider, lv_color_hex(COLOR_PRIMARY), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(g_widgets.fan_slider, LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(g_widgets.fan_slider, 9, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(g_widgets.fan_slider, lv_color_hex(COLOR_ACCENT_COOL), LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(g_widgets.fan_slider, LV_OPA_COVER, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(g_widgets.fan_slider, LV_RADIUS_CIRCLE, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(g_widgets.fan_slider, fan_slider_event_cb, LV_EVENT_ALL, NULL);

    lv_obj_t *level_hint = lv_label_create(card);
    lv_label_set_text(level_hint, "低速               中速               高速");
    lv_obj_set_pos(level_hint, 24, 248);
    style_label(level_hint, COLOR_TEXT_SUB, FONT_CN);

    lv_obj_t *state = lv_label_create(card);
    lv_label_set_text(state, "数据位: HUM_SENSOR_VALUE(AHT20), FAN_CTRL_OUTPUT(GPIO41)");
    lv_obj_set_pos(state, 24, 282);
    style_label(state, COLOR_ACCENT_COOL, FONT_CN);
}

static void build_light_control_card(lv_obj_t *parent)
{
    lv_obj_t *card = create_card(parent, 0, 322, 500, 132);

    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text(title, "灯光控制");
    lv_obj_set_pos(title, 20, 10);
    style_label(title, COLOR_TEXT_SUB, FONT_CN);

    g_widgets.light_state_label = lv_label_create(card);
    lv_obj_set_pos(g_widgets.light_state_label, 120, 10);
    style_label(g_widgets.light_state_label, COLOR_TEXT_MAIN, FONT_CN);

    g_widgets.light_on_btn = lv_btn_create(card);
    lv_obj_set_pos(g_widgets.light_on_btn, 20, 44);
    lv_obj_set_size(g_widgets.light_on_btn, 220, 70);
    lv_obj_set_style_radius(g_widgets.light_on_btn, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(g_widgets.light_on_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    freeze_obj(g_widgets.light_on_btn);
    lv_obj_add_event_cb(g_widgets.light_on_btn, light_on_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *on_label = lv_label_create(g_widgets.light_on_btn);
    lv_label_set_text(on_label, "开灯");
    lv_obj_center(on_label);
    style_label(on_label, COLOR_TEXT_MAIN, FONT_CN);

    g_widgets.light_off_btn = lv_btn_create(card);
    lv_obj_set_pos(g_widgets.light_off_btn, 260, 44);
    lv_obj_set_size(g_widgets.light_off_btn, 220, 70);
    lv_obj_set_style_radius(g_widgets.light_off_btn, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(g_widgets.light_off_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    freeze_obj(g_widgets.light_off_btn);
    lv_obj_add_event_cb(g_widgets.light_off_btn, light_off_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *off_label = lv_label_create(g_widgets.light_off_btn);
    lv_label_set_text(off_label, "关灯");
    lv_obj_center(off_label);
    style_label(off_label, COLOR_TEXT_MAIN, FONT_CN);

    lv_obj_t *map = lv_label_create(card);
    lv_label_set_text(map, "数据位: LIGHT_CTRL_OUTPUT(GPIO40)");
    lv_obj_set_pos(map, 20, 116);
    style_label(map, COLOR_TEXT_SUB, FONT_CN);
}
static void build_detail_layer(lv_obj_t *root)
{
    g_widgets.detail_mask = lv_obj_create(root);
    lv_obj_set_pos(g_widgets.detail_mask, 0, 0);
    lv_obj_set_size(g_widgets.detail_mask, LCD_HOR_RES, LCD_VER_RES);
    set_obj_bg(g_widgets.detail_mask, 0x000000, LV_OPA_40);
    lv_obj_set_style_border_width(g_widgets.detail_mask, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    freeze_obj(g_widgets.detail_mask);
    lv_obj_add_flag(g_widgets.detail_mask, LV_OBJ_FLAG_HIDDEN);

    g_widgets.detail_panel = lv_obj_create(g_widgets.detail_mask);
    lv_obj_set_size(g_widgets.detail_panel, 860, 500);
    lv_obj_center(g_widgets.detail_panel);
    style_panel(g_widgets.detail_panel, COLOR_PANEL, 16);

    lv_obj_t *close_btn = lv_btn_create(g_widgets.detail_panel);
    lv_obj_set_pos(close_btn, 790, 14);
    lv_obj_set_size(close_btn, 56, 36);
    lv_obj_set_style_radius(close_btn, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(close_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    set_obj_bg(close_btn, COLOR_OFF, LV_OPA_COVER);
    freeze_obj(close_btn);
    lv_obj_add_event_cb(close_btn, detail_close_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *close_txt = lv_label_create(close_btn);
    lv_label_set_text(close_txt, "关闭");
    lv_obj_center(close_txt);
    style_label(close_txt, COLOR_TEXT_MAIN, FONT_CN);

    g_widgets.detail_title = lv_label_create(g_widgets.detail_panel);
    lv_obj_set_pos(g_widgets.detail_title, 24, 18);
    style_label(g_widgets.detail_title, COLOR_TEXT_MAIN, FONT_CN);

    g_widgets.detail_image_slot = create_image_slot(g_widgets.detail_panel, 24, 58, 190, 145);

    g_widgets.detail_image_hint = lv_label_create(g_widgets.detail_panel);
    lv_obj_set_pos(g_widgets.detail_image_hint, 24, 212);
    lv_obj_set_width(g_widgets.detail_image_hint, 810);
    style_label(g_widgets.detail_image_hint, COLOR_TEXT_SUB, FONT_CN);

    g_widgets.detail_desc = lv_label_create(g_widgets.detail_panel);
    lv_obj_set_pos(g_widgets.detail_desc, 230, 66);
    lv_obj_set_size(g_widgets.detail_desc, 600, 120);
    lv_label_set_long_mode(g_widgets.detail_desc, LV_LABEL_LONG_WRAP);
    style_label(g_widgets.detail_desc, COLOR_TEXT_MAIN, FONT_CN);

    g_widgets.detail_value = lv_label_create(g_widgets.detail_panel);
    lv_obj_set_pos(g_widgets.detail_value, 230, 196);
    style_label(g_widgets.detail_value, COLOR_ACCENT_COOL, FONT_CN);

    g_widgets.detail_data_map = lv_label_create(g_widgets.detail_panel);
    lv_obj_set_pos(g_widgets.detail_data_map, 24, 262);
    lv_obj_set_size(g_widgets.detail_data_map, 810, 92);
    lv_label_set_long_mode(g_widgets.detail_data_map, LV_LABEL_LONG_WRAP);
    style_label(g_widgets.detail_data_map, COLOR_TEXT_SUB, FONT_CN);

    g_widgets.detail_fan_slider_label = lv_label_create(g_widgets.detail_panel);
    lv_obj_set_pos(g_widgets.detail_fan_slider_label, 24, 366);
    style_label(g_widgets.detail_fan_slider_label, COLOR_TEXT_MAIN, FONT_CN);

    g_widgets.detail_fan_slider = lv_slider_create(g_widgets.detail_panel);
    lv_obj_set_pos(g_widgets.detail_fan_slider, 24, 396);
    lv_obj_set_size(g_widgets.detail_fan_slider, 810, 18);
    lv_slider_set_range(g_widgets.detail_fan_slider, 0, 100);
    lv_obj_set_style_bg_color(g_widgets.detail_fan_slider, lv_color_hex(COLOR_OFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(g_widgets.detail_fan_slider, LV_OPA_30, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(g_widgets.detail_fan_slider, 9, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(g_widgets.detail_fan_slider, lv_color_hex(COLOR_PRIMARY), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(g_widgets.detail_fan_slider, LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(g_widgets.detail_fan_slider, 9, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(g_widgets.detail_fan_slider, lv_color_hex(COLOR_ACCENT_COOL), LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(g_widgets.detail_fan_slider, LV_OPA_COVER, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(g_widgets.detail_fan_slider, LV_RADIUS_CIRCLE, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(g_widgets.detail_fan_slider, fan_slider_event_cb, LV_EVENT_ALL, NULL);

    g_widgets.detail_light_on_btn = lv_btn_create(g_widgets.detail_panel);
    lv_obj_set_pos(g_widgets.detail_light_on_btn, 24, 438);
    lv_obj_set_size(g_widgets.detail_light_on_btn, 396, 46);
    lv_obj_set_style_radius(g_widgets.detail_light_on_btn, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(g_widgets.detail_light_on_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(g_widgets.detail_light_on_btn, light_on_event_cb, LV_EVENT_CLICKED, NULL);
    freeze_obj(g_widgets.detail_light_on_btn);

    lv_obj_t *on_label = lv_label_create(g_widgets.detail_light_on_btn);
    lv_label_set_text(on_label, "开灯");
    lv_obj_center(on_label);
    style_label(on_label, COLOR_TEXT_MAIN, FONT_CN);

    g_widgets.detail_light_off_btn = lv_btn_create(g_widgets.detail_panel);
    lv_obj_set_pos(g_widgets.detail_light_off_btn, 438, 438);
    lv_obj_set_size(g_widgets.detail_light_off_btn, 396, 46);
    lv_obj_set_style_radius(g_widgets.detail_light_off_btn, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(g_widgets.detail_light_off_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(g_widgets.detail_light_off_btn, light_off_event_cb, LV_EVENT_CLICKED, NULL);
    freeze_obj(g_widgets.detail_light_off_btn);

    lv_obj_t *off_label = lv_label_create(g_widgets.detail_light_off_btn);
    lv_label_set_text(off_label, "关灯");
    lv_obj_center(off_label);
    style_label(off_label, COLOR_TEXT_MAIN, FONT_CN);
}

static void touch_deinit(void)
{
    if (touch_fd >= 0) {
        close(touch_fd);
        touch_fd = -1;
    }
}

static int touch_init(const char *dev)
{
    touch_fd = open(dev, O_RDONLY | O_NONBLOCK);
    return (touch_fd >= 0) ? 0 : -1;
}

static void touch_read(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    (void)drv;

    if (touch_fd < 0) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    struct input_event ev;
    ssize_t rb = 0;

    while ((rb = read(touch_fd, &ev, sizeof(ev))) > 0) {
        if (ev.type == EV_ABS) {
            if (ev.code == ABS_X || ev.code == ABS_MT_POSITION_X) {
                touch_x = ev.value;
            } else if (ev.code == ABS_Y || ev.code == ABS_MT_POSITION_Y) {
                touch_y = ev.value;
            }
        } else if (ev.type == EV_KEY && ev.code == BTN_TOUCH) {
            touch_pressed = ev.value ? 1 : 0;
        }
    }

    if (rb < 0 && errno != EAGAIN) {
        touch_pressed = 0;
    }

    int px = clamp_int(touch_x, TP_MIN_X, TP_MAX_X);
    int py = clamp_int(touch_y, TP_MIN_Y, TP_MAX_Y);

    px = map_value(px, TP_MIN_X, TP_MAX_X, 0, LCD_HOR_RES - 1);
    py = map_value(py, TP_MIN_Y, TP_MAX_Y, 0, LCD_VER_RES - 1);

    data->point.x = px;
    data->point.y = py;
    data->state = touch_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}
int app_hardware_init(void)
{
    led_ready = (led_init(LED_GPIO) == 0);
    fan_ready = (fan_init(FAN_GPIO) == 0);

    if (led_ready) {
        (void)led_off(LED_GPIO);
    }

    if (fan_ready) {
        (void)fan_off(FAN_GPIO);
    }

    return 0;
}

int app_lvgl_port_init(void)
{
    static lv_disp_drv_t disp_drv;
    static lv_indev_drv_t indev_drv;

    uint32_t fb_w = 0;
    uint32_t fb_h = 0;
    uint32_t fb_dpi = 0;

    fbdev_init();
    fbdev_get_sizes(&fb_w, &fb_h, &fb_dpi);

    if (fb_w == 0 || fb_h == 0) {
        fb_w = LCD_HOR_RES;
        fb_h = LCD_VER_RES;
    }

    buf1 = malloc(sizeof(lv_color_t) * fb_w * DISP_BUF_LINES);
    if (!buf1) {
        return -1;
    }

    lv_disp_draw_buf_init(&draw_buf, buf1, NULL, fb_w * DISP_BUF_LINES);

    lv_disp_drv_init(&disp_drv);
    disp_drv.draw_buf = &draw_buf;
    disp_drv.flush_cb = fbdev_flush;
    disp_drv.hor_res = fb_w;
    disp_drv.ver_res = fb_h;
    lv_disp_drv_register(&disp_drv);

    if (touch_init(TOUCH_DEV_PATH) == 0) {
        lv_indev_drv_init(&indev_drv);
        indev_drv.type = LV_INDEV_TYPE_POINTER;
        indev_drv.read_cb = touch_read;
        lv_indev_drv_register(&indev_drv);
    }

    return 0;
}

void app_lvgl_port_deinit(void)
{
    touch_deinit();

    if (buf1) {
        free(buf1);
        buf1 = NULL;
    }

    if (fan_ready) {
        (void)fan_off(FAN_GPIO);
    }

    if (led_ready) {
        (void)led_off(LED_GPIO);
    }
}

uint64_t app_get_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000ULL + tv.tv_usec / 1000;
}

void custom_build_screen(lv_ui *ui)
{
    memset(&g_widgets, 0, sizeof(g_widgets));

    ui->screen = lv_obj_create(NULL);
    lv_obj_set_size(ui->screen, LCD_HOR_RES, LCD_VER_RES);
    style_panel(ui->screen, COLOR_BG, 0);

    build_header(ui->screen);

    lv_obj_t *left = lv_obj_create(ui->screen);
    lv_obj_set_pos(left, 18 + UI_OFFSET_X, 88 + UI_OFFSET_Y);
    lv_obj_set_size(left, 520, 462);
    style_panel(left, COLOR_PANEL, 12);

    lv_obj_t *right = lv_obj_create(ui->screen);
    lv_obj_set_pos(right, 548 + UI_OFFSET_X, 88 + UI_OFFSET_Y);
    lv_obj_set_size(right, 500, 462);
    style_panel(right, COLOR_PANEL, 12);

    build_temperature_card(left);
    build_light_card(left);
    build_humidity_fan_card(right);
    build_light_control_card(right);
    build_detail_layer(ui->screen);

    lv_obj_t *footer = lv_obj_create(ui->screen);
    lv_obj_set_pos(footer, 18 + UI_OFFSET_X, 558);
    lv_obj_set_size(footer, 1030, 28);
    style_panel(footer, COLOR_PANEL, 10);

    lv_obj_t *footer_text = lv_label_create(footer);
    lv_label_set_text(footer_text, "模式: 手动   触摸: 已启用   数据: 模拟");
    lv_obj_set_pos(footer_text, 18, 6);
    style_label(footer_text, COLOR_TEXT_SUB, FONT_CN);
}

void custom_init(void)
{
    update_time_label();

    if (clock_timer) {
        lv_timer_del(clock_timer);
    }
    clock_timer = lv_timer_create(clock_timer_cb, 1000, NULL);

    if (sensor_sim_timer) {
        lv_timer_del(sensor_sim_timer);
    }
    sensor_sim_timer = lv_timer_create(sensor_sim_timer_cb, 1500, NULL);

    lv_slider_set_value(g_widgets.fan_slider, g_fan_speed, LV_ANIM_OFF);
    lv_slider_set_value(g_widgets.detail_fan_slider, g_fan_speed, LV_ANIM_OFF);

    g_light_on = false;
    apply_light_hardware();
    update_light_button_style();

    update_sensor_widgets();
    update_fan_state_labels();
    update_detail_layout();
}
