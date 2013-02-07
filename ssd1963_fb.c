
#define DEBUG

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/list.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/printk.h>
#include <linux/console.h>

#include <asm/sizes.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/gpio.h>

#include "ssd1963_fb.h"

#define DRIVER_NAME		SSD1963_FB_DRIVER_NAME
#define MODULE_NAME		DRIVER_NAME

#define SSD1963_FB_DEBUG

#ifdef SSD1963_FB_DEBUG
#define print_debug(fmt,...) pr_debug("%s:%s:%d: "fmt, MODULE_NAME, __func__, __LINE__, ##__VA_ARGS__)
#else
#define print_debug(fmt,...)
#endif

struct ssd1963_fb {
	struct fb_info info;
	struct platform_device *dev;
	struct ssd1963_platform_data *pdata;
	struct ssd_init_vector iv;
	u32 cmap[16];
};

static struct ssd1963_fb this_fb;

/* 22-25, 28-31 */
#define BUS8(v)		(((v) & 0x0f) << 22 | ((v) & 0xf0) << (28 - 4))
/* 22-25, 27-31 */
#define BUS9(v)		(((v) & 0x0f) << 22 | ((v) & 0x1f0) << (27 - 4))

#define BUS_DC_MASK	(1 << 17)
#define BUS_WR_MASK	(1 << 18)

#define BUS		BUS8
#define BUS_MASK	BUS(~0)
// #define BUS_CMD_MASK	BUS(0xff) /* commands are only 8 bit wide */
#define BUS_CTL_MASK	(BUS_DC_MASK | BUS_WR_MASK)

#if 0
#include <mach/platform.h>

#define GPIO_CLR_BANK0	(__io_address(GPIO_BASE) + 0x28)
#define GPIO_SET_BANK0	(__io_address(GPIO_BASE) + 0x1c)

static void nop_n(unsigned n)
{
	while (n--)
		nop();
}

/* slow bus access */

#define WAIT1	nop_n(30)
#define WAIT2	nop_n(60)

void ssd_wr_slow_data(u8 v)
{
	u32 d = BUS(v);
	if (0)
		print_debug("%02x\n", v);

	writel(BUS_WR_MASK, GPIO_CLR_BANK0);
	WAIT1;
	writel(~d & BUS_MASK, GPIO_CLR_BANK0);
	WAIT1;
	writel( d & BUS_MASK, GPIO_SET_BANK0);
	WAIT2; /* todo: WAIT1? */
	writel(BUS_WR_MASK, GPIO_SET_BANK0);
	WAIT2;
}

void ssd_wr_slow_cmd(u8 v)
{
	u32 d = BUS(v);
	if (0)
		print_debug("%02x\n", v);

	writel(BUS_DC_MASK, GPIO_CLR_BANK0);
	WAIT1;
	writel(BUS_WR_MASK, GPIO_CLR_BANK0);
	WAIT1;
	writel(~d & BUS_MASK, GPIO_CLR_BANK0);
	writel( d & BUS_MASK, GPIO_SET_BANK0);
	WAIT1;
	writel(BUS_WR_MASK, GPIO_SET_BANK0);
	WAIT1;
	writel(BUS_DC_MASK, GPIO_SET_BANK0);
	WAIT2;
}

/* fast bus access */

static inline void ssd1963_bus_wr(u32 v)
{
	u32 d = BUS(v);
	writel((~d & BUS_MASK) | BUS_WR_MASK, GPIO_CLR_BANK0);
	writel(  d & BUS_MASK               , GPIO_SET_BANK0);
	writel(                  BUS_WR_MASK, GPIO_SET_BANK0);
}

static inline void ssd1963_wr_cmd(u8 x)
{
	if (0)
		print_debug("%02x\n", x);
	ssd_wr_slow_cmd(x);
}

static inline void ssd1963_wr_data(u8 x)
{
	if (0)
		print_debug("%02x", x);
	ssd1963_bus_wr(x);
}
#else
void ssd_wr_slow_data(u8 v)
{
	if (0)
		print_debug("%02x\n", v);
}

