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
-Ilv_drivers/display 

CFLAGS = $(INC) -Wall -O2 -DUSE_FBDEV=1
LDFLAGS = -lm

all:
	mkdir -p build
	$(CC) $(SRC) $(CFLAGS) -o $(TARGET) $(LDFLAGS)

clean:
	rm -f $(TARGET)