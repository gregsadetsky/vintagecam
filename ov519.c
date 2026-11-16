// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * OmniVision OV519 bridge + OV7648 sensor (Sony EyeToy, 054c:0154), ported
 * from linux drivers/media/usb/gspca/ov519.c
 *
 * Copyright (C) 2008-2011 Jean-François Moine <moinejf@free.fr>
 * Copyright (C) 2009 Hans de Goede <hdegoede@redhat.com>
 * ov519 support originally by Mark McClelland, Romain Beauxis and others
 *
 * only the BRIDGE_OV519 + SEN_OV7648 paths are kept. the camera sends
 * complete jpeg frames.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "cam.h"

#define R51x_SYS_RESET		0x50
#define OV519_R10_H_SIZE	0x10
#define OV519_R11_V_SIZE	0x11
#define OV519_R12_X_OFFSETL	0x12
#define OV519_R13_X_OFFSETH	0x13
#define OV519_R14_Y_OFFSETL	0x14
#define OV519_R15_Y_OFFSETH	0x15
#define OV519_R16_DIVIDER	0x16
#define OV519_R20_DFR		0x20
#define OV519_R25_FORMAT	0x25
#define OV519_R51_RESET1	0x51
#define OV519_R54_EN_CLK1	0x54
#define OV519_R57_SNAPSHOT	0x57
#define OV519_GPIO_DATA_OUT0	0x71
#define OV519_GPIO_IO_CTRL0	0x72

#define R51x_I2C_W_SID		0x41
#define R51x_I2C_SADDR_3	0x42
#define R51x_I2C_SADDR_2	0x43
#define R51x_I2C_R_SID		0x44
#define R51x_I2C_DATA		0x45
#define R518_I2C_CTL		0x47

#define OV7xx0_SID		0x42
#define OV7610_REG_SAT		0x03
#define OV7610_REG_BRT		0x06
#define OV7610_REG_ID_HIGH	0x1c
#define OV7610_REG_ID_LOW	0x1d
#define OV7610_REG_COM_I	0x29

struct ov_regvals {
	u8 reg;
	u8 val;
};

struct sd {
	int sensor_reg_cache[256];
	int clockdiv;
};

static void reg_w(struct cam *c, u16 index, u16 value)
{
	u8 buf = value;
	int ret;

	if (c->usb_err < 0)
		return;
	value = poke_lookup(c, 'r', index, value);
	buf = value;
	/* Avoid things going to fast for the bridge with a xhci host */
	usleep(150);
	ret = libusb_control_transfer(c->h, 0x40, 1, 0, index, &buf, 1, 500);
	if (ret < 0) {
		fprintf(stderr, "ov519: reg_w %02x failed %s\n", index,
			libusb_error_name(ret));
		c->usb_err = ret;
	}
}

/* n is 1, or 8 for the kernel's reg_r8() */
static int reg_r(struct cam *c, u16 index, int n)
{
	u8 buf[8] = {0};
	int ret;

	if (c->usb_err < 0)
		return -1;
	usleep(150);
	ret = libusb_control_transfer(c->h, 0xc0, 1, 0, index, buf, n, 500);
	if (ret < 0) {
		fprintf(stderr, "ov519: reg_r %02x failed %s\n", index,
			libusb_error_name(ret));
		c->usb_err = ret;
		return -1;
	}
	return buf[0];
}

static void reg_w_mask(struct cam *c, u16 index, u8 value, u8 mask)
{
	int ret;

	if (mask != 0xff) {
		value &= mask;
		ret = reg_r(c, index, 1);
		if (ret < 0)
			return;
		value |= ret & ~mask;
	}
	reg_w(c, index, value);
}

/* ov518_i2c_w() */
static void i2c_w_raw(struct cam *c, u8 reg, u8 value)
{
	reg_w(c, R51x_I2C_SADDR_3, reg);
	reg_w(c, R51x_I2C_DATA, value);
	reg_w(c, R518_I2C_CTL, 0x01);	/* Initiate 3-byte write cycle */
	msleep(4);
	reg_r(c, R518_I2C_CTL, 8);
}

static void i2c_w(struct cam *c, u8 reg, u8 value)
{
	struct sd *sd = c->priv;

	value = poke_lookup(c, 'i', reg, value);
	if (sd->sensor_reg_cache[reg] == value)
		return;
	i2c_w_raw(c, reg, value);
	if (c->usb_err >= 0) {
		/* Up on sensor reset empty the register cache */
		if (reg == 0x12 && (value & 0x80))
			memset(sd->sensor_reg_cache, -1,
			       sizeof(sd->sensor_reg_cache));
		else
			sd->sensor_reg_cache[reg] = value;
	}
}