void ssd_wr_slow_cmd(u8 v)
{
	if (0)
		print_debug("%02x\n", v);
}

static inline void ssd1963_bus_wr(u32 v)
{
}

static inline void ssd1963_wr_cmd(u8 x)
{
	if (0)
		print_debug("%02x\n", x);
}

static inline void ssd1963_wr_data(u8 x)
{
	if (0)
		print_debug("%02x", x);
}
#endif

#define SSD_WR_CMD(x)	ssd1963_wr_cmd(x)
#define SSD_WR_DATA(x)	ssd1963_wr_data(x)

#include "ssd1963_cmd.h"

/* This is limited to 16 characters when displayed by X startup */
static const char *ssd1963_name = "SSD1963 FB";

static int ssd1963_fb_set_bitfields(struct fb_var_screeninfo *var)
{
	var->red.msb_right    = 0;
	var->green.msb_right  = 0;
	var->blue.msb_right   = 0;
	var->transp.msb_right = 0;

	var->transp.length = 0;

	if (var->bits_per_pixel <= 8) {
		var->red.length    = var->bits_per_pixel;
		var->green.length  = var->bits_per_pixel;
		var->blue.length   = var->bits_per_pixel;

		var->blue.offset   = 0;
		var->green.offset  = 0;
		var->red.offset    = 0;
		var->transp.offset = 0;
	} else {
		switch (this_fb.pdata->bus_fmt) {
		case SSD_DATA_8:
		case SSD_DATA_12:
		case SSD_DATA_16_PACKED:
		case SSD_DATA_24:
			if (var->bits_per_pixel < 24)
				return -EINVAL;
			var->red.length     = 8;
			var->green.length   = 8;
			var->blue.length    = 8;
			if (var->bits_per_pixel < 32)
				break;
			var->transp.length = 8;
			break;
		case SSD_DATA_9:
		case SSD_DATA_18:
			if (var->bits_per_pixel < 18)
				return -EINVAL;
			var->red.length     = 6;
			var->green.length   = 6;
			var->blue.length    = 6;
			break;
		case SSD_DATA_16_565:
			if (var->bits_per_pixel != 16)
				return -EINVAL;
			var->red.length     = 5;
			var->green.length   = 6;
			var->blue.length    = 5;
			break;
		}
		var->blue.offset   = 0;
		var->green.offset  = var->blue.offset  + var->blue.length;
		var->red.offset    = var->green.offset + var->green.length;
		var->transp.offset = var->red.offset   + var->red.length;
	}

	return 0;
}

static int ssd1963_fb_check_var(struct fb_var_screeninfo *var,
				struct fb_info *info)
{
	/* info input, var output */
	enum ssd_err err;
	int xres, yres;
	u32 pclk;

	print_debug("check_var info(%p) %dx%d (%dx%d), %d, %d\n", info,
		info->var.xres, info->var.yres, info->var.xres_virtual,
		info->var.yres_virtual, (int)info->screen_size,
		info->var.bits_per_pixel);
	print_debug("check_var var(%p) %dx%d (%dx%d), %d\n", var,
		var->xres, var->yres, var->xres_virtual, var->yres_virtual,
		var->bits_per_pixel);

	if (!var->bits_per_pixel)
		var->bits_per_pixel = 16;

	if (ssd1963_fb_set_bitfields(var) != 0) {
		pr_err("check_var: invalid bits_per_pixel %d\n",
			var->bits_per_pixel);
		return -EINVAL;
	}

	if (var->xoffset < 0)
		var->xoffset = 0;
	if (var->yoffset < 0)
		var->yoffset = 0;

	if (1 || var->xres_virtual < var->xres)
		var->xres_virtual = var->xres;
	/* use highest possible virtual resolution */
	if (var->yres_virtual == -1) {
		var->yres_virtual = SSD1963_MAX_HEIGHT;
		pr_err("bcm2708_fb_check_var: virtual resolution set to "
			"maximum of %dx%d\n",
			var->xres_virtual, var->yres_virtual);
	}
	if (1 || var->yres_virtual < var->yres)
		var->yres_virtual = var->yres;

