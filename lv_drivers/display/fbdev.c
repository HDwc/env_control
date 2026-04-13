/**
 * @file fbdev.c
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include "fbdev.h"
#if USE_FBDEV || USE_BSD_FBDEV

#include <stdlib.h>
#include <unistd.h>
#include <stddef.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#if USE_BSD_FBDEV
#include <sys/fcntl.h>
#include <sys/time.h>
#include <sys/consio.h>
#include <sys/fbio.h>
#else  /* USE_BSD_FBDEV */
#include <linux/fb.h>
#endif /* USE_BSD_FBDEV */

/*********************
 *      DEFINES
 *********************/
#ifndef FBDEV_PATH
#define FBDEV_PATH  "/dev/fb0"
#endif

#ifndef DIV_ROUND_UP
#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#endif

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *      STRUCTURES
 **********************/

struct bsd_fb_var_info{
    uint32_t xoffset;
    uint32_t yoffset;
    uint32_t xres;
    uint32_t yres;
    int bits_per_pixel;
 };

struct bsd_fb_fix_info{
    long int line_length;
    long int smem_len;
};

/**********************
 *  STATIC PROTOTYPES
 **********************/

/**********************
 *  STATIC VARIABLES
 **********************/
#if USE_BSD_FBDEV
static struct bsd_fb_var_info vinfo;
static struct bsd_fb_fix_info finfo;
#else
static struct fb_var_screeninfo vinfo;
static struct fb_fix_screeninfo finfo;
#endif /* USE_BSD_FBDEV */
static uint8_t *fbp = NULL;
static long int screensize = 0;
static int fbfd = -1;

static void fbdev_invalidate(void)
{
    if (fbfd >= 0) {
        close(fbfd);
    }
    fbfd = -1;
    fbp = NULL;
    screensize = 0;
}

/**********************
 *      MACROS
 **********************/

#if USE_BSD_FBDEV
#define FBIOBLANK FBIO_BLANK
#endif /* USE_BSD_FBDEV */

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void fbdev_init(void)
{
    fbdev_invalidate();

    // Open the file for reading and writing
    fbfd = open(FBDEV_PATH, O_RDWR);
    if(fbfd == -1) {
        perror("Error: cannot open framebuffer device");
        return;
    }
    LV_LOG_INFO("The framebuffer device was opened successfully");

    // Make sure that the display is on.
    if (ioctl(fbfd, FBIOBLANK, FB_BLANK_UNBLANK) != 0) {
        perror("ioctl(FBIOBLANK)");
        // Don't return. Some framebuffer drivers like efifb or simplefb don't implement FBIOBLANK.
    }

#if USE_BSD_FBDEV
    struct fbtype fb;
    unsigned line_length;

    //Get fb type
    if (ioctl(fbfd, FBIOGTYPE, &fb) != 0) {
        perror("ioctl(FBIOGTYPE)");
        fbdev_invalidate();
        return;
    }

    //Get screen width
    if (ioctl(fbfd, FBIO_GETLINEWIDTH, &line_length) != 0) {
        perror("ioctl(FBIO_GETLINEWIDTH)");
        fbdev_invalidate();
        return;
    }

    vinfo.xres = (unsigned) fb.fb_width;
    vinfo.yres = (unsigned) fb.fb_height;
    vinfo.bits_per_pixel = fb.fb_depth;
    vinfo.xoffset = 0;
    vinfo.yoffset = 0;
    finfo.line_length = line_length;
    finfo.smem_len = finfo.line_length * vinfo.yres;
#else /* USE_BSD_FBDEV */

    // Get fixed screen information
    if(ioctl(fbfd, FBIOGET_FSCREENINFO, &finfo) == -1) {
        perror("Error reading fixed information");
        fbdev_invalidate();
        return;
    }

    // Get variable screen information
    if(ioctl(fbfd, FBIOGET_VSCREENINFO, &vinfo) == -1) {
        perror("Error reading variable information");
        fbdev_invalidate();
        return;
    }
#endif /* USE_BSD_FBDEV */

    LV_LOG_INFO("%dx%d, %dbpp", vinfo.xres, vinfo.yres, vinfo.bits_per_pixel);

    // Figure out the size of the screen in bytes
    screensize =  finfo.smem_len; //finfo.line_length * vinfo.yres;    

    // Map the device to memory
    fbp = (uint8_t *)mmap(0, screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);
    if (fbp == MAP_FAILED) {
        perror("Error: failed to map framebuffer device to memory");
        fbdev_invalidate();
        return;
    }

    // Clear framebuffer once at startup to avoid stale UI remnants from a previous process.
    if (screensize > 0) {
        memset(fbp, 0, (size_t)screensize);
    }

    LV_LOG_INFO("The framebuffer device was mapped to memory successfully");

    printf("xres=%u yres=%u xres_virtual=%u yres_virtual=%u bpp=%u line_length=%u\n",
       vinfo.xres, vinfo.yres, vinfo.xres_virtual, vinfo.yres_virtual,
       vinfo.bits_per_pixel, finfo.line_length);

    printf("red: offset=%u len=%u\n", vinfo.red.offset, vinfo.red.length);
    printf("green: offset=%u len=%u\n", vinfo.green.offset, vinfo.green.length);
    printf("blue: offset=%u len=%u\n", vinfo.blue.offset, vinfo.blue.length);
    printf("transp: offset=%u len=%u\n", vinfo.transp.offset, vinfo.transp.length);
}

