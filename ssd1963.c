/*
 * Copyright (c) 2013, Franz Brauße
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 * - Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <linux/delay.h>

#include "ssd1963_fb.h"

#define SSD_WR_CMD(x)	ssd_wr_slow_cmd(x)
#define SSD_WR_DATA(x)	ssd_wr_slow_data(x)

#include "ssd1963_cmd.h"

/* 0 <= ms < 1000 */
static void ssd_sleep(unsigned ms)
{
	msleep(ms);
}

#define STR(x)			#x
#define XSTR(x)			STR(x)

/* in kHz */
#define SSD_VCO_MIN		250000
#define SSD_VCO_MAX		800000
#define SSD_SYS_MIN		  1000
#define SSD_SYS_MAX		110000

uint_least32_t ssd_iv_get_vco_freq(const struct ssd_init_vector *iv)
{
	return iv->in_clk_freq * iv->pll_m;
}

uint_least32_t ssd_iv_get_pll_freq(const struct ssd_init_vector *iv)
{
	return ssd_iv_get_vco_freq(iv) / iv->pll_n;
}

uint_least32_t ssd_iv_get_sys_freq(const struct ssd_init_vector *iv)
{
	return iv->pll_as_sysclk ? ssd_iv_get_pll_freq(iv) : iv->in_clk_freq;
}

/* kHz, 19 bit */
uint_least32_t ssd_iv_get_pixel_freq_frac(const struct ssd_init_vector *iv)
{
	uint_least32_t f;

	f   = ssd_iv_get_vco_freq(iv); /* kHz */
	if (f > SSD_VCO_MAX)           /* 20 bit */
		return 0;
	f >>= 8;               /*  2^8 kHz, 12 bit */
	f  *= iv->lshift_mult; /* (2^8 kHz)/2^20 = 2^(-12) kHz, 32 bit */
	f  /= iv->pll_n;       /*  2^(-12) kHz, 32 bit (will be less) */
	f   = (f + (1 << 11)) >> 12; /* kHz, 20 bit */

	if (iv->lcd_flags & SSD_LCD_MODE_SERIAL)
		f <<= 2;       /* kHz, 22 bit */

	return f;
}

uint_least32_t ssd_iv_calc_pixel_freq(
	const struct ssd_init_vector *iv,
	uint_least16_t refresh_rate
) {
	return (uint_least32_t)refresh_rate * iv->ht * iv->vt / 1000; /* kHz */
}

uint_least32_t ssd_iv_calc_lshift_mult(
	const struct ssd_init_vector *iv,
	uint_least32_t pixel_freq
) {
	uint_least32_t pclk = pixel_freq;
	uint_least32_t pll_32;
	uint_least32_t frac;

	if (iv->lcd_flags & SSD_LCD_MODE_SERIAL) /* 2^(-2) kHz, 19 bit */
		pclk <<= 15 - 2;                 /* 2^(-15) kHz, 32 bit */
	else                                     /* 2^(0) kHz, 17 bit */
		pclk <<= 15;                     /* 2^(-15) kHz, 32 bit */

	pll_32 = ssd_iv_get_pll_freq(iv) >> 5;   /* 2^5 kHz, 17-5 = 12 bit */
	/* divide scaled pixel clock by PLL freq:
	 * (pclk * 2^(-15) kHz)/(pll * 2^5 kHz) = 2^(-20), 20 bit */
	frac = pclk / pll_32;

	return frac;
}

void ssd_iv_set_hsync(
	struct ssd_init_vector *iv,
	unsigned display, unsigned front, unsigned sync, unsigned back,
	unsigned sync_move, unsigned lpspp
) {
	iv->hdp = display;
	iv->ht = display + front + sync + back;
	iv->hpw = sync;
	iv->hps = sync_move + sync + back;
	iv->lps = sync_move;
	iv->lpspp = lpspp;
}

void ssd_iv_set_vsync(
	struct ssd_init_vector *iv,
	unsigned display, unsigned front, unsigned sync, unsigned back,
	unsigned sync_move
) {
	iv->vdp = display;
	iv->vt = display + front + sync + back;
	iv->vpw = sync;
	iv->vps = sync_move + sync + back;
	iv->fps = sync_move;
}