	if (var->xres_virtual > SSD1963_MAX_WIDTH) {
		pr_err("ssd1963_fb_check_var: ERROR: virtual xres (%d) > max. "
			"supported (%d)\n",
			var->xres_virtual, SSD1963_MAX_WIDTH);
		return -EINVAL;
	}
	if (var->yres_virtual > SSD1963_MAX_HEIGHT) {
		pr_err("ssd1963_fb_check_var: ERROR: virtual yres (%d) > max. "
			"supported (%d)\n",
			var->yres_virtual, SSD1963_MAX_HEIGHT);
		return -EINVAL;
	}

	/* truncate xoffset and yoffset to maximum if too high */
	if (var->xoffset > var->xres_virtual - var->xres)
		var->xoffset = var->xres_virtual - var->xres - 1;
	if (var->yoffset > var->yres_virtual - var->yres)
		var->yoffset = var->yres_virtual - var->yres - 1;

	xres = var->xres;
	yres = var->yres;
	/*
	if (var->vmode & FB_VMODE_DOUBLE)
		yres *= 2;
	else if (var->vmode & FB_VMODE_INTERLACED)
		yres = (yres + 1) / 2;*/

	if (xres > this_fb.pdata->lcd.hori.visible) {
		pr_err("check_var: ERROR: horizontal total (%d) > display size "
			"(%d); ",
			xres, this_fb.pdata->lcd.hori.visible);
		return -EINVAL;
	}
	if (yres > this_fb.pdata->lcd.vert.visible) {
		pr_err("check_var: ERROR: vertical total (%d) > display size "
			"(%d); ",
			yres, this_fb.pdata->lcd.vert.visible);
		return -EINVAL;
	}

	this_fb.iv.lcd_flags = this_fb.pdata->lcd.lcd_flags; /* TODO: to init() */

	ssd_iv_set_hsync(&this_fb.iv, xres, var->left_margin,
			 var->hsync_len, var->right_margin, 0, 0);
	ssd_iv_set_vsync(&this_fb.iv, yres, var->upper_margin,
			 var->vsync_len, var->lower_margin, 0);

	/* TODO: needs _valid_ PLL settings to be valid in iv */

	if (PICOS2KHZ(var->pixclock) >= SSD1963_MAX_DOTCLK) {
		/* pixfreq definitely larger than 1 << 17, so calc below would
		 * overflow and such high freqs aren't supported anyway */
		return -EINVAL;
	}
	/* fixed point .20 bit fraction < 1 as expected by hw */
	this_fb.iv.lshift_mult = ssd_iv_calc_lshift_mult(&this_fb.iv,
	                                                 PICOS2KHZ(var->pixclock));

	/* re-check that the new value still matches the monitor spec */
	pclk = ssd_iv_get_pixel_freq_frac(&this_fb.iv); /* kHz */
	var->pixclock = KHZ2PICOS(pclk);
	pclk *= 1000; /* in Hz */
	/* done by fb backend using the monitor spec? */
	if ((this_fb.pdata->lcd.pxclk_min && pclk < this_fb.pdata->lcd.pxclk_min) ||
	    (this_fb.pdata->lcd.pxclk_max && pclk > this_fb.pdata->lcd.pxclk_max)) {
		pr_err("check_var: ERROR: pixel clock (%u) out of valid range "
			"[%u,%u] for display\n",
			pclk,
			this_fb.pdata->lcd.pxclk_min, this_fb.pdata->lcd.pxclk_max);
		return -EINVAL;
	}

	err = ssd_iv_check(&this_fb.iv);
	if (err != SSD_ERR_NONE) {
		pr_err("check_var: ERROR: iv invalid: %s\n", ssd_strerr(err));
		return -EINVAL;
	}

	return 0;
}