/* ov518_i2c_r() */
static int i2c_r(struct cam *c, u8 reg)
{
	struct sd *sd = c->priv;
	int ret;

	if (sd->sensor_reg_cache[reg] != -1)
		return sd->sensor_reg_cache[reg];
	reg_w(c, R51x_I2C_SADDR_2, reg);
	reg_w(c, R518_I2C_CTL, 0x03);	/* Initiate 2-byte write cycle */
	reg_r(c, R518_I2C_CTL, 8);
	reg_w(c, R518_I2C_CTL, 0x05);	/* Initiate 2-byte read cycle */
	reg_r(c, R518_I2C_CTL, 8);
	ret = reg_r(c, R51x_I2C_DATA, 1);
	if (ret >= 0)
		sd->sensor_reg_cache[reg] = ret;
	return ret;
}

static void i2c_w_mask(struct cam *c, u8 reg, u8 value, u8 mask)
{
	int rc;

	value &= mask;
	rc = i2c_r(c, reg);
	if (rc < 0)
		return;
	value |= rc & ~mask;
	i2c_w(c, reg, value);
}

static void write_regvals(struct cam *c, const struct ov_regvals *r, int n)
{
	while (--n >= 0) {
		reg_w(c, r->reg, r->val);
		r++;
	}
}

static void ov51x_stop(struct cam *c)
{
	reg_w(c, OV519_R51_RESET1, 0x0f);
	reg_w(c, OV519_R51_RESET1, 0x00);
	reg_w(c, 0x22, 0x00);		/* FRAR */
}

static void ov51x_restart(struct cam *c)
{
	reg_w(c, OV519_R51_RESET1, 0x0f);
	reg_w(c, OV519_R51_RESET1, 0x00);
	reg_w(c, 0x22, 0x1d);		/* FRAR */
}

static void ov51x_led_control(struct cam *c, int on)
{
	reg_w_mask(c, OV519_GPIO_DATA_OUT0, on, 1);
}

/* init_ov_sensor(OV7xx0_SID) */
static int init_ov_sensor(struct cam *c)
{
	int i;

	reg_w(c, R51x_I2C_W_SID, OV7xx0_SID);
	reg_w(c, R51x_I2C_R_SID, OV7xx0_SID + 1);

	i2c_w(c, 0x12, 0x80);		/* Reset the sensor */
	msleep(150);
	for (i = 0; i < 10; i++) {
		if (i2c_r(c, OV7610_REG_ID_HIGH) == 0x7f &&
		    i2c_r(c, OV7610_REG_ID_LOW) == 0xa2)
			return 0;
		i2c_w(c, 0x12, 0x80);
		msleep(150);
		/* Dummy read to sync I2C */
		if (i2c_r(c, 0x00) < 0)
			return -1;
	}
	return -1;
}

/* sd_config + sd_init: ov519_configure(), sensor detection, norm_7640 */
static int ov519_init(struct cam *c)
{
	static const struct ov_regvals init_519[] = {
		{ 0x5a, 0x6d }, /* EnableSystem */
		{ 0x53, 0x9b }, /* don't enable the microcontroller */
		{ OV519_R54_EN_CLK1, 0xff }, /* set bit2 to enable jpeg */
		{ 0x5d, 0x03 },
		{ 0x49, 0x01 },
		{ 0x48, 0x00 },
		/* Set LED pin to output mode. Bit 4 must be cleared or sensor
		 * detection will fail. This deserves further investigation. */
		{ OV519_GPIO_IO_CTRL0,   0xee },
		{ OV519_R51_RESET1, 0x0f },
		{ OV519_R51_RESET1, 0x00 },
		{ 0x22, 0x00 },
	};
	struct sd *sd = calloc(1, sizeof *sd);
	int com_i, high, low;

	if (!sd)
		return -1;
	c->priv = sd;
	memset(sd->sensor_reg_cache, -1, sizeof(sd->sensor_reg_cache));

	if (c->width == 0)
		c->width = 640;
	if (c->width != 640 && c->width != 320) {
		fprintf(stderr, "ov519: width must be 640 or 320\n");
		return -1;
	}
	c->height = c->width * 3 / 4;

	write_regvals(c, init_519, sizeof init_519 / sizeof init_519[0]);
	if (init_ov_sensor(c) < 0) {
		fprintf(stderr, "ov519: no ov7xx0 sensor answered\n");
		return -1;
	}
	/* ov7xx0_configure() */
	com_i = i2c_r(c, OV7610_REG_COM_I);
	high = i2c_r(c, 0x0a);
	low = i2c_r(c, 0x0b);
	if (c->verbose || (com_i & 3) != 0 || high != 0x76 || low != 0x48)
		fprintf(stderr, "ov519: sensor com_i %02x pid %02x ver %02x "
			"(ov7648 = xx00b, 76, 48)\n", com_i, high, low);
	if (com_i < 0 || (com_i & 3) != 0 || high != 0x76 || low != 0x48) {
		fprintf(stderr, "ov519: not an ov7648, other sensors are not ported\n");
		return -1;
	}
	ov51x_led_control(c, 0);	/* turn LED off */

	/* norm_7640 */
	i2c_w(c, 0x12, 0x80);
	i2c_w(c, 0x12, 0x14);
	return c->usb_err < 0 ? -1 : 0;
}

