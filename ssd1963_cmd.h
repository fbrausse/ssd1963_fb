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

#ifndef SSD1963_CMD_H
#define SSD1963_CMD_H

/* the two macros SSD_WR_CMD and SSD_WR_DATA must be defined */

/* writes the unsigned char x while asserting D/#C and #WR and then releasing
 * both control lines */
/* #define SSD_WR_CMD(x)	((void)(x)) */

/* writes the unsigned char x while asserting #WR and then releasing it */
/* #define SSD_WR_DATA(x)	((void)(x)) */

/* the macro SSD_RD_DATA is optional, it should be defined if reading from the
 * controller is to be possible */

/* reads an unsigned char by first asserting #RD and retrieving the 8 lower bits
 * on the bus when releasing #RD */
/* #define SSD_RD_DATA()	(0) */

#ifdef SSD_IO_MACROS
#include SSD_IO_MACROS
#endif

/* these macros are allowed to evaluate their argument more than once */
#if !defined(SSD_WR_CMD) || !defined(SSD_WR_DATA)
# error SSD1963 functions need definitions of the macros SSD_WR_CMD, SSD_WR_DATA
#endif

static inline void ssd_cmd(const unsigned char k[static 1], unsigned data_len)
{
	SSD_WR_CMD(*k);
	k++;
	while (data_len) {
		SSD_WR_DATA(*k);
		k++;
		data_len--;
	}
}

#define SSD_CMD0(k)		ssd_cmd(k, sizeof(k)-1)
#define SSD_CMD(...)		SSD_CMD0(((unsigned char[]){ __VA_ARGS__ }))

/* use these raw commands with care: always read the description in the
 * SSD1963's datasheet for possible side effects, timing requirements, etc. */

#define SSD_NOP()			SSD_CMD(0x00)
#define SSD_SOFT_RESET()		SSD_CMD(0x01)
#define SSD_GET_POWER_MODE()		SSD_CMD(0x0a) /* param: 1 */
#define SSD_GET_ADDRESS_MODE()		SSD_CMD(0x0b) /* param: 1 */
#define SSD_GET_DISPLAY_MODE()		SSD_CMD(0x0d) /* param: 1 */
#define SSD_GET_TEAR_EFFECT_STATUS()	SSD_CMD(0x0e) /* param: 1 */
#define SSD_ENTER_SLEEP_MODE()		SSD_CMD(0x10)
#define SSD_EXIT_SLEEP_MODE()		SSD_CMD(0x11)
#define SSD_ENTER_PARTIAL_MODE()	SSD_CMD(0x12)
#define SSD_ENTER_NORMAL_MODE()		SSD_CMD(0x13)
#define SSD_EXIT_INVERT_MODE()		SSD_CMD(0x20)
#define SSD_ENTER_INVERT_MODE()		SSD_CMD(0x21)
/* g: [0-3] */
#define SSD_SET_GAMMA_CURVE(g)		SSD_CMD(0x26, 1 << (g))
#define SSD_SET_DISPLAY_OFF()		SSD_CMD(0x28)
#define SSD_SET_DISPLAY_ON()		SSD_CMD(0x29)
#define SSD_SET_COLUMN_ADDRESS(start_col,end_col) \
	SSD_CMD(0x2a, (start_col) >> 8, (start_col) & 0xff, (end_col) >> 8, (end_col) & 0xff)
#define SSD_SET_PAGE_ADDRESS(start_page,end_page) \
	SSD_CMD(0x2b, (start_page) >> 8, (start_page) & 0xff, (end_page) >> 8, (end_page) & 0xff)
#define SSD_WRITE_MEMORY_START()	SSD_CMD(0x2c)
#define SSD_READ_MEMORY_START()		SSD_CMD(0x2e)
#define SSD_SET_PARTIAL_AREA(start_row,end_row) \
	SSD_CMD(0x30, (start_row) >> 8, (start_row), (end_row) >> 8, (end_row))
#define SSD_SET_SCROLL_AREA(tfa,vsa,bfa) \
	SSD_CMD(0x33, (tfa) >> 8, (tfa), (vsa) >> 8, (vsa), (bfa) >> 8, (bfa))
#define SSD_SET_TEAR_OFF()		SSD_CMD(0x34)
#define SSD_SET_TEAR_ON(v_and_h_blank)	SSD_CMD(0x35, !!(v_and_h_blank))
#define SSD_SET_ADDRESS_MODE(arg)	SSD_CMD(0x36, (arg))
#define SSD_SET_SCROLL_START(vsp)	SSD_CMD(0x37, (vsp) >> 8, (vsp))
#define SSD_EXIT_IDLE_MODE()		SSD_CMD(0x38)
#define SSD_ENTER_IDLE_MODE()		SSD_CMD(0x39)
#define SSD_WRITE_MEMORY_CONTINUE()	SSD_CMD(0x3c)
#define SSD_READ_MEMORY_CONTINUE()	SSD_CMD(0x3e)
#define SSD_SET_TEAR_SCANLINE(n)	SSD_CMD(0x44, (n) >> 8, (n))
#define SSD_GET_SCANLINE()		SSD_CMD(0x45) /* param: 2 */
#define SSD_READ_DDB()			SSD_CMD(0xa1) /* param: 5 */
#define SSD_SET_LCD_MODE(a,b,hdp,vdp,g) \
	SSD_CMD(0xb0, (a), (b), (hdp) >> 8, (hdp), (vdp) >> 8, (vdp), (g))