static int ssd1963_fb_set_par(struct fb_info *info)
{
	const struct ssd_init_vector *iv = &this_fb.iv;
	enum ssd_err err;

	print_debug("set_par info(%p) %dx%d (%dx%d), %d, %d\n", info,
		info->var.xres, info->var.yres, info->var.xres_virtual,
		info->var.yres_virtual, (int)info->screen_size,
		info->var.bits_per_pixel);

	ssd_iv_print(iv);

#if 1
	err = ssd_init_display(iv);
	print_debug("init_display: %s\n", ssd_strerr(err));
#else
	/* equiv to ssd_init_display() */

	SSD_SET_LSHIFT_FREQ(iv->lshift_mult - 1);

	SSD_SET_LCD_MODE(
		(iv->lcd_flags >> 16),
		(iv->lcd_flags >>  8) & 0xff,
		iv->hdp - 1,
		iv->vdp - 1,
		(iv->lcd_flags      ) & 0xff);

	SSD_SET_HORI_PERIOD(
		iv->ht - 1,
		iv->hps + (iv->lcd_flags & SSD_LCD_MODE_SERIAL ? iv->lpspp : 0),
		iv->hpw - 1,
		iv->lps,
		iv->lpspp);

	SSD_SET_VERT_PERIOD(
		iv->vt - 1,
		iv->vps,
		iv->vpw - 1,
		iv->fps);
#endif

	if (info->var.bits_per_pixel <= 8)
		this_fb.info.fix.visual = FB_VISUAL_PSEUDOCOLOR;
	else
		this_fb.info.fix.visual = FB_VISUAL_TRUECOLOR;

	/* ensure fb_read/fb_write get called by fbmem */
	this_fb.info.screen_base = (__iomem char *)1;
	this_fb.info.fix.line_length = (this_fb.info.var.bits_per_pixel + 7) / 8 * this_fb.info.var.xres_virtual;
	this_fb.info.screen_size = this_fb.info.fix.line_length * this_fb.info.var.yres_virtual;

	return 0;
}

/* SSD_DATA_8        : [rgb].length >= 888, bbp >= 24
 * SSD_DATA_9        : [rgb].length >= 666, bpp >= 18 (24)
 * SSD_DATA_12       : [rgb].length >= 888, bpp >= 24
 * SSD_DATA_16_PACKED: [rgb].length >= 888, bpp >= 24
 * SSD_DATA_16_565   : [rgb].length >= 565, bpp >= 16
 * SSD_DATA_18       : [rgb].length >= 666, bpp >= 18 (24)
 * SSD_DATA_24       : [rgb].length >= 888, bpp >= 24 */
#if 0
static void ssd1963_wr_px8(u32 color)
{
	ssd1963_wr_data((color >> 16) & 0xff);
	ssd1963_wr_data((color >>  8) & 0xff);
	ssd1963_wr_data((color >>  0) & 0xff);
}

static void ssd1963_wr_px9(u32 color)
{
	ssd1963_wr_data((color >> 16-( 9-6)) & 0x1f8 | (color >>  8-(3-6)) & 0x007);
	ssd1963_wr_data((color >>  8-(12-6)) & 0x1c0 | (color >>  0+(8-6)) & 0x03f);
}

static void ssd1963_wr_px12(u32 color)
{
	ssd1963_wr_data((color >> 16-(12-8)) & 0xff0 | (color >>  8-(4-8)) & 0x00f);
	ssd1963_wr_data((color >>  8-(16-8)) & 0xf00 | (color >>  0-(8-8)) & 0x0ff);
}

static void ssd1963_wr_px16_2(u32 color1, u32 color2)
{
	ssd1963_wr_data((color1 >> 8) & 0xffff);
	ssd1963_wr_data((color1 << 8) & 0xff00 | (color2 >> 16) & 0x00ff);
	ssd1963_wr_data((color2     ) & 0xffff);
}

static void ssd1963_wr_px16_565(u16 color)
{
	ssd1963_wr_data(color);
}

static void ssd1963_wr_px16(u32 color)
{
	ssd1963_wr_data(
		(color >> 16-(16-8)) & 0xf800 |
		(color >>  8-(11-8)) & 0x07e0 |
		(color >>  0-( 8-8)) & 0x001f);
}

static void ssd1963_wr_px18(u32 color)
{
	ssd1963_wr_data(
		(color >> 16-(18-8)) & 0x3f000 |
		(color >>  8-(12-8)) & 0x00fc0 |
		(color >>  0-( 6-8)) & 0x0003f);
}

