
#include "custom.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "board.h"
#include "fan.h"
#include "fbdev.h"
#include "gpio.h"
#include "aht20.h"
#include "bh1750.h"
#include "i2c_bus.h"

LV_FONT_DECLARE(lv_font_kaiti_16);
LV_IMG_DECLARE(_icon_title_alpha_60x50);
LV_IMG_DECLARE(_icon_temp_alpha_150x150);
LV_IMG_DECLARE(_icon_hum_alpha_150x150);
LV_IMG_DECLARE(_icon_fan_alpha_180x180);
LV_IMG_DECLARE(_icon_light_alpha_200x180);

#define DISP_BUF_LINES 80
#define TOUCH_DEV_PATH "/dev/input/event2"
#define TOUCH_HOLD_MS 100U
#define FB_SIZE_RETRY_COUNT 40
#define FB_SIZE_RETRY_DELAY_US 100000

#define TP_MIN_X 150
#define TP_MAX_X 4010
#define TP_MIN_Y 430
#define TP_MAX_Y 3780

#define TOUCH_SWAP_XY 0
#define TOUCH_INV_X 0
#define TOUCH_INV_Y 0

#define HIST_N 12

#define TOP_H 60
#define CONTENT_H 480
#define BOTTOM_H 60

#define C_BG 0x0B1526
#define C_PANEL 0x12233F
#define C_CARD 0x1A3158
#define C_MAIN 0xEAF2FF
#define C_SUB 0x9CB3D8
#define C_PRIMARY 0x2B6CFF
#define C_WARM 0xFFAF4D
#define C_COOL 0x2AD3F5
#define C_OK 0x1BC67A
#define C_OFF 0x4E6288

#define FONT_UI (&lv_font_kaiti_16)
#define FAN_AUTO_ON_TEMP 29
#define FAN_AUTO_OFF_TEMP 27
#define SENSOR_I2C_BUS 3
#define SENSOR_SAMPLE_PERIOD_MS 1000
#define DB_SAVE_INTERVAL_SEC 60
#define DB_MAX_RECORDS 1440
#define SENSOR_LOG_PATH "/tmp/env_sensor_log.csv"

typedef enum {
    PAGE_OVERVIEW = 0,
    PAGE_DETAIL,
    PAGE_MODE,
    PAGE_COUNT
} page_t;

typedef enum {
    MOD_TEMP = 0,
    MOD_HUM,
    MOD_FAN,
    MOD_LIGHT,
    MOD_SUMMARY,
    MOD_LAMP
} module_t;

typedef enum {
    MODE_MANUAL = 0,
    MODE_AUTO,
    MODE_ECO,
    MODE_NIGHT,
    MODE_DEMO,
    MODE_COUNT
} app_mode_t;

typedef struct {
    int temp;
    int hum;
    int lux;
    int fan;
    bool lamp_r_on;
    bool lamp_g_on;
    bool lamp_b_on;
    app_mode_t mode;
    module_t detail_mod;

    int hist_temp[HIST_N];
    int hist_hum[HIST_N];
    int hist_lux[HIST_N];
} app_state_t;

typedef struct {
    time_t ts;
    int temp;
    int hum;
    int lux;
    int fan;
    bool lamp_r_on;
    bool lamp_g_on;
    bool lamp_b_on;
    app_mode_t mode;
} db_record_t;

typedef struct {
    lv_obj_t *screen;
    lv_obj_t *pages[PAGE_COUNT];

    lv_obj_t *time;
    lv_obj_t *mode;
    lv_obj_t *status_led;

    lv_obj_t *ov_temp;
    lv_obj_t *ov_temp_hint;
    lv_obj_t *ov_hum;
    lv_obj_t *ov_hum_hint;
    lv_obj_t *ov_fan;
    lv_obj_t *ov_fan_state;
    lv_obj_t *ov_fan_on_btn;
    lv_obj_t *ov_fan_off_btn;
    lv_obj_t *ov_lux;
    lv_obj_t *ov_lux_hint;
    lv_obj_t *ov_sum;
    lv_obj_t *ov_lamp;
    lv_obj_t *ov_lamp_r_btn;
    lv_obj_t *ov_lamp_g_btn;
    lv_obj_t *ov_lamp_b_btn;
    lv_obj_t *ov_lamp_off_btn;

    lv_obj_t *ov_temp_chart;
    lv_chart_series_t *ov_temp_ser;
    lv_obj_t *ov_hum_chart;
    lv_chart_series_t *ov_hum_ser;

    lv_obj_t *dt_title;
    lv_obj_t *dt_curr;
    lv_obj_t *dt_stat;
    lv_obj_t *dt_tip;
    lv_obj_t *dt_log;
    lv_obj_t *dt_slider;
    lv_obj_t *dt_slider_value;
    lv_obj_t *dt_btn_on;
    lv_obj_t *dt_btn_off;
    lv_obj_t *dt_btn_aux;
    lv_obj_t *dt_chart;
    lv_chart_series_t *dt_ser;
    lv_obj_t *dt_axis_top;
    lv_obj_t *dt_axis_mid;
    lv_obj_t *dt_axis_bot;

    lv_obj_t *mode_current;
    lv_obj_t *mode_info;
    lv_obj_t *mode_btns[MODE_COUNT];
} ui_t;

static ui_t g_ui;
static app_state_t g_app = {
    .temp = 26,
    .hum = 52,
    .lux = 332,
    .fan = 0,
    .lamp_r_on = false,
    .lamp_g_on = false,
    .lamp_b_on = false,
    .mode = MODE_MANUAL,
    .detail_mod = MOD_TEMP,
    .hist_temp = {0},
    .hist_hum = {0},
    .hist_lux = {0},
};

static lv_disp_draw_buf_t g_draw_buf;
static lv_color_t *g_buf1;
static lv_coord_t g_w = 1024;
static lv_coord_t g_h = 600;

static int g_touch_fd = -1;
static int g_touch_x;
static int g_touch_y;
static int g_touch_pressed;
static uint64_t g_touch_last_abs_ms;
static int g_tp_min_x = TP_MIN_X;
static int g_tp_max_x = TP_MAX_X;
static int g_tp_min_y = TP_MIN_Y;
static int g_tp_max_y = TP_MAX_Y;

static bool g_lamp_ok;
static bool g_lamp_r_ok;
static bool g_lamp_g_ok;
static bool g_lamp_b_ok;
static bool g_fan_ok;
static bool g_i2c_ok;
static bool g_aht20_ok;
static bool g_bh1750_ok;
static lv_timer_t *g_clock_t;
static lv_timer_t *g_data_t;
static int g_hw_lamp_level = -1;
static int g_hw_fan_on = -1;
static i2c_bus_t g_i2c_bus = {.fd = -1, .bus_id = -1};
static FILE *g_sensor_log_fp;
static int g_last_raw_temp;
static int g_last_raw_hum;
static int g_last_raw_lux;
static char g_hist_time[HIST_N][9];
static db_record_t g_db[DB_MAX_RECORDS];
static int g_db_head;
static int g_db_count;
static time_t g_db_last_save_ts;

static void detail_fill_recent_log(module_t mod);
static bool lamp_any_on(void);
static void detail_log_set(const char *text);
static void btn_set_text(lv_obj_t *btn, const char *text);

static int clamp_i(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void hist_min_max(const int *arr, int n, int *out_min, int *out_max)
{
    int mn = arr[0];
    int mx = arr[0];
    for (int i = 1; i < n; ++i) {
        if (arr[i] < mn) mn = arr[i];
        if (arr[i] > mx) mx = arr[i];
    }
    if (out_min) *out_min = mn;
    if (out_max) *out_max = mx;
}

static int map_i(int v, int in_lo, int in_hi, int out_lo, int out_hi)
{
    if (in_hi == in_lo) return out_lo;
    return (v - in_lo) * (out_hi - out_lo) / (in_hi - in_lo) + out_lo;
}

static uint64_t now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)tv.tv_usec / 1000ULL;
}