#define SSD_GET_LCD_MODE()		SSD_CMD(0xb1) /* param: 7 */
#define SSD_SET_HORI_PERIOD(ht,hps,hpw,lps,lpspp) \
	SSD_CMD(0xb4, (ht) >> 8, (ht), (hps) >> 8, (hps), (hpw), (lps) >> 8, (lps), (lpspp))
#define SSD_GET_HORI_PERIOD()		SSD_CMD(0xb5) /* param: 8 */
#define SSD_SET_VERT_PERIOD(vt,vps,vpw,fps) \
	SSD_CMD(0xb6, (vt) >> 8, (vt), (vps) >> 8, (vps), (vpw), (fps) >> 8, (fps))
#define SSD_GET_VERT_PERIOD()		SSD_CMD(0xb7) /* param: 7 */
#define SSD_SET_GPIO_CONF(a,b)		SSD_CMD(0xb8, (a), (b))
#define SSD_GET_GPIO_CONF()		SSD_CMD(0xb9) /* param: 2 */
#define SSD_SET_GPIO_VALUE(a)		SSD_CMD(0xba, (a))
#define SSD_GET_GPIO_STATUS()		SSD_CMD(0xbb) /* param: 1 */
#define SSD_SET_POST_PROC(a,b,c,d)	SSD_CMD(0xbc, (a), (b), (c), (d))
#define SSD_GET_POST_PROC()		SSD_CMD(0xbd) /* param: 4 */
#define SSD_SET_PWM_CONF(pwmf,pwm,c,d,e,f) \
	SSD_CMD(0xbe, (pwmf), (pwm), (c), (d), (e), (f))
#define SSD_GET_PWM_CONF()		SSD_CMD(0xbf) /* param: 7 */
#define SSD_SET_LCD_GEN0(a,gf,gr,f,gp) \
	SSD_CMD(0xc0, (a), (gf) >> 8, (gf), (gr) >> 8, (gr), (f) | (gp) >> 8, (gp))
#define SSD_GET_LCD_GEN0()		SSD_CMD(0xc1) /* param: 7 */
#define SSD_SET_LCD_GEN1(a,gf,gr,f,gp) \
	SSD_CMD(0xc2, (a), (gf) >> 8, (gf), (gr) >> 8, (gr), (f) | (gp) >> 8, (gp))
#define SSD_GET_LCD_GEN1()		SSD_CMD(0xc3) /* param: 7 */
#define SSD_SET_LCD_GEN2(a,gf,gr,f,gp) \
	SSD_CMD(0xc4, (a), (gf) >> 8, (gf), (gr) >> 8, (gr), (f) | (gp) >> 8, (gp))
#define SSD_GET_LCD_GEN2()		SSD_CMD(0xc5) /* param: 7 */
#define SSD_SET_LCD_GEN3(a,gf,gr,f,gp) \
	SSD_CMD(0xc6, (a), (gf) >> 8, (gf), (gr) >> 8, (gr), (f) | (gp) >> 8, (gp))
#define SSD_GET_LCD_GEN3()		SSD_CMD(0xc7) /* param: 7 */
#define SSD_SET_GPIO0_ROP(a,b)		SSD_CMD(0xc8, (a), (b))
#define SSD_GET_GPIO0_ROP()		SSD_CMD(0xc9) /* param: 2 */
#define SSD_SET_GPIO1_ROP(a,b)		SSD_CMD(0xca, (a), (b))
#define SSD_GET_GPIO1_ROP()		SSD_CMD(0xcb) /* param: 2 */
#define SSD_SET_GPIO2_ROP(a,b)		SSD_CMD(0xcc, (a), (b))
#define SSD_GET_GPIO2_ROP()		SSD_CMD(0xcd) /* param: 2 */
#define SSD_SET_GPIO3_ROP(a,b)		SSD_CMD(0xce, (a), (b))
#define SSD_GET_GPIO3_ROP()		SSD_CMD(0xcf) /* param: 2 */
#define SSD_SET_DBC_CONF(a)		SSD_CMD(0xd0, (a))
#define SSD_GET_DBC_CONF()		SSD_CMD(0xd1) /* param: 1 */
#define SSD_SET_DBC_TH(th1,th2,th3) \
	SSD_CMD(0xd4, (th1) >> 16, (th1) >> 8, (th1), (th2) >> 16, (th2) >> 8, (th2), (th3) >> 16, (th3) >> 8, (th3))
#define SSD_GET_DBC_TH()		SSD_CMD(0xd5) /* param: 9 */
#define SSD_SET_PLL(a)			SSD_CMD(0xe0, (a))
#define SSD_SET_PLL_MN(m,n,c)		SSD_CMD(0xe2, (m), (n), (c))
#define SSD_GET_PLL_MN()		SSD_CMD(0xe3) /* param: 3 */
#define SSD_GET_PLL_STATUS()		SSD_CMD(0xe4) /* param: 1 */
#define SSD_SET_DEEP_SLEEP()		SSD_CMD(0xe5)
#define SSD_SET_LSHIFT_FREQ(fpr)	SSD_CMD(0xe6, (fpr) >> 16, (fpr) >> 8, (fpr))
#define SSD_GET_LSHIFT_FREQ()		SSD_CMD(0xe7) /* param: 3 */
#define SSD_SET_PIXEL_DATA_INTERFACE(a)	SSD_CMD(0xf0, (a))
#define SSD_GET_PIXEL_DATA_INTERFACE()	SSD_CMD(0xf1) /* param: 1 */

#endif
