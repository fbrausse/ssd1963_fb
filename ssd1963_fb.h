
#ifndef SSD1963_FB_H
#define SSD1963_FB_H

typedef u8	uint_least8_t;
typedef u16	uint_least16_t;
typedef u32	uint_least32_t;
typedef u64	uint_least64_t;

#include "ssd1963.h"

struct ssd1963_platform_data {
	struct ssd_display lcd;
	enum ssd_address_mode lcd_addr_mode;
	enum ssd_interface_fmt bus_fmt;
	u32 xtal_freq;
	u8 pll_m, pll_n;
	char pll_as_sysclk;
	/* bus_width: refer to bus_fmt */
	unsigned gpio_bus[]; /* 0: #WR, 1: D/#C, 2..2+bus_width-1: data 0..bus_width-1 */
};

#define SSD1963_FB_DRIVER_NAME	"ssd1963_fb"

#define SSD1963_MAX_WIDTH	864
#define SSD1963_MAX_HEIGHT	480
#define SSD1963_MAX_PLL_M	256
#define SSD1963_MAX_PLL_N	16
#define SSD1963_MIN_DOTCLK	1000    /*   1 MHz in kHz */
#define SSD1963_MAX_DOTCLK	110000  /* 110 MHz in kHz */

extern void ssd_wr_slow_cmd(u8);
extern void ssd_wr_slow_data(u8);

#endif