/* sd_start: ov519_mode_init_regs(), set_ov_sensor_window(),
 * mode_init_ov_sensor_regs(), then the control defaults */
static int ov519_start(struct cam *c)
{
	static const struct ov_regvals mode_init_519[] = {
		{ 0x5d,	0x03 }, /* Turn off suspend mode */
		{ 0x53,	0x9f }, /* was 9b in 1.65-1.08 */
		{ OV519_R54_EN_CLK1, 0x0f }, /* bit2 (jpeg enable) */
		{ 0xa2,	0x20 }, /* a2-a5 are undocumented */
		{ 0xa3,	0x18 },
		{ 0xa4,	0x04 },
		{ 0xa5,	0x28 },
		{ 0x37,	0x00 },	/* SetUsbInit */
		{ 0x55,	0x02 }, /* 4.096 Mhz audio clock */
		/* Enable both fields, YUV Input, disable defect comp (why?) */
		{ 0x22,	0x1d },
		{ 0x17,	0x50 }, /* undocumented */
		{ 0x37,	0x00 }, /* undocumented */
		{ 0x40,	0xff }, /* I2C timeout counter */
		{ 0x46,	0x00 }, /* I2C clock prescaler */
		{ 0x59,	0x04 },	/* new from windrv 090403 */
		{ 0xff,	0x00 }, /* undocumented */
	};
	struct sd *sd = c->priv;
	int qvga = c->width == 320;

	write_regvals(c, mode_init_519, sizeof mode_init_519 / sizeof mode_init_519[0]);
	/* Select 8-bit input mode */
	reg_w_mask(c, OV519_R20_DFR, 0x10, 0x10);

	reg_w(c, OV519_R10_H_SIZE, c->width >> 4);
	reg_w(c, OV519_R11_V_SIZE, c->height >> 3);
	reg_w(c, OV519_R12_X_OFFSETL, qvga ? 0x01 : 0x00);
	reg_w(c, OV519_R13_X_OFFSETH, 0x00);
	reg_w(c, OV519_R14_Y_OFFSETL, 0x00);
	reg_w(c, OV519_R15_Y_OFFSETH, 0x00);
	reg_w(c, OV519_R16_DIVIDER, 0x00);
	reg_w(c, OV519_R25_FORMAT, 0x03);	/* YUV422 */
	reg_w(c, 0x26, 0x00);			/* Undocumented */

	/* frame rate: the kernel default of 15 */
	reg_w(c, 0xa4, 0x04);
	reg_w(c, 0x23, 0xff);
	sd->clockdiv = 1;

	/* mode_init_ov_sensor_regs(), SEN_OV7648 */
	i2c_w_mask(c, 0x14, qvga ? 0x20 : 0x00, 0x20);
	i2c_w_mask(c, 0x28, qvga ? 0x00 : 0x20, 0x20);
	/* Setting this undocumented bit in qvga mode removes a very
	   annoying vertical shaking of the image */
	i2c_w_mask(c, 0x2d, qvga ? 0x40 : 0x00, 0x40);
	/* Unknown */
	i2c_w_mask(c, 0x67, qvga ? 0xf0 : 0x90, 0xf0);
	/* Allow higher automatic gain (to allow higher framerates) */
	i2c_w_mask(c, 0x74, qvga ? 0x20 : 0x00, 0x20);
	i2c_w_mask(c, 0x12, 0x04, 0x04);	/* AWB: 1 */
	i2c_w(c, 0x11, sd->clockdiv);

	/* set_ov_sensor_window(): hwsbase = hwebase = 0x1a, vws = vwe = 3 */
	i2c_w(c, 0x17, 0x1a);
	i2c_w(c, 0x18, 0x1a + (c->width >> (qvga ? 1 : 2)));
	i2c_w(c, 0x19, 0x03);
	i2c_w(c, 0x1a, 0x03 + (c->height >> (qvga ? 0 : 1)));

	/* sd_reset_snapshot() */
	reg_w(c, R51x_SYS_RESET, 0x40);
	reg_w(c, R51x_SYS_RESET, 0x00);

	ov51x_restart(c);
	ov51x_led_control(c, 1);

	/* control defaults: brightness 127, saturation 127, flicker filter */
	i2c_w(c, OV7610_REG_BRT, 127);
	i2c_w(c, OV7610_REG_SAT, 127 & 0xf0);
	if (c->light_freq == 50) {
		i2c_w_mask(c, 0x2d, 0x04, 0x04);
		i2c_w_mask(c, 0x2a, 0x80, 0x80);
		i2c_w(c, 0x2b, 0xac);
	} else if (c->light_freq == 60) {
		i2c_w_mask(c, 0x2d, 0x04, 0x04);
		i2c_w_mask(c, 0x2a, 0x00, 0x80);
	} else {
		i2c_w_mask(c, 0x2d, 0x00, 0x04);
		i2c_w_mask(c, 0x2a, 0x00, 0x80);
	}
	return c->usb_err < 0 ? -1 : 0;
}