static void ssd1963_wr_px24(u32 color)
{
	ssd1963_wr_data(color & 0xffffff);
}
#endif

static struct ssd1963_px_wr {
	u8 color1;
	u8 color1_valid : 1;
} wr;

static void ssd1963_px_flush(void)
{
	if (this_fb.pdata->bus_fmt == SSD_DATA_16_PACKED && wr.color1_valid) {
		ssd1963_bus_wr((wr.color1 << 8) & 0xff00);
		wr.color1_valid = !wr.color1_valid;
	}
}

/* TODO? maybe make this a callback funptr in pdata */
static void ssd1963_px_wr(u32 color)
{
	switch (this_fb.pdata->bus_fmt) {
	case SSD_DATA_8:
		ssd1963_bus_wr(color >> 16);
		ssd1963_bus_wr(color >>  8);
		ssd1963_bus_wr(color);
		break;
	case SSD_DATA_9:
		ssd1963_bus_wr(color >> 9);
		ssd1963_bus_wr(color);
	case SSD_DATA_12:
		ssd1963_bus_wr(color >> 12);
		ssd1963_bus_wr(color);
		break;
	case SSD_DATA_16_PACKED:
		if (wr.color1_valid) {
			ssd1963_bus_wr(
				((u16)wr.color1 << 8) |
				((color >> 16) & 0x00ff));
			ssd1963_bus_wr(color);
		} else {
			ssd1963_bus_wr(color >> 8);
			wr.color1 = color;
		}
		wr.color1_valid = !wr.color1_valid;
		break;
	case SSD_DATA_16_565:
	case SSD_DATA_18:
	case SSD_DATA_24:
		ssd1963_bus_wr(color);
		break;
	}
}

static ssize_t ssd1963_fb_read(struct fb_info *info, char __user *buf,
			       size_t count, loff_t *ppos)
{
	print_debug("reading %zu bytes to user %p at %llu\n", count, buf, *ppos);
	*ppos += count;
	return count;
}

static ssize_t ssd1963_fb_write(struct fb_info *info, const char __user *buf,
				size_t count, loff_t *ppos)
{
	print_debug("writing %zu bytes to user %p at %llu\n", count, buf, *ppos);
	*ppos += count;
	return count;
}

static void ssd1963_fb_fillrect(struct fb_info *p, const struct fb_fillrect *rect)
{
	u32 n;

	if (p->state != FBINFO_STATE_RUNNING)
		return;

	if (rect->rop != ROP_COPY)
		printk(KERN_ERR MODULE_NAME " fillrect: unknown rop: %d, "
			"defaulting to ROP_COPY\n",
			rect->rop);

	SSD_SET_PAGE_ADDRESS(rect->dy, rect->dy + rect->height - 1);
	SSD_SET_COLUMN_ADDRESS(rect->dx, rect->dx + rect->width - 1);
	SSD_WRITE_MEMORY_START();

	wr.color1_valid = 0;
	for (n = rect->width * rect->height; n; n--)
		ssd1963_px_wr(rect->color);
	ssd1963_px_flush();
}

static void ssd1963_fb_imageblit(struct fb_info *p, const struct fb_image *image)
{
	const u8 *src = image->data;
	const u32 *palette = (u32 *)p->pseudo_palette;
	u32 color;

	if (p->state != FBINFO_STATE_RUNNING)
		return;

	SSD_SET_PAGE_ADDRESS(image->dy, image->dy + image->height - 1);
	SSD_SET_COLUMN_ADDRESS(image->dx, image->dx + image->width - 1);
	SSD_WRITE_MEMORY_START();

	wr.color1_valid = 0;
	if (image->depth == 1) {
		u32 spitch = (image->width + 7) / 8;
		u32 y, x;
		for (y = image->height; y; y--, src += spitch) {
			const u8 *s = src;
			u8 l = 8;
			for (x = image->width; x; x--) {
				color = (*s & (1 << --l)) ? image->fg_color : image->bg_color;
				ssd1963_px_wr(color);
				if (!l) {
					s++;
					l = 8;
				}
			}
		}
	} else {
		u32 n;
		for (n = image->dx * image->dy; n; n--, src++) {
			if (p->fix.visual == FB_VISUAL_TRUECOLOR ||
			    p->fix.visual == FB_VISUAL_DIRECTCOLOR)
				color = palette[*src];
			else
				color = *src;
			ssd1963_px_wr(color);
		}
	}
	ssd1963_px_flush();
}