void ssd_iv_set_display(struct ssd_init_vector *iv, const struct ssd_display *d)
{
	iv->lcd_flags = d->lcd_flags;
	ssd_iv_set_hsync(iv,
		d->hori.visible, d->hori.front, d->hori.sync, d->hori.back,
		0, 0);
	ssd_iv_set_vsync(iv,
		d->vert.visible, d->vert.front, d->vert.sync, d->vert.back,
		0);
}

void ssd_iv_print(const struct ssd_init_vector *iv)
{
	uint_least32_t pclk      = ssd_iv_get_pixel_freq_frac(iv); /* 19 bit */
	uint_least32_t pclk_hz   =  pclk * 1000; /* 29 bit */
	uint_least32_t pclk_hz_d =  pclk_hz / (iv->ht * iv->vt);
	uint_least32_t pclk_hz_m =  pclk_hz % (iv->ht * iv->vt);
	uint_least32_t pclk_hz_f = (pclk_hz_m * 1000) / (iv->ht * iv->vt);

	printk(KERN_INFO SSD1963_FB_DRIVER_NAME ": "
		"in_clk_freq: %u kHz, "
		"PLL: %u/%u -> %u kHz, "
		"VCO: %u kHz\n",
		iv->in_clk_freq,
		iv->pll_m, iv->pll_n, ssd_iv_get_pll_freq(iv),
		ssd_iv_get_vco_freq(iv));

	printk(KERN_INFO SSD1963_FB_DRIVER_NAME ": "
		"sys: %u kHz, "
		"px clk: %u kHz, "
		"lshift: %u, rate: %u.%03u Hz\n",
		ssd_iv_get_sys_freq(iv),
		pclk,
		iv->lshift_mult, pclk_hz_d, pclk_hz_f);

	printk(KERN_INFO SSD1963_FB_DRIVER_NAME ": "
		"ht, hps, hpw, lps, lpspp: %hu %hu %hu %hu %hu\n",
		iv->ht, iv->hps, iv->hpw, iv->lps, iv->lpspp);

	printk(KERN_INFO SSD1963_FB_DRIVER_NAME ": "
		"vt, vps, vpw, fps       : %hu %hu %hu %hu\n",
		iv->vt, iv->vps, iv->vpw, iv->fps);

	printk(KERN_INFO SSD1963_FB_DRIVER_NAME ": "
		"hdp: %hu, "
		"vdp: %hu, "
		"lcd-flags: 0x%02x 0x%02x 0x%02x\n",
		iv->hdp,
		iv->vdp,
		iv->lcd_flags >> 16, (iv->lcd_flags >> 8) & 0xff, iv->lcd_flags & 0xff);
}

const char * ssd_strerr(enum ssd_err err)
{
	static char buf[64];
	static const char *err_msgs[] = {
		[SSD_ERR_NONE]
		= "success",
		[SSD_ERR_VCO_OOR]
		= "VCO frequency out of range (" XSTR(SSD_VCO_MIN) "," XSTR(SSD_VCO_MAX) ") Hz",
		[SSD_ERR_SYS_CLK_OOR]
		= "system clock frequency out of range [" XSTR(SSD_SYS_MIN) "," XSTR(SSD_SYS_MAX) "] Hz",
		[SSD_ERR_PLL_M_OOR]
		= "PLL multiplier m out representable range",
		[SSD_ERR_PLL_N_OOR]
		= "PLL divider n out of representable range",
		[SSD_ERR_LSHIFT_OOR]
		= "LSHIFT multiplier out of representable range",
		[SSD_ERR_PLL_UNSTABLE]
		= "PLL did not stabilize",
		[SSD_ERR_PXCLK_UNAVAIL]
		= "refresh_rate not set and no typical pixel clock frequency available for the display",
		[SSD_ERR_PXCLK_OOR]
		= "pixel clock frequency out of range for the display"
	};

	if (err < ARRAY_SIZE(err_msgs))
		return err_msgs[err];

	snprintf(buf, sizeof(buf),
		"unknown error value %d: out of valid range",
		err);
	return buf;
}