/* sd_stopN + sd_stop0 */
static void ov519_stop(struct cam *c)
{
	c->usb_err = 0;
	ov51x_stop(c);
	ov51x_led_control(c, 0);
	reg_w(c, OV519_R57_SNAPSHOT, 0x23);
}

static void ov519_pkt_scan(struct cam *c, const u8 *data, int len)
{
	/* Header of ov519 is 16 bytes:
	 *     Byte     Value      Description
	 *	0	0xff	magic
	 *	1	0xff	magic
	 *	2	0xff	magic
	 *	3	0xXX	0x50 = SOF, 0x51 = EOF
	 *	9	0xXX	0x01 initial frame without data,
	 *			0x00 standard frame with image
	 *	14	Lo	in EOF: length of image data / 8
	 *	15	Hi
	 */
	if (len >= 16 && data[0] == 0xff && data[1] == 0xff && data[2] == 0xff) {
		switch (data[3]) {
		case 0x50:		/* start of frame */
			data += 16;
			len -= 16;
			if (len >= 2 && (data[0] == 0xff || data[1] == 0xd8))
				frame_add(c, FIRST_PACKET, data, len);
			else
				frame_add(c, DISCARD_PACKET, NULL, 0);
			return;
		case 0x51:		/* end of frame */
			if (c->verbose && c->last_pkt != DISCARD_PACKET) {
				int declared = (data[14] | data[15] << 8) * 8;

				if (declared != c->frame_len || data[9] != 0)
					fprintf(stderr, "ov519: eof declared %d bytes, got %d, "
						"flag9 %02x, hdr %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
						declared, c->frame_len, data[9],
						data[4], data[5], data[6], data[7], data[8],
						data[10], data[11], data[12], data[13], data[3]);
			}
			if (data[9] != 0)
				frame_add(c, DISCARD_PACKET, NULL, 0);
			/* not in the kernel: the bridge pads the jpeg with
			 * zero bytes after its ff d9 end marker (seen on a
			 * real unit), drop them so the frame ends cleanly */
			if (c->last_pkt != DISCARD_PACKET) {
				int n = 0;

				while (n < 64 && c->frame_len > 2 &&
				       c->frame[c->frame_len - 1] == 0x00) {
					c->frame_len--;
					n++;
				}
			}
			frame_add(c, LAST_PACKET, NULL, 0);
			return;
		}
	}
	/* intermediate packet */
	frame_add(c, INTER_PACKET, data, len);
}

/* test hook: the bridge's frame register cleared, as ov51x_stop() does */
static void ov519_test_break(struct cam *c)
{
	c->usb_err = 0;
	reg_w(c, 0x22, 0x00);
}

const struct cam_driver ov519_ov7648_driver = {
	.name = "sony-eyetoy-ov519-ov7648",
	.vid = 0x054c,
	.pid = 0x0154,
	.iso_ep = 0x81,
	.out_format = "mjpeg",
	.init = ov519_init,
	.start = ov519_start,
	.stop = ov519_stop,
	.pkt_scan = ov519_pkt_scan,
	.test_break = ov519_test_break,
};