static struct {
	unsigned sleep   : 1;
	unsigned idle    : 1;
	unsigned partial : 1;
	unsigned display : 1;
} ssd1963_mode;

static int ssd1963_fb_blank(int blank, struct fb_info *info)
{
	print_debug("blank: %d\n", blank);
	switch (blank) {
	case FB_BLANK_UNBLANK:
		SSD_EXIT_SLEEP_MODE();
		msleep(5);
		break;
	case FB_BLANK_NORMAL:
		SSD_SET_DISPLAY_OFF();
		break;
	case FB_BLANK_POWERDOWN:
		SSD_ENTER_SLEEP_MODE();
		msleep(5);
		break;
	default:
		return 1;
	}
	return 0;
}

static struct fb_ops ssd1963_fb_ops = {
	.owner		= THIS_MODULE,
	.fb_check_var	= ssd1963_fb_check_var,
	.fb_set_par	= ssd1963_fb_set_par,
	//.fb_setcolreg	= ssd1963_fb_setcolreg,
	//.fb_blank	= ssd1963_fb_blank,
	.fb_fillrect	= ssd1963_fb_fillrect,
	.fb_copyarea	= NULL,
	.fb_imageblit	= ssd1963_fb_imageblit,
	/* For framebuffers with strange non linear layouts or that do not
	 * work with normal memory mapped access
	 */
	.fb_read	= ssd1963_fb_read,
	.fb_write	= ssd1963_fb_write,
	/* pan display *//*
	int (*fb_pan_display)(struct fb_var_screeninfo *var, struct fb_info *info);*/
	/* Rotates the display *//*
	void (*fb_rotate)(struct fb_info *info, int angle);*/
	/* wait for blit idle, optional *//*
	int (*fb_sync)(struct fb_info *info);*/
};

