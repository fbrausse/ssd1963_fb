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

#ifndef SSD1963_H
#define SSD1963_H

/* flags for SSD_SET_ADDRESS_MODE */
enum ssd_address_mode {
	SSD_ADDR_HOST_VERT_REVERSE    = 1 << 7, /* host->SSD: bottom to top */
	SSD_ADDR_HOST_HORI_REVERSE    = 1 << 6, /* host->SSD: EC to SC */
	SSD_ADDR_PANEL_COLUMN_REVERSE = 1 << 5, /* SSD->panel */
	SSD_ADDR_PANEL_LINE_REVERSE   = 1 << 4, /* SSD->panel */
	SSD_ADDR_PANEL_COLOR_REVERSE  = 1 << 3, /* SSD->panel: BGR instead of RGB */
	SSD_ADDR_PANEL_LINE_REFRESH_REVERSE = 1 << 2, /* SSD->panel: refresh from right to left side */
	SSD_ADDR_PANEL_HORI_FLIP      = 1 << 1,
	SSD_ADDR_PANEL_VERT_FLIP      = 1 << 0,
};

/* mode for SSD_SET_PIXEL_DATA_INTERFACE */
enum ssd_interface_fmt {
	SSD_DATA_8 = 0,          /* R[7:0]
	                          * G[7:0]
	                          * B[7:0] */

	SSD_DATA_12 = 1,         /* R[7:0] G[7:4]
	                          * G[3:0] B[7:0] */

	SSD_DATA_16_PACKED = 2,  /* R1[7:0] G1[7:0]
	                          * B1[7:0] R2[7:0]
	                          * G2[7:0] B2[7:0] */

	SSD_DATA_16_565 = 3,     /* R[5:1] G[5:0] B[5:1] */

	SSD_DATA_18 = 4,         /* R[5:0] G[5:0] B[5:0] */

	SSD_DATA_24 = 5,         /* R[7:0] G[7:0] B[7:0] */

	SSD_DATA_9 = 6,          /* R[5:0] G[5:3]
	                          * G[2:0] B[5:0] */
};

/* Some values are transmitted to the controller decremented by one; while the
 * above SSD_* command macros take those decremented parameters as are descibed
 * in the datasheet, the init functions will take care about decrementing these
 * appropriately because in this structure the more sensible non-adjusted
 * values are stored to ease calculations. */
struct ssd_init_vector {
	uint_least32_t in_clk_freq; /* kHz, XTAL or ext. clock input, both OR'ed in chip */
	uint_least8_t pll_m, pll_n;
	uint_least16_t ht, hps, hpw, lps, lpspp;
	uint_least16_t vt, vps, vpw, fps;
	uint_least16_t hdp, vdp; /* horizontal / vertical panel size */
	uint_least32_t lshift_mult;
	enum ssd_lcd_flags {
		SSD_LCD_18BIT             = 0 << (16+5),
		SSD_LCD_24BIT             = 1 << (16+5),
		SSD_LCD_COL_DEPTH_ENH_EN  = 1 << (16+4), /* only for 18 bit */
		SSD_LCD_DITHERING         = 0 << (16+3), /* only for 18 bit */
		SSD_LCD_FRC               = 1 << (16+3), /* only for 18 bit */
		SSD_LCD_LSHIFT_FALLING    = 0 << (16+2),
		SSD_LCD_LSHIFT_RISING     = 1 << (16+2),
		SSD_LCD_LLINE_LOW         = 0 << (16+1),
		SSD_LCD_LLINE_HIGH        = 1 << (16+1),
		SSD_LCD_LFRAME_LOW        = 0 << (16+0),
		SSD_LCD_LFRAME_HIGH       = 1 << (16+0),
		SSD_LCD_MODE_TFT          = 0 << ( 8+5),
		SSD_LCD_MODE_SERIAL       = 2 << ( 8+5),
		SSD_LCD_MODE_DUMMY        = 1 << ( 8+5), /* only for serial mode */
		/* only needed for SSD_LCD_MODE_SERIAL */
		SSD_LCD_EVEN_SHIFT        =          3,
		SSD_LCD_EVEN_MASK         = 7 << SSD_LCD_EVEN_SHIFT,
		SSD_LCD_ODD_SHIFT         =          0,
		SSD_LCD_ODD_MASK          = 7 << SSD_LCD_ODD_SHIFT,
		SSD_LCD_RGB               = 0,
		SSD_LCD_RBG               = 1,
		SSD_LCD_GRB               = 2,
		SSD_LCD_GBR               = 3,
		SSD_LCD_BRG               = 4,
		SSD_LCD_BGR               = 5,
	} lcd_flags; /* specify low-level TFT data transfer */
	unsigned pll_as_sysclk : 1;
};

