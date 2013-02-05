
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

#include "ssd1963_fb.h"

#if 1
static void ssd1963_wr_cmd(u8 x) {}
static void ssd1963_wr_data(u8 x) {}

void ssd_wr_slow_cmd(u8 x) {}
void ssd_wr_slow_data(u8 x) {}

#define SSD_WR_CMD(x)	ssd1963_wr_cmd(x)
#define SSD_WR_DATA(x)	ssd1963_wr_data(x)

#include "ssd1963_cmd.h"
#endif

#ifdef SSD1963_FB_DEBUG
#define print_debug(fmt,...) pr_debug("%s:%s:%d: "fmt, MODULE_NAME, __func__, __LINE__, ##__VA_ARGS__)
#else
#define print_debug(fmt,...)
#endif

/* This is limited to 16 characters when displayed by X startup */
static const char *ssd1963_name = "SSD1963 FB";

#define DRIVER_NAME		SSD1963_FB_DRIVER_NAME

struct ssd1963_fb {
	struct fb_info info;
	struct platform_device *dev;
	struct ssd1963_platform_data *pdata;
	struct ssd_init_vector iv;
	u32 cmap[16];
};

#define to_ssd1963_fb(fbinfo)	container_of(fbinfo,struct ssd1963_fb,info)

static int ssd1963_fb_set_bitfields(struct fb_var_screeninfo *var)
{
	int ret = 0;

	var->red.msb_right = 0;
	var->green.msb_right = 0;
	var->blue.msb_right = 0;
	var->transp.msb_right = 0;

	var->transp.length = 0;

	switch (var->bits_per_pixel) {
	case 32:
		var->transp.length  = 8;
	case 24:
	case 1:
	case 2:
	case 4:
	case 8:
		var->red.length     = 8;
		var->green.length   = 8;
		var->blue.length    = 8;
		break;
	/*
	case 18:
		var->red.length     = 6;
		var->green.length   = 6;
		var->blue.length    = 6;
		break;*/
	case 16:
		var->red.length     = 5;
		var->green.length   = 6;
		var->blue.length    = 5;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	if (!ret && var->bits_per_pixel > 8) {
		var->blue.offset   = 0;
		var->green.offset  = var->blue.offset  + var->blue.length;
		var->red.offset    = var->green.offset + var->green.length;
		var->transp.offset = var->red.offset   + var->red.length;
	} else if (!ret) {
		var->blue.offset   = 0;
		var->green.offset  = 0;
		var->red.offset    = 0;
		var->transp.offset = 0;
	}

	return ret;
}

static int ssd1963_fb_check_var(struct fb_var_screeninfo *var,
				struct fb_info *info)
{
	/* info input, var output */
	struct ssd1963_fb *priv = to_ssd1963_fb(info);
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

	if (xres > priv->pdata->lcd.hori.visible) {
		pr_err("check_var: ERROR: horizontal total (%d) > display size "
			"(%d); ",
			xres, priv->pdata->lcd.hori.visible);
		return -EINVAL;
	}
	if (yres > priv->pdata->lcd.vert.visible) {
		pr_err("check_var: ERROR: vertical total (%d) > display size "
			"(%d); ",
			yres, priv->pdata->lcd.vert.visible);
		return -EINVAL;
	}

	priv->iv.lcd_flags = priv->pdata->lcd.lcd_flags; /* TODO: to init() */

	ssd_iv_set_hsync(&priv->iv, xres, var->left_margin,
			 var->hsync_len, var->right_margin, 0, 0);
	ssd_iv_set_vsync(&priv->iv, yres, var->upper_margin,
			 var->vsync_len, var->lower_margin, 0);

	/* TODO: needs _valid_ PLL settings to be valid in iv */
	/* 1e12 (ps/s) / pixclock (ps) / pll_hz (1/s) in 0.20 fixed point */

	if (PICOS2KHZ(var->pixclock) >= SSD1963_MAX_DOTCLK / 1000) {
		/* pixfreq definitely larger than 1 << 17, so calc below would
		 * overflow and such high freqs aren't supported anyway */
		return -EINVAL;
	}
	/* 15+5 results in fixed point .20 bit fraction < 1 as expected by hw */
	priv->iv.lshift_mult = (PICOS2KHZ(var->pixclock) << 15)
			     / (ssd_iv_get_pll_freq(&priv->iv) / (1000 << 5));

	/* re-check that the new value still matches the monitor spec */
	pclk = ssd_iv_get_pixel_freq_frac(&priv->iv) >> 20;
	var->pixclock = KHZ2PICOS(pclk / 1000);
	/* done by fb backend using the monitor spec? */
	if ((priv->pdata->lcd.pxclk_min && pclk < priv->pdata->lcd.pxclk_min) ||
	    (priv->pdata->lcd.pxclk_max && pclk > priv->pdata->lcd.pxclk_max)) {
		pr_err("check_var: ERROR: pixel clock (%u) out of valid range "
			"[%u,%u] for display\n",
			pclk,
			priv->pdata->lcd.pxclk_min, priv->pdata->lcd.pxclk_max);
		return -EINVAL;
	}

	err = ssd_iv_check(&priv->iv);
	if (err != SSD_ERR_NONE) {
		pr_err("check_var: ERROR: iv invalid: %s\n", ssd_strerr(err));
		return -EINVAL;
	}

	return 0;
}

static int ssd1963_fb_set_par(struct fb_info *info)
{
	struct ssd1963_fb *fb = to_ssd1963_fb(info);
	const struct ssd_init_vector *iv = &fb->iv;

	print_debug("set_par info(%p) %dx%d (%dx%d), %d, %d\n", info,
		info->var.xres, info->var.yres, info->var.xres_virtual,
		info->var.yres_virtual, (int)info->screen_size,
		info->var.bits_per_pixel);

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

	if (info->var.bits_per_pixel <= 8)
		fb->info.fix.visual = FB_VISUAL_PSEUDOCOLOR;
	else
		fb->info.fix.visual = FB_VISUAL_TRUECOLOR;

	return 0;
}

static struct fb_ops ssd1963_fb_ops = {
	.owner		= THIS_MODULE,
	.fb_check_var	= ssd1963_fb_check_var,
	.fb_set_par	= ssd1963_fb_set_par,
	//.fb_setcolreg	= ssd1963_fb_setcolreg,
	//.fb_blank	= ssd1963_fb_blank,
	.fb_fillrect	= cfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
	/* For framebuffers with strange non linear layouts or that do not
	 * work with normal memory mapped access
	 *//*
	ssize_t (*fb_read)(struct fb_info *info, char __user *buf,
			   size_t count, loff_t *ppos);
	ssize_t (*fb_write)(struct fb_info *info, const char __user *buf,
			    size_t count, loff_t *ppos);*/
	/* pan display *//*
	int (*fb_pan_display)(struct fb_var_screeninfo *var, struct fb_info *info);*/
	/* Rotates the display *//*
	void (*fb_rotate)(struct fb_info *info, int angle);*/
	/* wait for blit idle, optional *//*
	int (*fb_sync)(struct fb_info *info);*/
};

static int ssd1963_fb_register(struct ssd1963_fb *fb)
{
	struct ssd1963_platform_data *pdata = fb->pdata;
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
	fb->info.var.vmode		= FB_VMODE_NONINTERLACED;
	fb->info.var.activate		= FB_ACTIVATE_NOW;
	fb->info.var.nonstd		= 0;
	fb->info.var.height		= -1;	/* height of picture in mm */
	fb->info.var.width		= -1;	/* width of picture in mm */
	fb->info.var.accel_flags	= 0;

	fb->info.monspecs.hfmin		= 0;
	fb->info.monspecs.hfmax		= 100000;
	fb->info.monspecs.vfmin		= 0;
	fb->info.monspecs.vfmax		= 400;
	fb->info.monspecs.dclkmin	= pdata->lcd.pxclk_min;
	fb->info.monspecs.dclkmax	= pdata->lcd.pxclk_max;

	ssd1963_fb_set_bitfields(&fb->info.var);

	/*
	 * Allocate colourmap.
	 */

	fb_set_var(&fb->info, &fb->info.var);

	print_debug("BCM2708FB: registering framebuffer (%dx%d@%d)\n", fbwidth,
		fbheight, fbdepth);

	ret = register_framebuffer(&fb->info);
	print_debug("BCM2708FB: register framebuffer (%d)\n", ret);
	if (ret == 0)
		goto out;

	print_debug("BCM2708FB: cannot register framebuffer (%d)\n", ret);
out:
	return ret;
}

static int ssd1963_fb_probe(struct platform_device *pdev)
{
	struct ssd1963_fb *fb;
	struct ssd1963_platform_data *pdata = pdev->dev.platform_data;
	int ret;

	if (!pdata) {
		dev_err(&pdev->dev,
			"no platform data supplied to initialize display "
			"controller in " DRIVER_NAME " driver\n");
		ret = -EINVAL;
		goto free_region;
	}

	fb = kmalloc(sizeof(struct ssd1963_fb), GFP_KERNEL);
	if (!fb) {
		dev_err(&pdev->dev,
			"could not allocate new ssd1963_fb struct\n");
		ret = -ENOMEM;
		goto free_region;
	}
	memset(fb, 0, sizeof(struct ssd1963_fb));

	fb->dev = pdev;
	fb->pdata = pdata;

	ret = ssd1963_fb_register(fb);
	if (ret == 0) {
		platform_set_drvdata(pdev, fb);
		goto out;
	}

	kfree(fb);
free_region:
	dev_err(&pdev->dev, "probe failed, err %d\n", ret);
out:
	return ret;
}

static int ssd1963_fb_remove(struct platform_device *dev)
{
	struct ssd1963_fb *fb = platform_get_drvdata(dev);

	platform_set_drvdata(dev, NULL);

	unregister_framebuffer(&fb->info);

	kfree(fb);

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

static int __init ssd1963_fb_init(void)
{
	return platform_driver_register(&ssd1963_fb_driver);
}

module_init(ssd1963_fb_init);

static void __exit ssd1963_fb_exit(void)
{
	platform_driver_unregister(&ssd1963_fb_driver);
}

module_exit(ssd1963_fb_exit);

MODULE_DESCRIPTION("SSD1963 framebuffer driver");
MODULE_LICENSE("GPL");