static int ssd1963_fb_register(void)
{
	struct ssd1963_fb *fb = &this_fb;
	struct ssd1963_platform_data *pdata = fb->pdata;
	enum ssd_err err;
	int ret;

	fb->info.fbops			= &ssd1963_fb_ops;
	fb->info.flags			= FBINFO_FLAG_DEFAULT
					| FBINFO_HWACCEL_YWRAP;
	fb->info.pseudo_palette		= fb->cmap;

	strncpy(fb->info.fix.id, ssd1963_name, sizeof(fb->info.fix.id));
	fb->info.fix.type		= FB_TYPE_PACKED_PIXELS;
	fb->info.fix.type_aux		= 0;
	fb->info.fix.xpanstep		= 0;
	fb->info.fix.ypanstep		= 0;
	fb->info.fix.ywrapstep		= 1;
	fb->info.fix.accel		= FB_ACCEL_NONE;

	fb->info.var.xres		= pdata->lcd.hori.visible;
	fb->info.var.yres		= pdata->lcd.vert.visible;
#if 0
	fb->info.var.xres_virtual	= SSD1963_MAX_WIDTH;
	fb->info.var.yres_virtual	= SSD1963_MAX_HEIGHT;
#else
	fb->info.var.xres_virtual	= fb->info.var.xres;
	fb->info.var.yres_virtual	= fb->info.var.yres;
#endif
	fb->info.var.bits_per_pixel	= 32;
	fb->info.var.vmode		= FB_VMODE_NONINTERLACED;
	fb->info.var.activate		= FB_ACTIVATE_NOW;
	fb->info.var.nonstd		= 0;
	fb->info.var.height		= -1;	/* height of picture in mm */
	fb->info.var.width		= -1;	/* width of picture in mm */
	fb->info.var.accel_flags	= 0;

	fb->info.var.pixclock		= KHZ2PICOS(pdata->lcd.pxclk_typ / 1000);
	fb->info.var.left_margin	= pdata->lcd.hori.front;
	fb->info.var.right_margin	= pdata->lcd.hori.back;
	fb->info.var.hsync_len		= pdata->lcd.hori.sync;
	fb->info.var.upper_margin	= pdata->lcd.vert.front;
	fb->info.var.lower_margin	= pdata->lcd.vert.back;
	fb->info.var.vsync_len		= pdata->lcd.vert.sync;

	fb->info.monspecs.hfmin		= 0;
	fb->info.monspecs.hfmax		= 100000;
	fb->info.monspecs.vfmin		= 0;
	fb->info.monspecs.vfmax		= 400;
	fb->info.monspecs.dclkmin	= pdata->lcd.pxclk_min; /* Hz */
	fb->info.monspecs.dclkmax	= pdata->lcd.pxclk_max; /* Hz */

	ssd1963_fb_set_bitfields(&fb->info.var);

	fb->iv.lcd_flags		= pdata->lcd.lcd_flags;
	fb->iv.in_clk_freq		= pdata->xtal_freq;
	fb->iv.pll_as_sysclk		= pdata->pll_as_sysclk;
	fb->iv.pll_m			= pdata->pll_m;
	fb->iv.pll_n			= pdata->pll_n;

	/* TODO: ssd_pll_init && set bus_fmt and addr_mode || fail */

	/*
	 * Allocate colourmap.
	 */

	ret = ssd1963_fb_check_var(&fb->info.var, &fb->info);
	print_debug("SSD1963FB: set_var: %d\n", ret);
	if (ret)
		goto fail;

	err = ssd_init_pll(&fb->iv);
	print_debug("init_pll: %s\n", ssd_strerr(err));

	ret = ssd1963_fb_set_par(&fb->info);
	print_debug("SSD1963FB: set_par: %d\n", ret);
	if (ret)
		goto fail;

	ret = register_framebuffer(&fb->info);
	print_debug("SSD1963FB: register framebuffer (%d)\n", ret);
	if (ret == 0)
		goto out;
fail:
	print_debug("SSD1963FB: cannot register framebuffer (%d)\n", ret);
out:
	return ret;
}

static int gpiochip_match(struct gpio_chip *chip, void *data)
{
	return !strcmp(chip->label, data);
}

/* original definition to be found in arch/arm/mach-bcm2708/*.c */
#define BCM2708_GPIO_LABEL	"bcm2708_gpio"

static void ssd1963_gpio_bus_release(struct device *dev, u32 pins)
{
	struct gpio_chip *gpio;
	u32 p;
	int i;

	gpio = gpiochip_find(BCM2708_GPIO_LABEL, gpiochip_match);
	if (!gpio) {
		dev_warn(dev,
			"BUG relasing bus: gpio_chip %s not available\n",
			BCM2708_GPIO_LABEL);
	} else {
		for (i=0, p=pins; p != 0; i++, p >>= 1) {
			if (!(p & 1))
				continue;
			gpio_direction_input(i + gpio->base);
			gpio_free(i + gpio->base);
		}
	}
}

static int ssd1963_gpio_bus_request(struct device *dev, u32 pins, u32 values)
{
	struct gpio_chip *gpio;
	u32 p, v;
	int i;
	int ret = 0;

	gpio = gpiochip_find(BCM2708_GPIO_LABEL, gpiochip_match);
	if (!gpio) {
		dev_err(dev,
			"unable to find gpio_chip with label %s, cannot "
			"reserve bus pins 0x%08x\n", BCM2708_GPIO_LABEL, pins);
		ret = -ENODEV;
		goto out;
	}

	print_debug("requesting gpios for output: %08x\n", pins);
	for (i=0, p=pins, v=values; p != 0; i++, p >>= 1, v >>= 1) {
		if (!(p & 1))
			continue;
		ret = gpio_request(i + gpio->base, NULL);
		if (ret) {
			dev_err(dev,
				"cannot reserve gpio pin %d (%d on %s): %d\n",
				i + gpio->base, i, gpio->label, ret);
			/* failed to request some gpio pins, roll back */
			ssd1963_gpio_bus_release(dev, pins ^ (p << i));
			goto out;
		}
		/* all pins are outputs */
		gpio_direction_output(i + gpio->base, v & 1);
	}
out:
	return ret;
}