struct ssd_display {
	enum ssd_lcd_flags lcd_flags;
	struct ssd_timings {
		uint_least16_t visible, front, sync, back;
	} hori, vert;
	uint_least32_t pxclk_min, pxclk_typ, pxclk_max; /* Hz */
};

/* --------------------------------------------------------------------------
 * low level init functions
 * -------------------------------------------------------------------------- */

/* all frequencies below are in kHz */
uint_least32_t ssd_iv_get_vco_freq(const struct ssd_init_vector *iv);
uint_least32_t ssd_iv_get_pll_freq(const struct ssd_init_vector *iv);
uint_least32_t ssd_iv_get_sys_freq(const struct ssd_init_vector *iv);

/* returns the pixel clock frequency in kHz calculated using the current
 * lshift_freq value from iv */
uint_least32_t ssd_iv_get_pixel_freq_frac(const struct ssd_init_vector *iv);

/* in kHz */
uint_least32_t ssd_iv_calc_pixel_freq(
	const struct ssd_init_vector *iv,
	uint_least16_t refresh_rate
);

/* calculates an lshift_mult setting for the desired pixel clock frequency,
 * hsync/vsync settings and PLL frequency already set in iv */
uint_least32_t ssd_iv_calc_lshift_mult(
	const struct ssd_init_vector *iv,
	uint_least32_t pixel_freq /* kHz */
);

void ssd_iv_set_hsync(
	struct ssd_init_vector *iv,
	unsigned display, unsigned front, unsigned sync, unsigned back,
	unsigned sync_move, unsigned lpspp
);

void ssd_iv_set_vsync(
	struct ssd_init_vector *iv,
	unsigned display, unsigned front, unsigned sync, unsigned back,
	unsigned sync_move
);

void ssd_iv_set_display(struct ssd_init_vector *iv, const struct ssd_display *d);

/* --------------------------------------------------------------------------
 * high level init functions
 * -------------------------------------------------------------------------- */

/* Prints a textual representation of all the settings in iv to stderr. */
void ssd_iv_print(const struct ssd_init_vector *iv);

enum ssd_err {
	SSD_ERR_NONE,
	SSD_ERR_VCO_OOR,
	SSD_ERR_SYS_CLK_OOR,
	SSD_ERR_PLL_M_OOR,
	SSD_ERR_PLL_N_OOR,
	SSD_ERR_LSHIFT_OOR,
	SSD_ERR_PLL_UNSTABLE,
	SSD_ERR_PXCLK_UNAVAIL,
	SSD_ERR_PXCLK_OOR,
};

const char * ssd_strerr(enum ssd_err err);

/* refresh_rate may be 0 iff the ssd_display does specify a
 * typical pixel clock != 0. Otherwise it overrides the typical pixel clock. */
enum ssd_err ssd_iv_init(
	struct ssd_init_vector *iv,
	uint_least32_t in_clk_freq,
	uint_least8_t pll_m, uint_least8_t pll_n, char pll_as_sysclk,
	const struct ssd_display *d, uint_least16_t refresh_rate
);

enum ssd_err ssd_iv_check(const struct ssd_init_vector *iv);

/* Turns off the display, initializes the PLL, sets it up as system clock if
 * requested and soft-resets the controller (meaning all register values except
 * for 0xe0 to 0xe5 are lost).
 * 
 * If iv is invalid as determined by ssd_iv_check(), its error code is returned.
 * 
 * If the macro SSD_RD_DATA is defined and after programming the PLL verifying
 * its stability by querying the controller fails, this function returns
 * SSD_ERR_PLL_UNSTABLE. In that case the PLL is shut down and the controller is
 * not reset. */
enum ssd_err ssd_init_pll(const struct ssd_init_vector *iv);

/* Sets up the pixel frequency, horizontal and vertical timings and turns the
 * display back on. */
enum ssd_err ssd_init_display(const struct ssd_init_vector *iv);

/* Convenience function to fully initialize the controller.
 *
 * Fills a ssd_init_vector structure with all the information given in the
 * parameters via ssd_iv_init(), then calls ssd_init_pll() followed by
 * ssd_init_display() and finally sets the given address mode and interface
 * format.
 * 
 * If any of these fail, an error message is printed and the corresponding
 * error code is returned. */
enum ssd_err ssd_init(
	uint_least32_t in_clk_freq,
	uint_least8_t pll_m, uint_least8_t pll_n, char pll_as_sysclk,
	const struct ssd_display *d, uint_least16_t refresh_rate,
	enum ssd_address_mode adm, enum ssd_interface_fmt ifmt
);

#endif
