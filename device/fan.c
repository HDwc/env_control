#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include "../include/fan.h"

static const char *g_fan_paths[] = {
    "/sys/class/leds/fan0/brightness",
    "/sys/class/leds/fan1/brightness",
};
static const char *g_fan_names[] = {"fan0", "fan1"};
static int g_fan_idx = -1;
static const int g_fan_count = (int)(sizeof(g_fan_paths) / sizeof(g_fan_paths[0]));

static int fan_detect_channel(void)
{
    for (int i = 0; i < g_fan_count; ++i) {
        if (access(g_fan_paths[i], W_OK) == 0) {
            g_fan_idx = i;
            return 0;
        }
    }
    g_fan_idx = -1;
    return -1;
}

static const char *fan_active_path(void)
{
    if (g_fan_idx < 0 || g_fan_idx >= g_fan_count) return NULL;
    return g_fan_paths[g_fan_idx];
}

static int fan_write_index(int idx, const char *value)
{
    int fd;
    size_t len = strlen(value);
    ssize_t wr;

    if (idx < 0 || idx >= g_fan_count) return -1;
    if (access(g_fan_paths[idx], W_OK) != 0) return -1;

    fd = open(g_fan_paths[idx], O_WRONLY);
    if (fd < 0) return -1;

    wr = write(fd, value, len);
    close(fd);
    if (wr < 0 || (size_t)wr != len) return -1;

    return 0;
}

static int fan_write_value(const char *value)
{
    int ok = 0;
    int last_ok = -1;
    for (int i = 0; i < g_fan_count; ++i) {
        if (fan_write_index(i, value) == 0) {
            ok = 1;
            last_ok = i;
        }
    }

    if (ok) {
        if (g_fan_idx != last_ok) {
            g_fan_idx = last_ok;
            printf("[fan] active channel: %s (broadcast write)\n", g_fan_names[g_fan_idx]);
        } else {
            g_fan_idx = last_ok;
        }
        return 0;
    }

    g_fan_idx = -1;
    return -1;
}

int fan_init(int gpio)
{
    (void)gpio;
    if (fan_detect_channel() < 0) return -1;
    printf("[fan] using channel: %s\n", g_fan_names[g_fan_idx]);
    return 0;
}

int fan_on(int gpio)
{
    (void)gpio;
    return fan_write_value("1");
}

int fan_off(int gpio)
{
    (void)gpio;
    return fan_write_value("0");
}

int fan_get_state(int gpio)
{
    char buf[8];
    int fd;
    ssize_t rd;
    const char *path;

    (void)gpio;
    if (g_fan_idx < 0 || g_fan_idx >= g_fan_count) {
        if (fan_detect_channel() < 0) return -1;
    }

    path = fan_active_path();
    if (!path) {
        for (int i = 0; i < g_fan_count; ++i) {
            if (access(g_fan_paths[i], R_OK) == 0) {
                g_fan_idx = i;
                path = g_fan_paths[i];
                break;
            }
        }
        if (!path) return -1;
    }

    fd = open(path, O_RDONLY);
    if (fd < 0) return -1;

    rd = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (rd <= 0) return -1;

    buf[rd] = '\0';
    return (atoi(buf) > 0) ? 1 : 0;
}

int fan_deinit(int gpio)
{
    (void)gpio;
    g_fan_idx = -1;
    return 0;
}

const char *fan_get_channel(void)
{
    if (g_fan_idx < 0) return "unknown";
    return g_fan_names[g_fan_idx];
}