void fbdev_exit(void)
{
    if (fbp && fbp != MAP_FAILED && screensize > 0) {
        munmap(fbp, (size_t)screensize);
    }
    fbdev_invalidate();
}

/**
 * Flush a buffer to the marked area
 * @param drv pointer to driver where this function belongs
 * @param area an area where to copy `color_p`
 * @param color_p an array of pixels to copy to the `area` part of the screen
 */
static inline uint32_t scale_8_to_n(uint8_t v, uint32_t n)
{
    if (n >= 8) return ((uint32_t)v) << (n - 8);
    return ((uint32_t)v) >> (8 - n);
}

void fbdev_flush(lv_disp_drv_t * drv, const lv_area_t * area, lv_color_t * color_p)
{
    if (fbfd < 0 || fbp == NULL || vinfo.bits_per_pixel <= 0 || finfo.line_length <= 0) {
        lv_disp_flush_ready(drv);
        return;
    }

    int32_t act_x1 = area->x1 < 0 ? 0 : area->x1;
    int32_t act_y1 = area->y1 < 0 ? 0 : area->y1;
    int32_t act_x2 = area->x2 > (int32_t)vinfo.xres - 1 ? (int32_t)vinfo.xres - 1 : area->x2;
    int32_t act_y2 = area->y2 > (int32_t)vinfo.yres - 1 ? (int32_t)vinfo.yres - 1 : area->y2;

    if (act_x1 > act_x2 || act_y1 > act_y2) {
        lv_disp_flush_ready(drv);
        return;
    }

    int32_t src_w = area->x2 - area->x1 + 1;
    int32_t bpp = vinfo.bits_per_pixel / 8;
    if (bpp <= 0) {
        lv_disp_flush_ready(drv);
        return;
    }

    uint8_t *fb_begin = (uint8_t *)fbp;
    uint8_t *fb_end = fb_begin + screensize;

    for (int32_t y = act_y1; y <= act_y2; y++) {
        lv_color_t *src = color_p + (y - area->y1) * src_w + (act_x1 - area->x1);
        uint8_t *dst = fbp
                     + (y + vinfo.yoffset) * finfo.line_length
                     + (act_x1 + vinfo.xoffset) * bpp;
        size_t line_px = (size_t)(act_x2 - act_x1 + 1);
        size_t line_bytes = line_px * (size_t)bpp;

        if (dst < fb_begin || (dst + line_bytes) > fb_end) {
            break;
        }

        for (int32_t x = act_x1; x <= act_x2; x++) {
#if LV_COLOR_DEPTH == 32
    uint8_t r = src->ch.red;
    uint8_t g = src->ch.green;
    uint8_t b = src->ch.blue;

    uint32_t pixel = 0;
    pixel |= scale_8_to_n(r, vinfo.red.length)   << vinfo.red.offset;
    pixel |= scale_8_to_n(g, vinfo.green.length) << vinfo.green.offset;
    pixel |= scale_8_to_n(b, vinfo.blue.length)  << vinfo.blue.offset;

    if (vinfo.transp.length) {
        uint32_t a = (1U << vinfo.transp.length) - 1U;
        pixel |= a << vinfo.transp.offset;
    }

    if (bpp == 4) {
        *(uint32_t *)dst = pixel;
    } else if (bpp == 2) {
        *(uint16_t *)dst = (uint16_t)pixel;
    }
#elif LV_COLOR_DEPTH == 16
    *(uint16_t *)dst = src->full;
#endif
            src++;
            dst += bpp;
        }
    }

    lv_disp_flush_ready(drv);
}

void fbdev_get_sizes(uint32_t *width, uint32_t *height, uint32_t *dpi) {
    if (width)
        *width = vinfo.xres;

    if (height)
        *height = vinfo.yres;

    if (dpi && vinfo.height)
        *dpi = DIV_ROUND_UP(vinfo.xres * 254, vinfo.width * 10);
}

void fbdev_set_offset(uint32_t xoffset, uint32_t yoffset) {
    vinfo.xoffset = xoffset;
    vinfo.yoffset = yoffset;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

#endif
