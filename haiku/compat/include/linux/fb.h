/* Linux <linux/fb.h>: no fbdev, only what DRM's mode code converts */
#include <lx/all.h>
#ifndef _LX_FB_H
#define _LX_FB_H
#define KHZ2PICOS(a)	(1000000000UL / (a))
#define PICOS2KHZ(a)	(1000000000UL / (a))
#define FB_VMODE_INTERLACED	1
#define FB_VMODE_DOUBLE		2
#define FB_SYNC_HOR_HIGH_ACT	1
#define FB_SYNC_VERT_HIGH_ACT	2
struct fb_info;
struct fb_videomode {
	const char *name;
	u32 refresh;
	u32 xres;
	u32 yres;
	u32 pixclock;
	u32 left_margin;
	u32 right_margin;
	u32 upper_margin;
	u32 lower_margin;
	u32 hsync_len;
	u32 vsync_len;
	u32 sync;
	u32 vmode;
	u32 flag;
};
#endif
