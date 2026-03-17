CC = gcc

TARGET = build/env_control

SRC = \
$(wildcard src/*.c) \
$(wildcard device/*.c) \
$(wildcard gui/*.c) \
$(wildcard gui/images/*.c) \
$(wildcard gui/guider_fonts/*.c) \
$(wildcard gui/guider_customer_fonts/*.c) \
$(shell find lvgl/src -name "*.c") \
lv_drivers/display/fbdev.c \
lv_drivers/indev/evdev.c

INC = \
-I. \
-Iinclude \
-Isrc \
-Igui \
-Igui/images \
-Igui/guider_fonts \
-Igui/guider_customer_fonts \
-Ilvgl \
-Ilvgl/src \
-Ilv_drivers \
-Ilv_drivers/display \
-Ilv_drivers/indev

CFLAGS = $(INC) -Wall -O2 -DUSE_FBDEV=1 -DUSE_EVDEV=1 -DLV_CONF_INCLUDE_SIMPLE
LDFLAGS = -lm

all:
	mkdir -p build
	$(CC) $(SRC) $(CFLAGS) -o $(TARGET) $(LDFLAGS)

clean:
	rm -f $(TARGET)