enum ssd_err ssd_iv_init(
	struct ssd_init_vector *iv,
	uint_least32_t in_clk_freq,
	uint_least8_t pll_m, uint_least8_t pll_n, char pll_as_sysclk,
	const struct ssd_display *d, uint_least16_t refresh_rate
) {
	enum ssd_err r = SSD_ERR_NONE;
	uint_least32_t pclk;

	iv->in_clk_freq   = in_clk_freq;
	iv->pll_m         = pll_m;
	iv->pll_n         = pll_n;
	iv->pll_as_sysclk = pll_as_sysclk;

	ssd_iv_set_display(iv, d);

	r = ssd_iv_check(iv);
	if (r == SSD_ERR_LSHIFT_OOR)
		r = SSD_ERR_NONE;
	if (r != SSD_ERR_NONE)
		return r;

	if (refresh_rate)
		pclk      = ssd_iv_calc_pixel_freq(iv, refresh_rate); /* kHz */
	else if (d->pxclk_typ)
		pclk      = d->pxclk_typ;                             /* kHz */
	else
		return SSD_ERR_PXCLK_UNAVAIL;

	if ((d->pxclk_min && pclk * 1000 < d->pxclk_min) ||
	    (d->pxclk_max && pclk * 1000 > d->pxclk_max))
		r = SSD_ERR_PXCLK_OOR;

	iv->lshift_mult   = ssd_iv_calc_lshift_mult(iv, pclk);
	pclk              = ssd_iv_get_pixel_freq_frac(iv);           /* kHz */

	if ((d->pxclk_min && pclk * 1000 < d->pxclk_min) ||
	    (d->pxclk_max && pclk * 1000 > d->pxclk_max))
		r = SSD_ERR_PXCLK_OOR;

	return r;
}

enum ssd_err ssd_iv_check(const struct ssd_init_vector *iv)
{
	uint_least32_t vco;

	if (!iv->pll_m || iv->pll_m - 1 >= 1 << 8)
		return SSD_ERR_PLL_M_OOR;
	if (!iv->pll_n || iv->pll_n - 1 >= 1 << 4)
		return SSD_ERR_PLL_N_OOR;

	vco = ssd_iv_get_vco_freq(iv);
	if (!(SSD_VCO_MIN < vco && vco < SSD_VCO_MAX))
		return SSD_ERR_VCO_OOR;
	if (ssd_iv_get_sys_freq(iv) > SSD_SYS_MAX)
		return SSD_ERR_SYS_CLK_OOR;

	if (!iv->lshift_mult || iv->lshift_mult - 1 >= (uint_least32_t)1 << 19)
		return SSD_ERR_LSHIFT_OOR;

	return SSD_ERR_NONE;
}

enum ssd_err ssd_init_pll(const struct ssd_init_vector *iv)
{
	enum ssd_err r = SSD_ERR_NONE;

	r = ssd_iv_check(iv);
	if (r != SSD_ERR_NONE)
		return r;

	SSD_SET_DISPLAY_OFF();

	/* disable usage of PLL as system clock and the PLL itself */
	SSD_SET_PLL(0x00);

	SSD_SET_PLL_MN(
		iv->pll_m - 1,
		iv->pll_n - 1,
		0x04);         /* effectuate PLL settings */

	/* enable PLL and wait 100ms for it to settle */
	SSD_SET_PLL(0x01);
	ssd_sleep(100);

#ifdef SSD_RD_DATA
	SSD_GET_PLL_STATUS();
	if (!(SSD_RD_DATA() & 0x04)) {
		/* PLL unstable, deactivate it */
		SSD_SET_PLL(0x00);
		return SSD_ERR_PLL_UNSTABLE;
	}
#endif

	/* ok, PLL stable, use as system clock if requested */
	if (iv->pll_as_sysclk)
		SSD_SET_PLL(0x03);

	/* need to wait 5ms after soft reset */
	SSD_SOFT_RESET();
	ssd_sleep(5);

	return r;
}

enum ssd_err ssd_init_display(const struct ssd_init_vector *iv)
{
	enum ssd_err r = SSD_ERR_NONE;

	r = ssd_iv_check(iv);
	if (r != SSD_ERR_NONE)
		return r;

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

	SSD_SET_DISPLAY_ON();

	return r;
}