static void freeze(lv_obj_t *obj)
{
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(obj, LV_DIR_NONE);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

static void set_bg(lv_obj_t *obj, uint32_t c, lv_opa_t opa)
{
    lv_obj_set_style_bg_opa(obj, opa, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(obj, lv_color_hex(c), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(obj, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void style_panel(lv_obj_t *obj, uint32_t c, lv_coord_t r)
{
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(obj, r, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    set_bg(obj, c, LV_OPA_COVER);
    freeze(obj);
}

static void style_label(lv_obj_t *obj, uint32_t c)
{
    lv_obj_set_style_text_color(obj, lv_color_hex(c), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(obj, FONT_UI, LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void style_btn(lv_obj_t *btn, uint32_t c)
{
    style_panel(btn, c, 10);
    lv_obj_set_style_bg_opa(btn, LV_OPA_80, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_translate_y(btn, 2, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_outline_width(btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_outline_width(btn, 2, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_outline_color(btn, lv_color_hex(0x9CC8FF), LV_PART_MAIN | LV_STATE_PRESSED);
}

static bool is_press_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    return (code == LV_EVENT_CLICKED);
}

static void refresh_lamp_btn_state(void)
{
    if (g_ui.ov_lamp_r_btn) {
        set_bg(g_ui.ov_lamp_r_btn, g_app.lamp_r_on ? 0xD64545 : C_OFF, LV_OPA_COVER);
    }
    if (g_ui.ov_lamp_g_btn) {
        set_bg(g_ui.ov_lamp_g_btn, g_app.lamp_g_on ? 0x2FAF5A : C_OFF, LV_OPA_COVER);
    }
    if (g_ui.ov_lamp_b_btn) {
        set_bg(g_ui.ov_lamp_b_btn, g_app.lamp_b_on ? 0x3A74E8 : C_OFF, LV_OPA_COVER);
    }
    if (g_ui.ov_lamp_off_btn) {
        set_bg(g_ui.ov_lamp_off_btn, lamp_any_on() ? C_OFF : C_PRIMARY, LV_OPA_COVER);
    }

    if (g_ui.dt_btn_on) {
        set_bg(g_ui.dt_btn_on, g_app.lamp_r_on ? 0xD64545 : C_OFF, LV_OPA_COVER);
    }
    if (g_ui.dt_btn_off) {
        set_bg(g_ui.dt_btn_off, g_app.lamp_g_on ? 0x2FAF5A : C_OFF, LV_OPA_COVER);
    }
    if (g_ui.dt_btn_aux) {
        set_bg(g_ui.dt_btn_aux, g_app.lamp_b_on ? 0x3A74E8 : C_OFF, LV_OPA_COVER);
    }
}

static void refresh_fan_btn_state(void)
{
    if (g_ui.ov_fan_on_btn && g_ui.ov_fan_off_btn) {
        set_bg(g_ui.ov_fan_on_btn, g_app.fan > 0 ? C_PRIMARY : C_OFF, LV_OPA_COVER);
        set_bg(g_ui.ov_fan_off_btn, g_app.fan > 0 ? C_OFF : C_PRIMARY, LV_OPA_COVER);
    }
}

static void auto_update_fan_by_temp(void)
{
    if (g_app.mode == MODE_MANUAL) return;

    if (g_app.mode == MODE_ECO) {
        /* Minimum-energy: fan only runs when heat/humidity is clearly high. */
        if (g_app.temp >= 32 || g_app.hum >= 78) g_app.fan = 1;
        else if (g_app.temp <= 29 && g_app.hum <= 72) g_app.fan = 0;
        return;
    }

    if (g_app.mode == MODE_NIGHT) {
        /* Night mode prefers quiet operation. */
        if (g_app.temp >= 33 || g_app.hum >= 80) g_app.fan = 1;
        else if (g_app.temp <= 30 && g_app.hum <= 74) g_app.fan = 0;
        return;
    }

    /* Auto/Balance mode: coordinated by temperature + humidity + illumination. */
    if (g_app.temp >= 31 || g_app.hum >= 75) g_app.fan = 1;
    else if (g_app.temp <= 28 && g_app.hum <= 68 && g_app.lux >= 60) g_app.fan = 0;
}

static bool lamp_any_on(void)
{
    return g_app.lamp_r_on || g_app.lamp_g_on || g_app.lamp_b_on;
}

static void lamp_set_rgb(bool r, bool g, bool b)
{
    g_app.lamp_r_on = r;
    g_app.lamp_g_on = g;
    g_app.lamp_b_on = b;
}

static void auto_update_lamp_by_env(void)
{
    if (g_app.mode == MODE_MANUAL) return;

    bool r = false;
    bool g = false;
    bool b = false;

    if (g_app.mode == MODE_ECO) {
        /* ECO uses one channel at low brightness-equivalent behavior. */
        if (g_app.lux < 80) g = true;
    } else if (g_app.mode == MODE_NIGHT) {
        if (g_app.lux < 220) b = true;
    } else {
        if (g_app.temp >= 31) r = true;
        if (g_app.hum >= 75) b = true;
        if (g_app.lux < 120) g = true;
    }

    if (!r && !g && !b && g_app.lux < 70) g = true;
    lamp_set_rgb(r, g, b);
}

static const char *lamp_state_text(void)
{
    if (g_app.lamp_r_on && g_app.lamp_g_on && g_app.lamp_b_on) return "RGB";
    if (g_app.lamp_r_on && g_app.lamp_g_on) return "RG";
    if (g_app.lamp_r_on && g_app.lamp_b_on) return "RB";
    if (g_app.lamp_g_on && g_app.lamp_b_on) return "GB";
    if (g_app.lamp_r_on) return "R";
    if (g_app.lamp_g_on) return "G";
    if (g_app.lamp_b_on) return "B";
    return "OFF";
}

static void sensor_log_close(void)
{
    if (g_sensor_log_fp) {
        fclose(g_sensor_log_fp);
        g_sensor_log_fp = NULL;
    }
}

static void sensor_log_open(void)
{
    if (g_sensor_log_fp) return;

    g_sensor_log_fp = fopen(SENSOR_LOG_PATH, "a+");
    if (!g_sensor_log_fp) {
        fprintf(stderr, "[sensor] open log failed: %s\n", SENSOR_LOG_PATH);
        return;
    }

    if (fseek(g_sensor_log_fp, 0, SEEK_END) == 0) {
        long sz = ftell(g_sensor_log_fp);
        if (sz == 0) {
            fprintf(g_sensor_log_fp,
                    "time,temp_c,hum_pct,lux,fan,r,g,b,mode\n");
            fflush(g_sensor_log_fp);
        }
    }
    printf("[sensor] logging to %s\n", SENSOR_LOG_PATH);
}

static void db_reset(void)
{
    memset(g_db, 0, sizeof(g_db));
    g_db_head = 0;
    g_db_count = 0;
    g_db_last_save_ts = 0;
}

static void db_push_current(time_t now_ts)
{
    if (g_db_last_save_ts != 0 && (now_ts - g_db_last_save_ts) < DB_SAVE_INTERVAL_SEC) return;

    db_record_t *rec = &g_db[g_db_head];
    rec->ts = now_ts;
    rec->temp = g_app.temp;
    rec->hum = g_app.hum;
    rec->lux = g_app.lux;
    rec->fan = g_app.fan;
    rec->lamp_r_on = g_app.lamp_r_on;
    rec->lamp_g_on = g_app.lamp_g_on;
    rec->lamp_b_on = g_app.lamp_b_on;
    rec->mode = g_app.mode;

    g_db_head = (g_db_head + 1) % DB_MAX_RECORDS;
    if (g_db_count < DB_MAX_RECORDS) g_db_count++;
    g_db_last_save_ts = now_ts;

    if (g_sensor_log_fp) {
        struct tm tmv;
        char tbuf[32];
        localtime_r(&rec->ts, &tmv);
        strftime(tbuf, sizeof(tbuf), "%F %T", &tmv);
        fprintf(g_sensor_log_fp,
                "%s,%d,%d,%d,%d,%d,%d,%d,%d\n",
                tbuf,
                rec->temp,
                rec->hum,
                rec->lux,
                rec->fan,
                rec->lamp_r_on ? 1 : 0,
                rec->lamp_g_on ? 1 : 0,
                rec->lamp_b_on ? 1 : 0,
                (int)rec->mode);
        fflush(g_sensor_log_fp);
    }
}
static const char *mode_name(app_mode_t m)
{
    switch (m) {
    case MODE_MANUAL: return "手动模式";
    case MODE_AUTO: return "自动模式";
    case MODE_ECO: return "节能模式";
    case MODE_NIGHT: return "夜间模式";
    case MODE_DEMO: return "均衡模式";
    default: return "未知模式";
    }
}

static const char *fan_level(int speed)
{
    if (speed <= 0) return "关闭";
    return "开启";
}

static const lv_img_dsc_t *module_img(module_t m)
{
    switch (m) {
    case MOD_TEMP: return &_icon_temp_alpha_150x150;
    case MOD_HUM: return &_icon_hum_alpha_150x150;
    case MOD_FAN: return &_icon_fan_alpha_180x180;
    case MOD_LIGHT: return &_icon_light_alpha_200x180;
    case MOD_LAMP: return &_icon_light_alpha_200x180;
    default: return NULL;
    }
}

static void chart_set(lv_obj_t *chart, lv_chart_series_t *ser, const int *arr, int n)
{
    if (!chart || !ser || !arr || n <= 0) return;
    lv_chart_set_point_count(chart, (uint16_t)n);
    for (int i = 0; i < n; ++i) {
        lv_chart_set_value_by_id(chart, ser, (uint16_t)i, arr[i]);
    }
    lv_chart_refresh(chart);
}

static lv_obj_t *add_icon(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h, module_t m)
{
    lv_obj_t *slot = lv_obj_create(parent);
    lv_obj_set_pos(slot, x, y);
    lv_obj_set_size(slot, w, h);
    style_panel(slot, C_CARD, 8);
    set_bg(slot, C_CARD, LV_OPA_TRANSP);
    lv_obj_clear_flag(slot, LV_OBJ_FLAG_CLICKABLE);

    const lv_img_dsc_t *dsc = module_img(m);
    if (dsc) {
        lv_obj_t *img = lv_img_create(slot);
        lv_img_set_src(img, dsc);
        lv_obj_center(img);

        if (dsc->header.w > 0 && dsc->header.h > 0) {
            uint32_t zw = ((uint32_t)(w - 8) * 256U) / dsc->header.w;
            uint32_t zh = ((uint32_t)(h - 8) * 256U) / dsc->header.h;
            uint32_t z = (zw < zh) ? zw : zh;
            if (z > 256U) z = 256U;
            if (z < 80U) z = 80U;
            lv_img_set_zoom(img, (uint16_t)z);
        }
    }
    return slot;
}

static lv_obj_t *mk_card(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_set_pos(c, x, y);
    lv_obj_set_size(c, w, h);
    style_panel(c, C_CARD, 16);
    return c;
}

static void show_page(page_t p)
{
    for (int i = 0; i < PAGE_COUNT; ++i) {
        if (!g_ui.pages[i]) continue;
        if (i == p) lv_obj_clear_flag(g_ui.pages[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(g_ui.pages[i], LV_OBJ_FLAG_HIDDEN);
    }
}

static void apply_hw(void)
{
    int lamp_r = g_app.lamp_r_on ? LAMP_ON_LEVEL : LAMP_OFF_LEVEL;
    int lamp_g = g_app.lamp_g_on ? LAMP_ON_LEVEL : LAMP_OFF_LEVEL;
    int lamp_b = g_app.lamp_b_on ? LAMP_ON_LEVEL : LAMP_OFF_LEVEL;
    int fan_running = (g_app.fan > 0) ? 1 : 0;

    if (g_lamp_ok) {
        if (g_lamp_r_ok) (void)gpio_write(LAMP_R_GPIO, lamp_r);
        if (g_lamp_g_ok) (void)gpio_write(LAMP_G_GPIO, lamp_g);
        if (g_lamp_b_ok) (void)gpio_write(LAMP_B_GPIO, lamp_b);
        g_hw_lamp_level = (lamp_r ? 1 : 0) | (lamp_g ? 2 : 0) | (lamp_b ? 4 : 0);
    }

    if (!g_fan_ok) {
        g_fan_ok = (fan_init(FAN_GPIO) == 0);
    }

    if (g_fan_ok) {
        if (fan_running != g_hw_fan_on) {
            int rc = fan_running ? fan_on(FAN_GPIO) : fan_off(FAN_GPIO);
            if (rc < 0) {
                /* Re-detect fan0/fan1 and retry once. */
                g_fan_ok = (fan_init(FAN_GPIO) == 0);
                if (g_fan_ok) {
                    rc = fan_running ? fan_on(FAN_GPIO) : fan_off(FAN_GPIO);
                }
            }
            if (rc < 0) {
                fprintf(stderr, "[fan] control failed on both fan0/fan1\n");
                g_fan_ok = false;
            } else {
                g_hw_fan_on = fan_running;
            }
        }
    }
}

static void refresh_time(void)
{
    if (!g_ui.time) return;
    char tbuf[16];
    time_t t = time(NULL);
    struct tm *tmv = localtime(&t);
    if (!tmv) {
        lv_label_set_text(g_ui.time, "--:--:--");
        return;
    }
    strftime(tbuf, sizeof(tbuf), "%H:%M:%S", tmv);
    lv_label_set_text(g_ui.time, tbuf);
}

static void refresh_overview(bool with_charts)
{
    char b[96];
    int tmin = 0, tmax = 0, hmin = 0, hmax = 0, lmin = 0, lmax = 0;

    hist_min_max(g_app.hist_temp, HIST_N, &tmin, &tmax);
    hist_min_max(g_app.hist_hum, HIST_N, &hmin, &hmax);
    hist_min_max(g_app.hist_lux, HIST_N, &lmin, &lmax);

    if (g_ui.ov_temp) {
        snprintf(b, sizeof(b), "%d°C (row %d)", g_app.temp, g_last_raw_temp);
        lv_label_set_text(g_ui.ov_temp, b);
    }
    if (g_ui.ov_temp_hint) {
        snprintf(b, sizeof(b), "阈值:%d°C  峰值:%d°C", FAN_AUTO_ON_TEMP, tmax);
        lv_label_set_text(g_ui.ov_temp_hint, b);
    }
    if (g_ui.ov_hum) {
        snprintf(b, sizeof(b), "%d%% (row %d)", g_app.hum, g_last_raw_hum);
        lv_label_set_text(g_ui.ov_hum, b);
    }
    if (g_ui.ov_hum_hint) {
        snprintf(b, sizeof(b), "阈值:75%%  峰值:%d%%", hmax);
        lv_label_set_text(g_ui.ov_hum_hint, b);
    }
    if (g_ui.ov_fan) {
        lv_label_set_text(g_ui.ov_fan, fan_level(g_app.fan));
    }
    if (g_ui.ov_fan_state) {
        lv_label_set_text(g_ui.ov_fan_state, g_app.fan > 0 ? "当前状态: 运行中" : "当前状态: 已停止");
    }
    if (g_ui.ov_lux) {
        snprintf(b, sizeof(b), "%d LUX (row %d)", g_app.lux, g_last_raw_lux);
        lv_label_set_text(g_ui.ov_lux, b);
    }
    if (g_ui.ov_lux_hint) {
        snprintf(b, sizeof(b), "阈值:120 LUX  峰值:%d", lmax);
        lv_label_set_text(g_ui.ov_lux_hint, b);
    }
    if (g_ui.ov_lamp) {
        snprintf(b, sizeof(b), "RGB灯: %s", lamp_state_text());
        lv_label_set_text(g_ui.ov_lamp, b);
    }
    if (g_ui.ov_sum) {
        snprintf(b, sizeof(b), "模式:%s  风扇:%s  灯光:%s",
                 mode_name(g_app.mode),
                 g_app.fan ? "开启" : "关闭",
                 lamp_state_text());
        lv_label_set_text(g_ui.ov_sum, b);
    }

    if (with_charts) {
        if (g_ui.ov_temp_chart && g_ui.ov_temp_ser) {
            lv_chart_set_range(g_ui.ov_temp_chart, LV_CHART_AXIS_PRIMARY_Y, 20, 36);
            chart_set(g_ui.ov_temp_chart, g_ui.ov_temp_ser, g_app.hist_temp, HIST_N);
        }
        if (g_ui.ov_hum_chart && g_ui.ov_hum_ser) {
            lv_chart_set_range(g_ui.ov_hum_chart, LV_CHART_AXIS_PRIMARY_Y, 35, 80);
            chart_set(g_ui.ov_hum_chart, g_ui.ov_hum_ser, g_app.hist_hum, HIST_N);
        }
    }
}
static void refresh_detail(bool with_chart)
{
    char b[128];
    const int *hist = g_app.hist_temp;
    int ymin = 0;
    int ymax = 100;

    if (!g_ui.dt_title) return;

    switch (g_app.detail_mod) {
    case MOD_TEMP:
        lv_label_set_text(g_ui.dt_title, "温度详情");
        snprintf(b, sizeof(b), "%d°C", g_app.temp);
        lv_label_set_text(g_ui.dt_curr, b);
        snprintf(b, sizeof(b), "当前值:%d°C  原始值:%d°C", g_app.temp, g_last_raw_temp);
        lv_label_set_text(g_ui.dt_stat, b);
        lv_label_set_text(g_ui.dt_tip, "数据源:AHT20；当前为只读采集，控制功能预留。");
        lv_obj_add_flag(g_ui.dt_slider, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_ui.dt_slider_value, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_ui.dt_btn_on, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_ui.dt_btn_off, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_ui.dt_btn_aux, LV_OBJ_FLAG_HIDDEN);
        hist = g_app.hist_temp;
        ymin = 20;
        ymax = 36;
        detail_fill_recent_log(MOD_TEMP);
        break;
    case MOD_HUM:
        lv_label_set_text(g_ui.dt_title, "湿度详情");
        snprintf(b, sizeof(b), "%d%%", g_app.hum);
        lv_label_set_text(g_ui.dt_curr, b);
        snprintf(b, sizeof(b), "当前值:%d%%  原始值:%d%%", g_app.hum, g_last_raw_hum);
        lv_label_set_text(g_ui.dt_stat, b);
        lv_label_set_text(g_ui.dt_tip, "数据源:AHT20；当前为只读采集，控制功能预留。");
        lv_obj_add_flag(g_ui.dt_slider, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_ui.dt_slider_value, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_ui.dt_btn_on, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_ui.dt_btn_off, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_ui.dt_btn_aux, LV_OBJ_FLAG_HIDDEN);
        hist = g_app.hist_hum;
        ymin = 35;
        ymax = 80;
        detail_fill_recent_log(MOD_HUM);
        break;
    case MOD_LIGHT:
        lv_label_set_text(g_ui.dt_title, "光照详情");
        snprintf(b, sizeof(b), "%d LUX", g_app.lux);
        lv_label_set_text(g_ui.dt_curr, b);
        snprintf(b, sizeof(b), "当前值:%d LUX  原始值:%d LUX", g_app.lux, g_last_raw_lux);
        lv_label_set_text(g_ui.dt_stat, b);
        lv_label_set_text(g_ui.dt_tip, "数据源:BH1750；当前为只读采集，控制功能预留。");
        lv_obj_add_flag(g_ui.dt_slider, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_ui.dt_slider_value, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_ui.dt_btn_on, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_ui.dt_btn_off, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_ui.dt_btn_aux, LV_OBJ_FLAG_HIDDEN);
        hist = g_app.hist_lux;
        ymin = 0;
        ymax = 1000;
        detail_fill_recent_log(MOD_LIGHT);
        break;
    case MOD_FAN:
        lv_label_set_text(g_ui.dt_title, "风扇详情");
        lv_label_set_text(g_ui.dt_curr, g_app.fan > 0 ? "已开启" : "已关闭");
        snprintf(b, sizeof(b), "当前通道:%s", fan_get_channel());
        lv_label_set_text(g_ui.dt_stat, b);
        lv_label_set_text(g_ui.dt_tip, "仅支持风扇开/关控制；速度调节预留。");
        detail_fill_recent_log(MOD_FAN);
        lv_obj_add_flag(g_ui.dt_slider, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_ui.dt_slider_value, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(g_ui.dt_btn_on, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(g_ui.dt_btn_off, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_ui.dt_btn_aux, LV_OBJ_FLAG_HIDDEN);
        btn_set_text(g_ui.dt_btn_on, "风扇开");
        btn_set_text(g_ui.dt_btn_off, "风扇关");
        hist = g_app.hist_temp;
        ymin = 20;
        ymax = 36;
        break;
    case MOD_LAMP:
    default:
        lv_label_set_text(g_ui.dt_title, "灯光详情");
        snprintf(b, sizeof(b), "RGB状态:%s", lamp_state_text());
        lv_label_set_text(g_ui.dt_curr, b);
        lv_label_set_text(g_ui.dt_stat, "红(GPIO117) 绿(GPIO116) 蓝(GPIO109)");
        lv_label_set_text(g_ui.dt_tip, "手动模式可单独切换；自动模式按环境联动。");
        detail_fill_recent_log(MOD_LAMP);
        lv_obj_add_flag(g_ui.dt_slider, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_ui.dt_slider_value, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(g_ui.dt_btn_on, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(g_ui.dt_btn_off, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(g_ui.dt_btn_aux, LV_OBJ_FLAG_HIDDEN);
        btn_set_text(g_ui.dt_btn_on, "R");
        btn_set_text(g_ui.dt_btn_off, "G");
        btn_set_text(g_ui.dt_btn_aux, "B");
        hist = g_app.hist_lux;
        ymin = 0;
        ymax = 1000;
        break;
    }

    if (!lv_obj_has_flag(g_ui.dt_slider, LV_OBJ_FLAG_HIDDEN)) {
        int v = lv_slider_get_value(g_ui.dt_slider);
        snprintf(b, sizeof(b), "当前设置: %d", v);
        lv_label_set_text(g_ui.dt_slider_value, b);
    }

    if (g_ui.dt_axis_top && g_ui.dt_axis_mid && g_ui.dt_axis_bot) {
        int mid = (ymin + ymax) / 2;
        snprintf(b, sizeof(b), "%d", ymax);
        lv_label_set_text(g_ui.dt_axis_top, b);
        snprintf(b, sizeof(b), "%d", mid);
        lv_label_set_text(g_ui.dt_axis_mid, b);
        snprintf(b, sizeof(b), "%d", ymin);
        lv_label_set_text(g_ui.dt_axis_bot, b);
    }

    if (with_chart) {
        lv_chart_set_range(g_ui.dt_chart, LV_CHART_AXIS_PRIMARY_Y, ymin, ymax);
        chart_set(g_ui.dt_chart, g_ui.dt_ser, hist, HIST_N);
    }
}

static void refresh_mode(void)
{
    if (!g_ui.mode_current) return;

    char b[64];
    snprintf(b, sizeof(b), "当前模式: %s", mode_name(g_app.mode));
    lv_label_set_text(g_ui.mode_current, b);

    if (g_ui.mode_info) {
        snprintf(b, sizeof(b), "风扇通道:%s  历史条目:%d  当前状态:风扇%s/RGB-%s",
                 fan_get_channel(),
                 g_db_count,
                 g_app.fan ? "开" : "关",
                 lamp_state_text());
        lv_label_set_text(g_ui.mode_info, b);
    }

    for (int i = 0; i < MODE_COUNT; ++i) {
        if (!g_ui.mode_btns[i]) continue;
        set_bg(g_ui.mode_btns[i], i == g_app.mode ? C_PRIMARY : C_OFF, LV_OPA_COVER);
    }
}

static void refresh_all(void)
{
    refresh_time();
    refresh_overview(true);
    refresh_detail(true);
    refresh_mode();
    refresh_lamp_btn_state();
    refresh_fan_btn_state();

    if (g_ui.mode) {
        lv_label_set_text(g_ui.mode, mode_name(g_app.mode));
    }
    if (g_ui.status_led) {
        lv_led_set_color(g_ui.status_led, lv_color_hex(g_app.mode == MODE_MANUAL ? C_OFF : C_OK));
    }
}

static void refresh_quick(void)
{
    refresh_time();
    refresh_overview(false);
    refresh_detail(false);
    refresh_mode();
    refresh_lamp_btn_state();
    refresh_fan_btn_state();

    if (g_ui.mode) {
        lv_label_set_text(g_ui.mode, mode_name(g_app.mode));
    }
    if (g_ui.status_led) {
        lv_led_set_color(g_ui.status_led, lv_color_hex(g_app.mode == MODE_MANUAL ? C_OFF : C_OK));
    }
}

static void detail_log_set(const char *text)
{
    if (!g_ui.dt_log) return;
    lv_textarea_set_text(g_ui.dt_log, text ? text : "");
}

static void btn_set_text(lv_obj_t *btn, const char *text)
{
    if (!btn || !text) return;
    lv_obj_t *child = lv_obj_get_child(btn, 0);
    if (child) lv_label_set_text(child, text);
}

static void ev_nav_overview(lv_event_t *e)
{
    if (!is_press_event(e)) return;
    show_page(PAGE_OVERVIEW);
    refresh_all();
}

static void ev_nav_mode(lv_event_t *e)
{
    if (!is_press_event(e)) return;
    show_page(PAGE_MODE);
    refresh_all();
}

static void ev_open_detail(lv_event_t *e)
{
    if (!is_press_event(e)) return;
    g_app.detail_mod = (module_t)(intptr_t)lv_event_get_user_data(e);
    show_page(PAGE_DETAIL);
    refresh_all();
}

static void ev_slider(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;

    int v = lv_slider_get_value(lv_event_get_target(e));
    if (g_app.detail_mod == MOD_FAN) {
        g_app.fan = (v > 0) ? 1 : 0;
    }
    apply_hw();
    refresh_quick();
}

static void ev_lamp_toggle(lv_event_t *e)
{
    if (!is_press_event(e)) return;
    lv_obj_t *target = lv_event_get_target(e);
    int ch = (int)(intptr_t)lv_event_get_user_data(e);

    if (g_app.detail_mod == MOD_FAN && (target == g_ui.dt_btn_on || target == g_ui.dt_btn_off)) {
        g_app.mode = MODE_MANUAL;
        g_app.fan = (target == g_ui.dt_btn_on) ? 1 : 0;
        apply_hw();
        refresh_quick();
        return;
    }

    if (ch == 0) g_app.lamp_r_on = !g_app.lamp_r_on;
    else if (ch == 1) g_app.lamp_g_on = !g_app.lamp_g_on;
    else if (ch == 2) g_app.lamp_b_on = !g_app.lamp_b_on;

    g_app.mode = MODE_MANUAL;
    apply_hw();
    refresh_quick();
}

static void ev_lamp_all_off(lv_event_t *e)
{
    if (!is_press_event(e)) return;
    lamp_set_rgb(false, false, false);
    g_app.mode = MODE_MANUAL;
    apply_hw();
    refresh_quick();
}

static void ev_fan_on(lv_event_t *e)
{
    if (!is_press_event(e)) return;
    if (g_app.fan == 1) return;
    g_app.mode = MODE_MANUAL;
    g_app.fan = 1;
    apply_hw();
    refresh_quick();
}

static void ev_fan_off(lv_event_t *e)
{
    if (!is_press_event(e)) return;
    if (g_app.fan == 0) return;
    g_app.mode = MODE_MANUAL;
    g_app.fan = 0;
    apply_hw();
    refresh_quick();
}

static void ev_mode_pick(lv_event_t *e)
{
    if (!is_press_event(e)) return;

    g_app.mode = (app_mode_t)(intptr_t)lv_event_get_user_data(e);
    auto_update_fan_by_temp();
    auto_update_lamp_by_env();
    apply_hw();
    refresh_quick();
}
static lv_obj_t *mk_detail_btn(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, module_t m)
{
    lv_obj_t *b = lv_btn_create(parent);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, 76, 34);
    style_btn(b, C_PRIMARY);
    lv_obj_add_event_cb(b, ev_open_detail, LV_EVENT_CLICKED, (void *)(intptr_t)m);

    lv_obj_t *t = lv_label_create(b);
    lv_label_set_text(t, "详情");
    style_label(t, C_MAIN);
    lv_obj_center(t);
    return b;
}

static void build_top(lv_obj_t *root)
{
    lv_obj_t *bar = lv_obj_create(root);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_size(bar, g_w, TOP_H);
    style_panel(bar, C_PANEL, 0);

    lv_obj_t *icon = lv_obj_create(bar);
    lv_obj_set_pos(icon, 14, 6);
    lv_obj_set_size(icon, 48, 48);
    style_panel(icon, C_CARD, 10);
    lv_obj_t *im = lv_img_create(icon);
    lv_img_set_src(im, &_icon_title_alpha_60x50);
    lv_obj_center(im);

    lv_obj_t *title = lv_label_create(bar);
    lv_label_set_text(title, "智能环境控制系统");
    style_label(title, C_MAIN);
    lv_obj_set_pos(title, 74, 17);

    g_ui.status_led = lv_led_create(bar);
    lv_obj_set_size(g_ui.status_led, 14, 14);
    lv_obj_set_pos(g_ui.status_led, g_w - 380, 23);

    g_ui.time = lv_label_create(bar);
    style_label(g_ui.time, C_MAIN);
    lv_obj_set_pos(g_ui.time, g_w - 360, 19);

    g_ui.mode = lv_label_create(bar);
    style_label(g_ui.mode, C_SUB);
    lv_obj_set_pos(g_ui.mode, g_w - 280, 19);

    lv_obj_t *btn_mode = lv_btn_create(bar);
    lv_obj_set_pos(btn_mode, g_w - 180, 12);
    lv_obj_set_size(btn_mode, 104, 36);
    style_btn(btn_mode, C_PRIMARY);
    lv_obj_add_event_cb(btn_mode, ev_nav_mode, LV_EVENT_CLICKED, NULL);
    lv_obj_t *m_txt = lv_label_create(btn_mode);
    lv_label_set_text(m_txt, "模式管理");
    style_label(m_txt, C_MAIN);
    lv_obj_center(m_txt);

    lv_obj_t *btn_home = lv_btn_create(bar);
    lv_obj_set_pos(btn_home, g_w - 68, 12);
    lv_obj_set_size(btn_home, 56, 36);
    style_btn(btn_home, C_OFF);
    lv_obj_add_event_cb(btn_home, ev_nav_overview, LV_EVENT_CLICKED, NULL);
    lv_obj_t *h_txt = lv_label_create(btn_home);
    lv_label_set_text(h_txt, "首页");
    style_label(h_txt, C_MAIN);
    lv_obj_center(h_txt);
}

static void build_overview(lv_obj_t *root)
{
    lv_obj_t *v = lv_obj_create(root);
    lv_obj_set_pos(v, 0, TOP_H);
    lv_obj_set_size(v, g_w, CONTENT_H);
    style_panel(v, C_PANEL, 0);
    g_ui.pages[PAGE_OVERVIEW] = v;

    lv_coord_t x0 = 16;
    lv_coord_t y0 = 12;
    lv_coord_t gap = 12;
    lv_coord_t colw = (g_w - x0 * 2 - gap * 2) / 3;
    lv_coord_t rowh = (CONTENT_H - y0 * 2 - gap) / 2;

    lv_obj_t *c1 = mk_card(v, x0 + (colw + gap) * 0, y0, colw, rowh);
    lv_obj_t *c2 = mk_card(v, x0 + (colw + gap) * 1, y0, colw, rowh);
    lv_obj_t *c3 = mk_card(v, x0 + (colw + gap) * 2, y0, colw, rowh);
    lv_obj_t *c4 = mk_card(v, x0 + (colw + gap) * 0, y0 + rowh + gap, colw, rowh);
    lv_obj_t *c5 = mk_card(v, x0 + (colw + gap) * 1, y0 + rowh + gap, colw, rowh);
    lv_obj_t *c6 = mk_card(v, x0 + (colw + gap) * 2, y0 + rowh + gap, colw, rowh);

    lv_obj_t *t = lv_label_create(c1);
    lv_label_set_text(t, "温度");
    style_label(t, C_MAIN);
    lv_obj_set_pos(t, 14, 10);
    mk_detail_btn(c1, colw - 90, 8, MOD_TEMP);
    add_icon(c1, colw - 142, 52, 126, 126, MOD_TEMP);
    g_ui.ov_temp = lv_label_create(c1);
    style_label(g_ui.ov_temp, C_MAIN);
    lv_obj_set_pos(g_ui.ov_temp, 14, 58);
    lv_obj_t *temp_hint = lv_label_create(c1);
    g_ui.ov_temp_hint = temp_hint;
    lv_label_set_text(temp_hint, "传感器: AHT20");
    style_label(temp_hint, C_SUB);
    lv_obj_set_pos(temp_hint, 14, 84);
    g_ui.ov_temp_chart = lv_chart_create(c1);
    lv_obj_set_pos(g_ui.ov_temp_chart, 14, rowh - 58);
    lv_obj_set_size(g_ui.ov_temp_chart, colw - 156, 42);
    lv_chart_set_type(g_ui.ov_temp_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_div_line_count(g_ui.ov_temp_chart, 1, 0);
    lv_obj_set_style_line_width(g_ui.ov_temp_chart, 3, LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_size(g_ui.ov_temp_chart, 2, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(g_ui.ov_temp_chart, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    set_bg(g_ui.ov_temp_chart, C_CARD, LV_OPA_TRANSP);
    g_ui.ov_temp_ser = lv_chart_add_series(g_ui.ov_temp_chart, lv_color_hex(C_WARM), LV_CHART_AXIS_PRIMARY_Y);

    t = lv_label_create(c2);
    lv_label_set_text(t, "湿度");
    style_label(t, C_MAIN);
    lv_obj_set_pos(t, 14, 10);
    mk_detail_btn(c2, colw - 90, 8, MOD_HUM);
    add_icon(c2, colw - 142, 52, 126, 126, MOD_HUM);
    g_ui.ov_hum = lv_label_create(c2);
    style_label(g_ui.ov_hum, C_MAIN);
    lv_obj_set_pos(g_ui.ov_hum, 14, 58);
    lv_obj_t *hum_hint = lv_label_create(c2);
    g_ui.ov_hum_hint = hum_hint;
    lv_label_set_text(hum_hint, "传感器: AHT20");
    style_label(hum_hint, C_SUB);
    lv_obj_set_pos(hum_hint, 14, 84);
    g_ui.ov_hum_chart = lv_chart_create(c2);
    lv_obj_set_pos(g_ui.ov_hum_chart, 14, rowh - 58);
    lv_obj_set_size(g_ui.ov_hum_chart, colw - 156, 42);
    lv_chart_set_type(g_ui.ov_hum_chart, LV_CHART_TYPE_BAR);
    lv_chart_set_div_line_count(g_ui.ov_hum_chart, 1, 0);
    lv_obj_set_style_border_width(g_ui.ov_hum_chart, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    set_bg(g_ui.ov_hum_chart, C_CARD, LV_OPA_TRANSP);
    g_ui.ov_hum_ser = lv_chart_add_series(g_ui.ov_hum_chart, lv_color_hex(C_COOL), LV_CHART_AXIS_PRIMARY_Y);

    t = lv_label_create(c3);
    lv_label_set_text(t, "风扇");
    style_label(t, C_MAIN);
    lv_obj_set_pos(t, 14, 10);
    mk_detail_btn(c3, colw - 90, 8, MOD_FAN);
    add_icon(c3, colw - 142, 47, 126, 126, MOD_FAN);
    g_ui.ov_fan = lv_label_create(c3);
    style_label(g_ui.ov_fan, C_MAIN);
    lv_obj_set_pos(g_ui.ov_fan, 14, 58);
    g_ui.ov_fan_state = lv_label_create(c3);
    style_label(g_ui.ov_fan_state, C_SUB);
    lv_obj_set_pos(g_ui.ov_fan_state, 14, 86);
    lv_obj_t *fan_on = lv_btn_create(c3);
    g_ui.ov_fan_on_btn = fan_on;
    lv_obj_set_pos(fan_on, 14, rowh - 56);
    lv_obj_set_size(fan_on, 110, 40);
    style_btn(fan_on, C_PRIMARY);
    lv_obj_add_event_cb(fan_on, ev_fan_on, LV_EVENT_CLICKED, NULL);
    t = lv_label_create(fan_on);
    lv_label_set_text(t, "开启");
    style_label(t, C_MAIN);
    lv_obj_center(t);
    lv_obj_t *fan_off = lv_btn_create(c3);
    g_ui.ov_fan_off_btn = fan_off;
    lv_obj_set_pos(fan_off, 132, rowh - 56);
    lv_obj_set_size(fan_off, 110, 40);
    style_btn(fan_off, C_OFF);
    lv_obj_add_event_cb(fan_off, ev_fan_off, LV_EVENT_CLICKED, NULL);
    t = lv_label_create(fan_off);
    lv_label_set_text(t, "关闭");
    style_label(t, C_MAIN);
    lv_obj_center(t);
    t = lv_label_create(c4);
    lv_label_set_text(t, "光照");
    style_label(t, C_MAIN);
    lv_obj_set_pos(t, 14, 10);
    mk_detail_btn(c4, colw - 90, 8, MOD_LIGHT);
    add_icon(c4, colw - 142, 52, 126, 126, MOD_LIGHT);
    g_ui.ov_lux = lv_label_create(c4);
    style_label(g_ui.ov_lux, C_MAIN);
    lv_obj_set_pos(g_ui.ov_lux, 14, 58);
    lv_obj_t *lux_hint = lv_label_create(c4);
    g_ui.ov_lux_hint = lux_hint;
    lv_label_set_text(lux_hint, "传感器: BH1750");
    style_label(lux_hint, C_SUB);
    lv_obj_set_pos(lux_hint, 14, 84);

    t = lv_label_create(c5);
    lv_label_set_text(t, "环境摘要");
    style_label(t, C_MAIN);
    lv_obj_set_pos(t, 14, 10);
    g_ui.ov_sum = lv_label_create(c5);
    style_label(g_ui.ov_sum, C_MAIN);
    lv_obj_set_pos(g_ui.ov_sum, 14, 62);
    lv_obj_t *sum_hint = lv_label_create(c5);
    lv_label_set_text(sum_hint, "实时传感器数据");
    style_label(sum_hint, C_SUB);
    lv_obj_set_pos(sum_hint, 14, 92);

    t = lv_label_create(c6);
    lv_label_set_text(t, "灯光");
    style_label(t, C_MAIN);
    lv_obj_set_pos(t, 14, 10);
    mk_detail_btn(c6, colw - 90, 8, MOD_LAMP);
    g_ui.ov_lamp = lv_label_create(c6);
    style_label(g_ui.ov_lamp, C_MAIN);
    lv_obj_set_pos(g_ui.ov_lamp, 14, 52);

    lv_obj_t *br = lv_btn_create(c6);
    g_ui.ov_lamp_r_btn = br;
    lv_obj_set_pos(br, 14, rowh - 56);
    lv_obj_set_size(br, 56, 40);
    style_btn(br, 0xD64545);
    lv_obj_add_event_cb(br, ev_lamp_toggle, LV_EVENT_CLICKED, (void *)(intptr_t)0);
    t = lv_label_create(br);
    lv_label_set_text(t, "R");
    style_label(t, C_MAIN);
    lv_obj_center(t);

    lv_obj_t *bg = lv_btn_create(c6);
    g_ui.ov_lamp_g_btn = bg;
    lv_obj_set_pos(bg, 78, rowh - 56);
    lv_obj_set_size(bg, 56, 40);
    style_btn(bg, 0x2FAF5A);
    lv_obj_add_event_cb(bg, ev_lamp_toggle, LV_EVENT_CLICKED, (void *)(intptr_t)1);
    t = lv_label_create(bg);
    lv_label_set_text(t, "G");
    style_label(t, C_MAIN);
    lv_obj_center(t);

    lv_obj_t *bb = lv_btn_create(c6);
    g_ui.ov_lamp_b_btn = bb;
    lv_obj_set_pos(bb, 142, rowh - 56);
    lv_obj_set_size(bb, 56, 40);
    style_btn(bb, 0x3A74E8);
    lv_obj_add_event_cb(bb, ev_lamp_toggle, LV_EVENT_CLICKED, (void *)(intptr_t)2);
    t = lv_label_create(bb);
    lv_label_set_text(t, "B");
    style_label(t, C_MAIN);
    lv_obj_center(t);

    lv_obj_t *off = lv_btn_create(c6);
    g_ui.ov_lamp_off_btn = off;
    lv_obj_set_pos(off, 206, rowh - 56);
    lv_obj_set_size(off, 56, 40);
    style_btn(off, C_OFF);
    lv_obj_add_event_cb(off, ev_lamp_all_off, LV_EVENT_CLICKED, NULL);
    t = lv_label_create(off);
    lv_label_set_text(t, "全关");
    style_label(t, C_MAIN);
    lv_obj_center(t);
}

static void build_detail(lv_obj_t *root)
{
    lv_coord_t left_w = g_w - 16 - 392 - 12;
    lv_coord_t left_h = CONTENT_H - 68;
    lv_coord_t right_w = 376;
    lv_coord_t right_h = CONTENT_H - 68;
    lv_coord_t ctrl_h = 132;
    lv_coord_t tip_h = 72;
    lv_coord_t log_y = 216;
    lv_coord_t log_h = right_h - log_y;

    lv_obj_t *v = lv_obj_create(root);
    lv_obj_set_pos(v, 0, TOP_H);
    lv_obj_set_size(v, g_w, CONTENT_H);
    style_panel(v, C_PANEL, 0);
    g_ui.pages[PAGE_DETAIL] = v;

    lv_obj_t *back = lv_btn_create(v);
    lv_obj_set_pos(back, 16, 8);
    lv_obj_set_size(back, 70, 40);
    style_btn(back, C_PRIMARY);
    lv_obj_add_event_cb(back, ev_nav_overview, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bt = lv_label_create(back);
    lv_label_set_text(bt, "返回");
    style_label(bt, C_MAIN);
    lv_obj_center(bt);

    g_ui.dt_title = lv_label_create(v);
    style_label(g_ui.dt_title, C_MAIN);
    lv_obj_set_pos(g_ui.dt_title, 98, 16);

    lv_obj_t *left = mk_card(v, 16, 56, left_w, left_h);
    lv_obj_t *right = mk_card(v, g_w - 392, 56, right_w, right_h);

    lv_obj_t *curr = mk_card(left, 0, 0, left_w, 120);
    g_ui.dt_curr = lv_label_create(curr);
    style_label(g_ui.dt_curr, C_MAIN);
    lv_obj_set_pos(g_ui.dt_curr, 16, 20);
    lv_label_set_text(g_ui.dt_curr, "当前值: --");
    g_ui.dt_stat = lv_label_create(curr);
    style_label(g_ui.dt_stat, C_SUB);
    lv_obj_set_pos(g_ui.dt_stat, 16, 72);
    lv_label_set_text(g_ui.dt_stat, "统计值: --");

    lv_obj_t *hist = mk_card(left, 0, 132, left_w, left_h - 132);
    lv_obj_t *hist_t = lv_label_create(hist);
    lv_label_set_text(hist_t, "趋势图");
    style_label(hist_t, C_SUB);
    lv_obj_set_pos(hist_t, 16, 10);

    g_ui.dt_axis_top = lv_label_create(hist);
    style_label(g_ui.dt_axis_top, C_SUB);
    lv_obj_set_pos(g_ui.dt_axis_top, 16, 36);
    lv_label_set_text(g_ui.dt_axis_top, "100");
    g_ui.dt_axis_mid = lv_label_create(hist);
    style_label(g_ui.dt_axis_mid, C_SUB);
    lv_obj_set_pos(g_ui.dt_axis_mid, 16, 98);
    lv_label_set_text(g_ui.dt_axis_mid, "50");
    g_ui.dt_axis_bot = lv_label_create(hist);
    style_label(g_ui.dt_axis_bot, C_SUB);
    lv_obj_set_pos(g_ui.dt_axis_bot, 16, 160);
    lv_label_set_text(g_ui.dt_axis_bot, "0");

    g_ui.dt_chart = lv_chart_create(hist);
    lv_obj_set_pos(g_ui.dt_chart, 58, 38);
    lv_obj_set_size(g_ui.dt_chart, left_w - 70, (left_h - 132) - 50);
    lv_chart_set_type(g_ui.dt_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_div_line_count(g_ui.dt_chart, 3, 4);
    lv_obj_set_style_border_width(g_ui.dt_chart, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    set_bg(g_ui.dt_chart, C_CARD, LV_OPA_40);
    g_ui.dt_ser = lv_chart_add_series(g_ui.dt_chart, lv_color_hex(C_WARM), LV_CHART_AXIS_PRIMARY_Y);

    lv_obj_t *ctrl = mk_card(right, 0, 0, right_w, ctrl_h);
    lv_obj_t *ct = lv_label_create(ctrl);
    lv_label_set_text(ct, "控制区");
    style_label(ct, C_MAIN);
    lv_obj_set_pos(ct, 14, 10);

    g_ui.dt_slider = lv_slider_create(ctrl);
    lv_obj_set_pos(g_ui.dt_slider, 14, 56);
    lv_obj_set_size(g_ui.dt_slider, 240, 20);
    lv_obj_add_event_cb(g_ui.dt_slider, ev_slider, LV_EVENT_VALUE_CHANGED, NULL);

    g_ui.dt_slider_value = lv_label_create(ctrl);
    style_label(g_ui.dt_slider_value, C_MAIN);
    lv_obj_set_pos(g_ui.dt_slider_value, 264, 56);
    g_ui.dt_btn_on = lv_btn_create(ctrl);
    lv_obj_set_pos(g_ui.dt_btn_on, 14, 86);
    lv_obj_set_size(g_ui.dt_btn_on, 104, 40);
    style_btn(g_ui.dt_btn_on, 0xD64545);
    lv_obj_add_event_cb(g_ui.dt_btn_on, ev_lamp_toggle, LV_EVENT_CLICKED, (void *)(intptr_t)0);
    lv_obj_t *on_t = lv_label_create(g_ui.dt_btn_on);
    lv_label_set_text(on_t, "R");
    style_label(on_t, C_MAIN);
    lv_obj_center(on_t);

    g_ui.dt_btn_off = lv_btn_create(ctrl);
    lv_obj_set_pos(g_ui.dt_btn_off, 126, 86);
    lv_obj_set_size(g_ui.dt_btn_off, 104, 40);
    style_btn(g_ui.dt_btn_off, 0x2FAF5A);
    lv_obj_add_event_cb(g_ui.dt_btn_off, ev_lamp_toggle, LV_EVENT_CLICKED, (void *)(intptr_t)1);
    lv_obj_t *off_t = lv_label_create(g_ui.dt_btn_off);
    lv_label_set_text(off_t, "G");
    style_label(off_t, C_MAIN);
    lv_obj_center(off_t);

    g_ui.dt_btn_aux = lv_btn_create(ctrl);
    lv_obj_set_pos(g_ui.dt_btn_aux, 238, 86);
    lv_obj_set_size(g_ui.dt_btn_aux, 104, 40);
    style_btn(g_ui.dt_btn_aux, 0x3A74E8);
    lv_obj_add_event_cb(g_ui.dt_btn_aux, ev_lamp_toggle, LV_EVENT_CLICKED, (void *)(intptr_t)2);
    lv_obj_t *aux_t = lv_label_create(g_ui.dt_btn_aux);
    lv_label_set_text(aux_t, "B");
    style_label(aux_t, C_MAIN);
    lv_obj_center(aux_t);

    lv_obj_t *tip = mk_card(right, 0, 140, right_w, tip_h);
    g_ui.dt_tip = lv_label_create(tip);
    style_label(g_ui.dt_tip, C_COOL);
    lv_obj_set_pos(g_ui.dt_tip, 14, 14);
    lv_obj_set_width(g_ui.dt_tip, right_w - 28);
    lv_label_set_long_mode(g_ui.dt_tip, LV_LABEL_LONG_WRAP);
    lv_label_set_text(g_ui.dt_tip, "说明信息加载中...");

    lv_obj_t *log = mk_card(right, 0, log_y, right_w, log_h);
    lv_obj_t *log_t = lv_label_create(log);
    lv_label_set_text(log_t, "历史数据窗口(最近10条)");
    style_label(log_t, C_MAIN);
    lv_obj_set_pos(log_t, 14, 8);
    g_ui.dt_log = lv_textarea_create(log);
    lv_obj_set_pos(g_ui.dt_log, 14, 34);
    lv_obj_set_size(g_ui.dt_log, right_w - 28, log_h - 44);
    lv_textarea_set_one_line(g_ui.dt_log, false);
    lv_obj_set_style_bg_opa(g_ui.dt_log, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(g_ui.dt_log, lv_color_hex(0x0F1F38), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(g_ui.dt_log, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(g_ui.dt_log, lv_color_hex(C_MAIN), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(g_ui.dt_log, lv_color_hex(C_MAIN), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(g_ui.dt_log, FONT_UI, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_textarea_set_text(g_ui.dt_log, "历史数据加载中...\n");
    lv_obj_move_foreground(g_ui.dt_log);
}

static void build_mode(lv_obj_t *root)
{
    static const char *desc[MODE_COUNT] = {
        "仅手动控制: 风扇开关、RGB颜色",
        "自动联动: 温湿度控风扇, 光照控RGB",
        "节能优先: 提高触发阈值, 减少动作次数",
        "夜间静音: 风扇更保守, 仅蓝光补光",
        "均衡策略: 温湿光联合决策"
    };

    lv_coord_t list_w = g_w - 32;
    lv_coord_t list_h = CONTENT_H - 68;

    lv_obj_t *v = lv_obj_create(root);
    lv_obj_set_pos(v, 0, TOP_H);
    lv_obj_set_size(v, g_w, CONTENT_H);
    style_panel(v, C_PANEL, 0);
    g_ui.pages[PAGE_MODE] = v;

    lv_obj_t *back = lv_btn_create(v);
    lv_obj_set_pos(back, 16, 8);
    lv_obj_set_size(back, 70, 40);
    style_btn(back, C_PRIMARY);
    lv_obj_add_event_cb(back, ev_nav_overview, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bt = lv_label_create(back);
    lv_label_set_text(bt, "返回");
    style_label(bt, C_MAIN);
    lv_obj_center(bt);

    lv_obj_t *title = lv_label_create(v);
    lv_label_set_text(title, "模式管理");
    style_label(title, C_MAIN);
    lv_obj_set_pos(title, 100, 16);

    lv_obj_t *list = mk_card(v, 16, 56, list_w, list_h);
    g_ui.mode_current = lv_label_create(list);
    style_label(g_ui.mode_current, C_MAIN);
    lv_obj_set_pos(g_ui.mode_current, 16, 10);
    g_ui.mode_info = lv_label_create(list);
    style_label(g_ui.mode_info, C_SUB);
    lv_obj_set_pos(g_ui.mode_info, 320, 10);
    lv_obj_t *mode_tip = lv_label_create(list);
    lv_label_set_text(mode_tip, "点击下方按钮即可切换模式");
    style_label(mode_tip, C_SUB);
    lv_obj_set_pos(mode_tip, 16, 34);

    for (int i = 0; i < MODE_COUNT; ++i) {
        int col = i % 2;
        int row = i / 2;
        int bw = (list_w - 48) / 2;
        int bh = 74;
        lv_obj_t *b = lv_btn_create(list);
        lv_obj_set_pos(b, 16 + col * (bw + 16), 62 + row * (bh + 10));
        lv_obj_set_size(b, bw, bh);
        style_btn(b, i == g_app.mode ? C_PRIMARY : C_OFF);
        lv_obj_set_style_border_width(b, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_color(b, lv_color_hex(C_MAIN), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_add_event_cb(b, ev_mode_pick, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        g_ui.mode_btns[i] = b;

        lv_obj_t *n = lv_label_create(b);
        lv_label_set_text(n, mode_name((app_mode_t)i));
        style_label(n, C_MAIN);
        lv_obj_set_pos(n, 14, 6);

        lv_obj_t *d = lv_label_create(b);
        lv_label_set_text(d, desc[i]);
        style_label(d, C_SUB);
        lv_obj_set_pos(d, 14, 28);
        lv_obj_move_foreground(b);
    }
}

static void build_bottom(lv_obj_t *root)
{
    lv_obj_t *bar = lv_obj_create(root);
    lv_obj_set_pos(bar, 0, TOP_H + CONTENT_H);
    lv_obj_set_size(bar, g_w, BOTTOM_H);
    style_panel(bar, C_PANEL, 0);

    lv_obj_t *txt = lv_label_create(bar);
    lv_label_set_text(txt, "首页 / 详情 / 模式管理   分辨率自适配");
    style_label(txt, C_SUB);
    lv_obj_set_pos(txt, 16, 18);
}

static void history_push(int temp, int hum, int lux)
{
    time_t t;
    struct tm tmv;

    for (int k = 0; k < HIST_N - 1; ++k) {
        g_app.hist_temp[k] = g_app.hist_temp[k + 1];
        g_app.hist_hum[k] = g_app.hist_hum[k + 1];
        g_app.hist_lux[k] = g_app.hist_lux[k + 1];
        memcpy(g_hist_time[k], g_hist_time[k + 1], sizeof(g_hist_time[k]));
    }

    g_app.hist_temp[HIST_N - 1] = temp;
    g_app.hist_hum[HIST_N - 1] = hum;
    g_app.hist_lux[HIST_N - 1] = lux;

    t = time(NULL);
    localtime_r(&t, &tmv);
    strftime(g_hist_time[HIST_N - 1], sizeof(g_hist_time[HIST_N - 1]), "%H:%M:%S", &tmv);
}

static void detail_fill_recent_log(module_t mod)
{
    char buf[768];
    size_t used = 0;
    int n = (g_db_count < 10) ? g_db_count : 10;

    if (n <= 0) {
        int start = HIST_N - 10;
        if (start < 0) start = 0;
        used += (size_t)snprintf(buf + used, sizeof(buf) - used, "数据库暂无记录，显示最近采样缓存:\n");
        for (int i = start; i < HIST_N && used < sizeof(buf); ++i) {
            int val = 0;
            const char *unit = "";
            if (mod == MOD_TEMP) {
                val = g_app.hist_temp[i];
                unit = "°C";
            } else if (mod == MOD_HUM) {
                val = g_app.hist_hum[i];
                unit = "%";
            } else {
                val = g_app.hist_lux[i];
                unit = "LUX";
            }
            used += (size_t)snprintf(buf + used, sizeof(buf) - used, "%s  %d%s\n", g_hist_time[i], val, unit);
        }
        detail_log_set(buf);
        return;
    }

    used += (size_t)snprintf(buf + used, sizeof(buf) - used, "最近%d条数据库记录(1分钟间隔):\n", n);
    for (int i = 0; i < n && used < sizeof(buf); ++i) {
        int idx = (g_db_head - 1 - i + DB_MAX_RECORDS) % DB_MAX_RECORDS;
        const db_record_t *rec = &g_db[idx];
        struct tm tmv;
        char tbuf[20];

        localtime_r(&rec->ts, &tmv);
        strftime(tbuf, sizeof(tbuf), "%H:%M:%S", &tmv);

        if (mod == MOD_TEMP) {
            used += (size_t)snprintf(buf + used, sizeof(buf) - used, "%s  %d°C\n", tbuf, rec->temp);
        } else if (mod == MOD_HUM) {
            used += (size_t)snprintf(buf + used, sizeof(buf) - used, "%s  %d%%\n", tbuf, rec->hum);
        } else if (mod == MOD_LIGHT) {
            used += (size_t)snprintf(buf + used, sizeof(buf) - used, "%s  %d LUX\n", tbuf, rec->lux);
        } else if (mod == MOD_FAN) {
            used += (size_t)snprintf(buf + used, sizeof(buf) - used, "%s  风扇:%s  模式:%s\n",
                                     tbuf, rec->fan ? "开启" : "关闭", mode_name(rec->mode));
        } else {
            used += (size_t)snprintf(buf + used, sizeof(buf) - used, "%s  RGB:%d%d%d  模式:%s\n",
                                     tbuf,
                                     rec->lamp_r_on ? 1 : 0,
                                     rec->lamp_g_on ? 1 : 0,
                                     rec->lamp_b_on ? 1 : 0,
                                     mode_name(rec->mode));
        }
    }
    detail_log_set(buf);
}

static void data_step(void)
{
    float t_c = -1.0f;
    float h_pct = -1.0f;
    float lux_f = -1.0f;
    int raw_temp = g_app.temp;
    int raw_hum = g_app.hum;
    int raw_lux = g_app.lux;
    int temp;
    int hum;
    int lux;
    int aht_ok = 0;
    int bh_ok = 0;
    char ts[32];
    time_t now_t;
    struct tm tmv;

    if (!g_i2c_ok) {
        history_push(g_app.temp, g_app.hum, g_app.lux);
        auto_update_fan_by_temp();
        fprintf(stderr, "[sensor] i2c bus not ready, keep last values\n");
        return;
    }

    if (g_aht20_ok) {
        int rc = aht20_read(&g_i2c_bus, AHT20_I2C_ADDR, &t_c, &h_pct);
        if (rc == 0 || rc == -2) {
            raw_temp = clamp_i((int)(t_c + (t_c >= 0 ? 0.5f : -0.5f)), -40, 80);
            raw_hum = clamp_i((int)(h_pct + 0.5f), 0, 100);
            aht_ok = 1;
            if (rc == -2) {
                fprintf(stderr, "[sensor] AHT20 CRC warning, still using data T=%.2fC H=%.2f%%\n", t_c, h_pct);
            }
        } else {
            fprintf(stderr, "[sensor] AHT20 read failed, keep last T=%dC H=%d%%\n", g_app.temp, g_app.hum);
        }
    } else {
        fprintf(stderr, "[sensor] AHT20 not ready\n");
    }

    if (g_bh1750_ok) {
        int rc = bh1750_read_lux(&g_i2c_bus, BH1750_I2C_ADDR, &lux_f);
        if (rc == 0) {
            raw_lux = clamp_i((int)(lux_f + 0.5f), 0, 100000);
            bh_ok = 1;
        } else {
            fprintf(stderr, "[sensor] BH1750 read failed, keep last L=%d lux\n", g_app.lux);
        }
    } else {
        fprintf(stderr, "[sensor] BH1750 not ready\n");
    }

    temp = raw_temp;
    hum = raw_hum;
    lux = raw_lux;

    g_app.temp = temp;
    g_app.hum = hum;
    g_app.lux = lux;
    g_last_raw_temp = raw_temp;
    g_last_raw_hum = raw_hum;
    g_last_raw_lux = raw_lux;
    history_push(g_app.temp, g_app.hum, g_app.lux);

    now_t = time(NULL);
    localtime_r(&now_t, &tmv);
    strftime(ts, sizeof(ts), "%F %T", &tmv);

    printf("[sensor] %s raw(T=%.2fC,H=%.2f%%,L=%.2flux) disp(T=%dC,H=%d%%,L=%d) ok(aht=%d,bh=%d)\n",
           ts, t_c, h_pct, lux_f, g_app.temp, g_app.hum, g_app.lux, aht_ok, bh_ok);

    auto_update_fan_by_temp();
    auto_update_lamp_by_env();
    db_push_current(now_t);
}

static void tmr_clock(lv_timer_t *t)
{
    (void)t;
    refresh_time();
}

static void tmr_data(lv_timer_t *t)
{
    (void)t;
    data_step();
    apply_hw();
    refresh_all();
}

static bool touch_is_pointer_fd(int fd)
{
    unsigned long ev_bits[(EV_MAX + 8 * sizeof(unsigned long)) / (8 * sizeof(unsigned long))];
    unsigned long abs_bits[(ABS_MAX + 8 * sizeof(unsigned long)) / (8 * sizeof(unsigned long))];

    memset(ev_bits, 0, sizeof(ev_bits));
    memset(abs_bits, 0, sizeof(abs_bits));

    if (ioctl(fd, EVIOCGBIT(0, sizeof(ev_bits)), ev_bits) < 0) return false;
    if (!(ev_bits[EV_ABS / (8 * sizeof(unsigned long))] & (1UL << (EV_ABS % (8 * sizeof(unsigned long)))))) return false;
    if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abs_bits)), abs_bits) < 0) return false;

    if (abs_bits[ABS_X / (8 * sizeof(unsigned long))] & (1UL << (ABS_X % (8 * sizeof(unsigned long))))) return true;
    if (abs_bits[ABS_MT_POSITION_X / (8 * sizeof(unsigned long))] & (1UL << (ABS_MT_POSITION_X % (8 * sizeof(unsigned long))))) return true;

    return false;
}

static int touch_get_abs_range(int fd, unsigned int code, int *min_v, int *max_v)
{
    struct input_absinfo absinfo;
    if (ioctl(fd, EVIOCGABS(code), &absinfo) < 0) return -1;
    if (absinfo.maximum <= absinfo.minimum) return -1;
    *min_v = absinfo.minimum;
    *max_v = absinfo.maximum;
    return 0;
}

static void touch_load_calibration(int fd)
{
    int min_x = TP_MIN_X;
    int max_x = TP_MAX_X;
    int min_y = TP_MIN_Y;
    int max_y = TP_MAX_Y;

    if (touch_get_abs_range(fd, ABS_MT_POSITION_X, &min_x, &max_x) < 0) {
        (void)touch_get_abs_range(fd, ABS_X, &min_x, &max_x);
    }
    if (touch_get_abs_range(fd, ABS_MT_POSITION_Y, &min_y, &max_y) < 0) {
        (void)touch_get_abs_range(fd, ABS_Y, &min_y, &max_y);
    }

    g_tp_min_x = min_x;
    g_tp_max_x = max_x;
    g_tp_min_y = min_y;
    g_tp_max_y = max_y;
}

static int touch_open(const char *dev)
{
    char p[64];
    int fd;

    g_touch_fd = open(dev, O_RDONLY | O_NONBLOCK);
    if (g_touch_fd >= 0 && touch_is_pointer_fd(g_touch_fd)) {
        touch_load_calibration(g_touch_fd);
        return 0;
    }
    if (g_touch_fd >= 0) {
        close(g_touch_fd);
        g_touch_fd = -1;
    }

    for (int i = 0; i < 16; ++i) {
        snprintf(p, sizeof(p), "/dev/input/event%d", i);
        fd = open(p, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;
        if (touch_is_pointer_fd(fd)) {
            g_touch_fd = fd;
            touch_load_calibration(g_touch_fd);
            return 0;
        }
        close(fd);
    }

    return -1;
}

static void touch_close(void)
{
    if (g_touch_fd >= 0) {
        close(g_touch_fd);
        g_touch_fd = -1;
    }
}

static void touch_read(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    struct input_event ev;
    ssize_t rb;
    int explicit_press_seen = 0;
    int abs_seen = 0;
    int px;
    int py;

    (void)drv;

    if (g_touch_fd < 0) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    while ((rb = read(g_touch_fd, &ev, sizeof(ev))) > 0) {
        if (ev.type == EV_ABS) {
            if (ev.code == ABS_X || ev.code == ABS_MT_POSITION_X) g_touch_x = ev.value;
            if (ev.code == ABS_Y || ev.code == ABS_MT_POSITION_Y) g_touch_y = ev.value;
            if (ev.code == ABS_MT_POSITION_X || ev.code == ABS_MT_POSITION_Y || ev.code == ABS_X || ev.code == ABS_Y) {
                abs_seen = 1;
                g_touch_last_abs_ms = now_ms();
            }
            if (ev.code == ABS_MT_TRACKING_ID) {
                explicit_press_seen = 1;
                g_touch_pressed = (ev.value >= 0) ? 1 : 0;
            }
        } else if (ev.type == EV_KEY && ev.code == BTN_TOUCH) {
            explicit_press_seen = 1;
            g_touch_pressed = ev.value ? 1 : 0;
        }
    }
    if (rb < 0 && errno != EAGAIN) {
        g_touch_pressed = 0;
    }

    if (!explicit_press_seen) {
        if (abs_seen) {
            g_touch_pressed = 1;
        } else if (g_touch_pressed) {
            uint64_t dt = now_ms() - g_touch_last_abs_ms;
            if (dt > TOUCH_HOLD_MS) g_touch_pressed = 0;
        }
    }

    px = clamp_i(g_touch_x, g_tp_min_x, g_tp_max_x);
    py = clamp_i(g_touch_y, g_tp_min_y, g_tp_max_y);

    px = map_i(px, g_tp_min_x, g_tp_max_x, 0, g_w - 1);
    py = map_i(py, g_tp_min_y, g_tp_max_y, 0, g_h - 1);

    if (TOUCH_SWAP_XY) {
        int t = px;
        px = py;
        py = t;
    }
    if (TOUCH_INV_X) px = (g_w - 1) - px;
    if (TOUCH_INV_Y) py = (g_h - 1) - py;

    data->point.x = px;
    data->point.y = py;
    data->state = g_touch_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

int app_hardware_init(void)
{
    g_lamp_r_ok = false;
    g_lamp_g_ok = false;
    g_lamp_b_ok = false;

    if (gpio_export(LAMP_R_GPIO) == 0) g_lamp_r_ok = (gpio_set_direction(LAMP_R_GPIO, "out") == 0);
    if (gpio_export(LAMP_G_GPIO) == 0) g_lamp_g_ok = (gpio_set_direction(LAMP_G_GPIO, "out") == 0);
    if (gpio_export(LAMP_B_GPIO) == 0) g_lamp_b_ok = (gpio_set_direction(LAMP_B_GPIO, "out") == 0);
    usleep(100000);
    g_lamp_ok = g_lamp_r_ok || g_lamp_g_ok || g_lamp_b_ok;
    g_fan_ok = (fan_init(FAN_GPIO) == 0);
    if (g_fan_ok) {
        printf("[fan] detected channel: %s\n", fan_get_channel());
    } else {
        fprintf(stderr, "[fan] no valid fan channel (fan0/fan1)\n");
    }
    g_hw_lamp_level = -1;
    g_hw_fan_on = -1;
    if (g_fan_ok) {
        int fs = fan_get_state(FAN_GPIO);
        if (fs >= 0) g_app.fan = fs;
    }

    g_i2c_ok = false;
    g_aht20_ok = false;
    g_bh1750_ok = false;
    g_i2c_bus.fd = -1;
    g_i2c_bus.bus_id = SENSOR_I2C_BUS;
    db_reset();
    memset(g_hist_time, 0, sizeof(g_hist_time));
    for (int i = 0; i < HIST_N; ++i) {
        snprintf(g_hist_time[i], sizeof(g_hist_time[i]), "--:--:--");
    }
    g_last_raw_temp = g_app.temp;
    g_last_raw_hum = g_app.hum;
    g_last_raw_lux = g_app.lux;
    sensor_log_open();

    if (i2c_bus_open(&g_i2c_bus, SENSOR_I2C_BUS) == 0) {
        g_i2c_ok = true;
        g_aht20_ok = (aht20_init(&g_i2c_bus, AHT20_I2C_ADDR) == 0);
        g_bh1750_ok = (bh1750_init(&g_i2c_bus, BH1750_I2C_ADDR) == 0);

        printf("[sensor] AHT20(%s) BH1750(%s)\n",
               g_aht20_ok ? "ok" : "fail",
               g_bh1750_ok ? "ok" : "fail");
    } else {
        fprintf(stderr, "[sensor] i2c init failed, bus=%d\n", SENSOR_I2C_BUS);
    }

    if (g_aht20_ok || g_bh1750_ok) {
        data_step();
        for (int i = 0; i < HIST_N; ++i) {
            g_app.hist_temp[i] = g_app.temp;
            g_app.hist_hum[i] = g_app.hum;
            g_app.hist_lux[i] = g_app.lux;
        }
    }

    lamp_set_rgb(false, false, false);
    if (g_lamp_r_ok) (void)gpio_write(LAMP_R_GPIO, LAMP_OFF_LEVEL);
    if (g_lamp_g_ok) (void)gpio_write(LAMP_G_GPIO, LAMP_OFF_LEVEL);
    if (g_lamp_b_ok) (void)gpio_write(LAMP_B_GPIO, LAMP_OFF_LEVEL);
    return 0;
}

int app_lvgl_port_init(void)
{
    static lv_disp_drv_t disp_drv;
    static lv_indev_drv_t indev_drv;
    uint32_t fbw = 0;
    uint32_t fbh = 0;
    uint32_t dpi = 0;

    for (int i = 0; i < FB_SIZE_RETRY_COUNT; ++i) {
        fbdev_init();
        fbdev_get_sizes(&fbw, &fbh, &dpi);
        if (fbw > 0 && fbh > 0) break;

        fbdev_exit();
        usleep(FB_SIZE_RETRY_DELAY_US);
    }

    if (fbw == 0 || fbh == 0) {
        fbw = 1024;
        fbh = 600;
        fbdev_init();
    }

    g_w = (lv_coord_t)fbw;
    g_h = (lv_coord_t)fbh;

    g_buf1 = malloc(sizeof(lv_color_t) * fbw * DISP_BUF_LINES);
    if (!g_buf1) return -1;

    lv_disp_draw_buf_init(&g_draw_buf, g_buf1, NULL, fbw * DISP_BUF_LINES);

    lv_disp_drv_init(&disp_drv);
    disp_drv.draw_buf = &g_draw_buf;
    disp_drv.flush_cb = fbdev_flush;
    disp_drv.hor_res = fbw;
    disp_drv.ver_res = fbh;
    lv_disp_drv_register(&disp_drv);

    if (touch_open(TOUCH_DEV_PATH) == 0) {
        lv_indev_drv_init(&indev_drv);
        indev_drv.type = LV_INDEV_TYPE_POINTER;
        indev_drv.read_cb = touch_read;
        lv_indev_drv_register(&indev_drv);
    }

    return 0;
}

void app_lvgl_port_deinit(void)
{
    touch_close();

    if (g_buf1) {
        free(g_buf1);
        g_buf1 = NULL;
    }

    /* Keep fan state on exit; avoid unexpected toggles during service restart. */
    if (g_lamp_r_ok) {
        (void)gpio_write(LAMP_R_GPIO, LAMP_OFF_LEVEL);
        (void)gpio_unexport(LAMP_R_GPIO);
    }
    if (g_lamp_g_ok) {
        (void)gpio_write(LAMP_G_GPIO, LAMP_OFF_LEVEL);
        (void)gpio_unexport(LAMP_G_GPIO);
    }
    if (g_lamp_b_ok) {
        (void)gpio_write(LAMP_B_GPIO, LAMP_OFF_LEVEL);
        (void)gpio_unexport(LAMP_B_GPIO);
    }
    i2c_bus_close(&g_i2c_bus);
    sensor_log_close();
    g_i2c_ok = false;
    g_aht20_ok = false;
    g_bh1750_ok = false;
    g_lamp_r_ok = false;
    g_lamp_g_ok = false;
    g_lamp_b_ok = false;
    g_hw_lamp_level = -1;
    g_hw_fan_on = -1;
}

uint64_t app_get_ms(void)
{
    return now_ms();
}

void custom_build_screen(lv_ui *ui)
{
    memset(&g_ui, 0, sizeof(g_ui));

    g_ui.screen = lv_obj_create(NULL);
    lv_obj_set_size(g_ui.screen, g_w, g_h);
    style_panel(g_ui.screen, C_BG, 0);

    build_top(g_ui.screen);
    build_overview(g_ui.screen);
    build_detail(g_ui.screen);
    build_mode(g_ui.screen);
    build_bottom(g_ui.screen);

    show_page(PAGE_OVERVIEW);
    refresh_all();

    ui->screen = g_ui.screen;
}

void custom_init(void)
{
    if (!g_ui.screen) return;

    if (g_clock_t) lv_timer_del(g_clock_t);
    if (g_data_t) lv_timer_del(g_data_t);

    g_clock_t = lv_timer_create(tmr_clock, 1000, NULL);
    g_data_t = lv_timer_create(tmr_data, SENSOR_SAMPLE_PERIOD_MS, NULL);

    apply_hw();
    refresh_all();
}