/* TODO: rename BUS_MASK to BUS_DATA_MASK, BUS() to BUS_DATA() and rework the
 *       below gpio pin code */

static int ssd1963_fb_probe(struct platform_device *pdev)
{
	struct ssd1963_platform_data *pdata = pdev->dev.platform_data;
	int ret;

	if (!pdata) {
		dev_err(&pdev->dev,
			"no platform data supplied to initialize display "
			"controller in " DRIVER_NAME " driver\n");
		ret = -EINVAL;
		goto fail;
	}

	/* Only gpio management is done via gpiolib, actual pin toggling to send
	 * commands to the ssd1963 controller is done via hardware registers for
	 * speed since:
	 * 1. gpiolib only supports setting one pin at a time
	 * 2. bcm2708's <mach/gpio.h> doesn't even provide inline setters */
	ret = ssd1963_gpio_bus_request(&pdev->dev,
				       BUS_CTL_MASK | BUS_MASK,
				       BUS_CTL_MASK | 0); /* 0 is SSD_NOP */
	if (ret)
		goto fail;

	memset(&this_fb, 0, sizeof(struct ssd1963_fb));
	this_fb.dev = pdev;
	this_fb.pdata = pdata;

	ret = ssd1963_fb_register();
	if (ret)
		goto release_gpios;

	// platform_set_drvdata(pdev, fb);
	goto done;

// free_region:
release_gpios:
	ssd1963_gpio_bus_release(&pdev->dev, BUS_CTL_MASK | BUS_MASK);
fail:
	dev_err(&pdev->dev, "probe failed, err %d\n", ret);
done:
	return ret;
}

static int ssd1963_fb_remove(struct platform_device *pdev)
{
	// struct ssd1963_fb *fb = platform_get_drvdata(pdev);

	// platform_set_drvdata(pdev, NULL);

	unregister_framebuffer(&this_fb.info);

	ssd1963_gpio_bus_release(&pdev->dev, BUS_CTL_MASK | BUS_MASK);

	// kfree(fb);

	dev_info(&pdev->dev, DRIVER_NAME " removed");

	return 0;
}

static struct platform_driver ssd1963_fb_driver = {
	.probe = ssd1963_fb_probe,
	.remove = ssd1963_fb_remove,
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
	},
};

#include "../ssd1963/itdb02.h" /* TODO */

static struct ssd1963_platform_data ssd_pdev_data = {
	.lcd		= HSD050IDW1_A,
	.lcd_addr_mode	= 0,
	.bus_fmt	= SSD_DATA_8,
	.xtal_freq	= ITDB02_XTAL_FREQ / 1000, /* kHz */
	.pll_m		= 40,
	.pll_n		= 5,
	.pll_as_sysclk	= 1,
};

static void ssd_pdev_release(struct device *dev)
{
	(void)dev;
}

static struct platform_device ssd_pdev = {
	.name			= SSD1963_FB_DRIVER_NAME,
	.id			= -1,
	.dev.platform_data	= &ssd_pdev_data,
	.dev.release		= ssd_pdev_release,
};

static int __init ssd1963_fb_init(void)
{
	int err = 0;

	err = platform_device_register(&ssd_pdev);

	if (!err)
		err = platform_driver_register(&ssd1963_fb_driver);

	pr_info("ssd1963 platform driver: return status %d\n", err);

	return err;
}

module_init(ssd1963_fb_init);

static void __exit ssd1963_fb_exit(void)
{
	platform_driver_unregister(&ssd1963_fb_driver);
	platform_device_unregister(&ssd_pdev);
}

module_exit(ssd1963_fb_exit);

MODULE_DESCRIPTION("SSD1963 framebuffer driver");
MODULE_LICENSE("GPL");
