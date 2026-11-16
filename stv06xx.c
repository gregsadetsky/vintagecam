// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * STV0600 bridge + Photobit PB-0100 sensor (Logitech QuickCam Express,
 * 046d:0840) and STV0610 bridge + ST VV6410 sensor (LEGO camera / QuickCam
 * Web, 046d:0850), ported from linux drivers/media/usb/gspca/stv06xx/
 * (stv06xx.c, stv06xx_pb0100.c, stv06xx_vv6410.c).
 *
 * Copyright (c) 2001 Jean-Fredric Clere, Nikolas Zimmermann, Georg Acher
 *		      Mark Cave-Ayland, Carlo E Prelz, Dick Streefland
 * Copyright (c) 2002, 2003 Tuukka Toivonen
 * Copyright (c) 2008 Erik Andrén
 *
 * the camera sends raw 8 bit bayer, the engine turns each frame into a jpeg.
 * other 046d:0840 units carry an HDCS-1000 sensor, which is not ported.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cam.h"

#define STV_I2C_FLUSH		0x1423
#define STV_ISO_ENABLE		0x1440
#define STV_LED_CTRL		0x1445
#define STV_RESET		0x1620
#define STV_SCAN_RATE		0x1443
#define STV_REG00		0x1500
#define STV_REG01		0x1501
#define STV_REG02		0x1502
#define STV_REG03		0x1503
#define STV_REG04		0x1504
#define STV_ISO_SIZE_L		0x15c1
#define STV_Y_CTRL		0x15c3
#define STV_X_CTRL		0x1680

#define I2C_MAX_WORDS		8
#define I2C_BUFFER_LENGTH	0x23
#define I2C_READ_CMD		3
#define I2C_WRITE_CMD		1
#define CTRL_TIMEOUT		5000

#define PB_I2C_ADDR		0xba
#define PB_I2C_FLUSH		1

#define PB_IDENT		0x00
#define PB_RSTART		0x01
#define PB_CSTART		0x02
#define PB_RWSIZE		0x03
#define PB_CWSIZE		0x04
#define PB_CFILLIN		0x05
#define PB_VBL			0x06
#define PB_CONTROL		0x07
#define PB_FINTTIME		0x08
#define PB_RINTTIME		0x09
#define PB_ROWSPEED		0x0a
#define PB_ABORTFRAME		0x0b
#define PB_RESET		0x0d
#define PB_EXPGAIN		0x0e
#define PB_R15			0x0f
#define PB_R17			0x11
#define PB_R21			0x15
#define PB_R22			0x16
#define PB_UPDATEINT		0x17
#define PB_R28			0x1c
#define PB_PREADCTRL		0x20
#define PB_ADCMAXGAIN		0x33
#define PB_ADCMINGAIN		0x34
#define PB_ADCGLOBALGAIN	0x35
#define PB_R54			0x36
#define PB_R55			0x37
#define PB_VOFFSET		0x39
#define PB_ADCGAINH		0x3b
#define PB_ADCGAINL		0x3c

#define VV_I2C_ADDR		0x20
#define VV_I2C_FLUSH		5

#define VV6410_DEVICEH		0x00
#define VV6410_SETUP0		0x10
#define VV6410_SETUP1		0x11
#define VV6410_FGMODES		0x14
#define VV6410_PINMAPPING	0x15
#define VV6410_DATAFORMAT	0x16
#define VV6410_FINEH		0x20
#define VV6410_FINEL		0x21
#define VV6410_COARSEH		0x22
#define VV6410_COARSEL		0x23
#define VV6410_ANALOGGAIN	0x24
#define VV6410_CLKDIV		0x25
#define VV6410_AS0		0x77
#define VV6410_AT0		0x78
#define VV6410_AT1		0x79
#define VV6410_LOW_POWER_MODE	(1 << 0)
#define VV6410_SOFT_RESET	(1 << 2)
#define VV6410_CLK_DIV_2	(1 << 1)
#define VV6410_CIF_LINELENGTH	415

#define BIT(n) (1u << (n))

/* software auto exposure. the sensor's own (PB_EXPGAIN) stayed near white
 * for every target tried on a real unit, so it is left off.
 * measured on the device: row integration time above about 0xf0 lowers the
 * frame rate, so past that point the gain goes up instead. */
#define AE_TARGET	105	/* mean raw pixel value to aim for */
#define AE_DEADBAND	15
#define AE_SETTLE	3	/* frames to wait after a change */

struct sd {
	int vv6410;		/* 0: pb0100 */
	/* pb0100: PB_RINTTIME, PB_ADCGLOBALGAIN. vv6410: the kernel's
	 * exposure control value (0..32768) and analog gain (0..15) */
	int exp, gain;
	int exp_min, exp_max, gain_min, gain_max;
	int auto_exp;
	long sum, n;		/* of the frame being received */
	int mean, mean_fresh, settle;
	int skip_first;		/* short leftover frames come out right after start */
};

static void write_bridge(struct cam *c, u16 address, int value)
{
	u8 buf[2];
	int r, len;

	if (c->usb_err < 0)
		return;
	value = poke_lookup(c, 'r', address, value);
	len = value > 0xff ? 2 : 1;
	buf[0] = value & 0xff;
	buf[1] = (value >> 8) & 0xff;
	r = libusb_control_transfer(c->h, 0x40, 0x04, address, 0, buf, len,
				    CTRL_TIMEOUT);
	if (c->verbose)
		fprintf(stderr, "bridge %04x <- %x (%d)\n", address, value, r);
	if (r < 0) {
		if (r != LIBUSB_ERROR_PIPE || c->verbose)
			fprintf(stderr, "stv06xx: bridge write %04x: %s\n", address,
				libusb_error_name(r));
		c->usb_err = r;
	}
}

/* a bridge write whose failure the kernel ignores (vv6410_init/_start do not
 * check these). seen on a real lego camera: 0x1680 answers with a stall. */
static void write_bridge_try(struct cam *c, u16 address, int value)
{
	if (c->usb_err < 0)
		return;
	write_bridge(c, address, value);
	if (c->usb_err == LIBUSB_ERROR_PIPE)
		c->usb_err = 0;
}

/* the pb0100 has 16 bit registers, the vv6410 8 bit ones */
static void write_sensor(struct cam *c, u8 reg, int value)
{
	struct sd *sd = c->priv;
	u8 buf[I2C_BUFFER_LENGTH];
	int r;

	if (c->usb_err < 0)
		return;
	value = poke_lookup(c, 'i', reg, value);
	memset(buf, 0, sizeof buf);
	buf[0] = reg;
	buf[0x10] = value & 0xff;
	if (!sd->vv6410)
		buf[0x11] = (value >> 8) & 0xff;
	buf[0x20] = sd->vv6410 ? VV_I2C_ADDR : PB_I2C_ADDR;
	buf[0x21] = 0;			/* number of commands - 1 */
	buf[0x22] = I2C_WRITE_CMD;
	r = libusb_control_transfer(c->h, 0x40, 0x04, 0x0400, 0, buf, sizeof buf,
				    CTRL_TIMEOUT);
	if (r >= 0 && sd->vv6410) {
		/* stv06xx_write_sensor_finish(), STV0610 only */
		u8 zero = 0;

		r = libusb_control_transfer(c->h, 0x40, 0x04, 0x1704, 0, &zero, 1,
					    CTRL_TIMEOUT);
	}
	if (c->verbose)
		fprintf(stderr, "sensor %02x <- %04x (%d)\n", reg, value, r);
	if (r < 0) {
		fprintf(stderr, "stv06xx: sensor write %02x: %s\n", reg,
			libusb_error_name(r));
		c->usb_err = r;
	}
}

static int read_sensor(struct cam *c, u8 reg)
{
	struct sd *sd = c->priv;
	u8 buf[I2C_BUFFER_LENGTH];
	int r, len = sd->vv6410 ? 1 : 2;

	write_bridge(c, STV_I2C_FLUSH, sd->vv6410 ? VV_I2C_FLUSH : PB_I2C_FLUSH);
	if (c->usb_err < 0)
		return -1;
	memset(buf, 0, sizeof buf);
	buf[0] = reg;
	buf[0x20] = sd->vv6410 ? VV_I2C_ADDR : PB_I2C_ADDR;
	buf[0x21] = 0;
	buf[0x22] = I2C_READ_CMD;
	r = libusb_control_transfer(c->h, 0x40, 0x04, 0x1400, 0, buf, sizeof buf,
				    CTRL_TIMEOUT);
	if (r >= 0)
		r = libusb_control_transfer(c->h, 0xc0, 0x04, 0x1410, 0, buf, len,
					    CTRL_TIMEOUT);
	if (r < len) {
		fprintf(stderr, "stv06xx: sensor read %02x: %s\n", reg,
			r < 0 ? libusb_error_name(r) : "short");
		c->usb_err = r < 0 ? r : LIBUSB_ERROR_IO;
		return -1;
	}
	return len == 2 ? buf[0] | (buf[1] << 8) : buf[0];
}

/* vv6410_set_exposure() */
static void vv6410_set_exposure(struct cam *c, int val)
{
	unsigned int fine, coarse;

	val = (val * val >> 14) + val / 4;
	fine = val % VV6410_CIF_LINELENGTH;
	coarse = val / VV6410_CIF_LINELENGTH;
	if (coarse > 512)
		coarse = 512;
	write_sensor(c, VV6410_FINEH, fine >> 8);
	write_sensor(c, VV6410_FINEL, fine & 0xff);
	write_sensor(c, VV6410_COARSEH, coarse >> 8);
	write_sensor(c, VV6410_COARSEL, coarse & 0xff);
}

static void set_exposure(struct cam *c, int val)
{
	struct sd *sd = c->priv;

	if (sd->vv6410)
		vv6410_set_exposure(c, val);
	else
		write_sensor(c, PB_RINTTIME, val);
}

static void set_gain(struct cam *c, int val)
{
	struct sd *sd = c->priv;

	if (sd->vv6410)		/* vv6410_set_analog_gain() */
		write_sensor(c, VV6410_ANALOGGAIN, 0xf0 | (val & 0xf));
	else
		write_sensor(c, PB_ADCGLOBALGAIN, val);
}

/* vv6410_probe() + vv6410_init() */
static int vv6410_init(struct cam *c)
{
	static const u16 bridge_init[][2] = {
		/* This reg is written twice. Some kind of reset? */
		{STV_RESET, 0x80},
		{STV_RESET, 0x00},
		{STV_SCAN_RATE, 0x00},
		{STV_I2C_FLUSH, 0x04},
		{STV_REG00, 0x0b},
		{STV_REG01, 0xa7},
		{STV_REG02, 0xb7},
		{STV_REG03, 0x00},
		{STV_REG04, 0x00},
		{0x1536, 0x02},
		{0x1537, 0x00},
		{0x1538, 0x60},
		{0x1539, 0x01},
		{0x153a, 0x20},
		{0x153b, 0x01},
	};
	static const u8 sensor_init[][2] = {
		{VV6410_SETUP0,	VV6410_SOFT_RESET},
		{VV6410_SETUP0,	VV6410_LOW_POWER_MODE},
		/* Use shuffled read-out mode */
		{VV6410_SETUP1,	BIT(6)},
		{VV6410_FGMODES, BIT(6) | BIT(4) | BIT(2) | BIT(0)},
		{VV6410_PINMAPPING, 0x00},
		/* Pre-clock generator divide off */
		{VV6410_DATAFORMAT, BIT(7) | BIT(0)},
		{VV6410_CLKDIV,	VV6410_CLK_DIV_2},
		/* Enable voltage doubler */
		{VV6410_AS0, BIT(6) | BIT(4) | BIT(3) | BIT(2) | BIT(1)},
		{VV6410_AT0, 0x00},
		/* Power up audio, differential */
		{VV6410_AT1, BIT(4) | BIT(0)},
	};
	u8 buf[I2C_BUFFER_LENGTH], zero = 0;
	int ident, r;
	unsigned i, n = sizeof sensor_init / sizeof sensor_init[0];

	if (c->width == 0)
		c->width = 356;
	if (c->width != 356) {
		fprintf(stderr, "stv06xx: this camera only does 356x292\n");
		return -1;
	}
	c->height = 292;

	msleep(250);
	ident = read_sensor(c, VV6410_DEVICEH);
	if (ident < 0)
		return -1;
	if (ident != 0x19) {
		fprintf(stderr, "stv06xx: sensor id %02x is not a vv6410\n", ident);
		return -1;
	}
	if (c->verbose)
		fprintf(stderr, "stv06xx: vv6410 sensor id %02x\n", ident);

	for (i = 0; i < sizeof bridge_init / sizeof bridge_init[0]; i++)
		write_bridge_try(c, bridge_init[i][0], bridge_init[i][1]);
	if (c->usb_err < 0)
		return -1;

	/* stv06xx_write_sensor_bytes(): the whole table in one command */
	memset(buf, 0, sizeof buf);
	for (i = 0; i < n; i++) {
		buf[i] = sensor_init[i][0];
		buf[0x10 + i] = sensor_init[i][1];
	}
	buf[0x20] = VV_I2C_ADDR;
	buf[0x21] = n - 1;
	buf[0x22] = I2C_WRITE_CMD;
	r = libusb_control_transfer(c->h, 0x40, 0x04, 0x0400, 0, buf, sizeof buf,
				    CTRL_TIMEOUT);
	if (r >= 0)
		r = libusb_control_transfer(c->h, 0x40, 0x04, 0x1704, 0, &zero, 1,
					    CTRL_TIMEOUT);
	if (r < 0) {
		fprintf(stderr, "stv06xx: sensor init: %s\n", libusb_error_name(r));
		return -1;
	}
	return 0;
}

/* pb0100_init() */
static int stv06xx_init(struct cam *c)
{
	struct sd *sd = calloc(1, sizeof *sd);
	int ident;

	if (!sd)
		return -1;
	c->priv = sd;
	sd->vv6410 = c->drv == &stv06xx_vv6410_driver;
	sd->auto_exp = c->exposure < 0;
	if (sd->vv6410) {
		/* kernel control defaults: exposure 20000, gain 10 */
		sd->exp_min = 200;
		sd->exp_max = 32768;
		sd->gain_min = 0;
		sd->gain_max = 15;
		sd->exp = c->exposure < 0 ? 20000 : c->exposure;
		sd->gain = c->gain < 0 ? 10 : c->gain;
	} else {
		sd->exp_min = 1;
		sd->exp_max = 0xf0;
		sd->gain_min = 12;	/* the kernel's PB_ADCGLOBALGAIN */
		sd->gain_max = 0xff;
		sd->exp = c->exposure < 0 ? 0x30 : c->exposure;
		sd->gain = c->gain < 0 ? sd->gain_min : c->gain;
	}
	if (sd->exp > (sd->vv6410 ? 32768 : 511) || sd->gain > sd->gain_max) {
		fprintf(stderr, "stv06xx: exposure is 0..%d, gain 0..%d\n",
			sd->vv6410 ? 32768 : 511, sd->gain_max);
		return -1;
	}
	if (sd->vv6410)
		return vv6410_init(c);

	if (c->width == 0)
		c->width = 320;
	if (c->width == 320) {
		c->height = 240;
	} else if (c->width == 352) {
		c->height = 288;
	} else {
		fprintf(stderr, "stv06xx: width must be 320 or 352\n");
		return -1;
	}

	msleep(250);		/* let the usb init settle, as the kernel does */

	ident = read_sensor(c, PB_IDENT);
	if (ident < 0)
		return -1;
	if ((ident >> 8) != 0x64) {
		fprintf(stderr, "stv06xx: sensor id %04x is not a pb0100 "
			"(this unit may have an hdcs sensor, not ported)\n", ident);
		return -1;
	}
	if (c->verbose)
		fprintf(stderr, "stv06xx: pb0100 sensor id %04x\n", ident);

	write_bridge(c, STV_REG00, 1);
	write_bridge(c, STV_SCAN_RATE, 0);

	/* Reset sensor */
	write_sensor(c, PB_RESET, 1);
	write_sensor(c, PB_RESET, 0);

	/* Disable chip */
	write_sensor(c, PB_CONTROL, BIT(5)|BIT(3));

	/* Gain stuff...*/
	write_sensor(c, PB_PREADCTRL, BIT(12)|BIT(10)|BIT(6));
	write_sensor(c, PB_ADCGLOBALGAIN, 12);

	/* Set up auto-exposure */
	write_sensor(c, PB_R28, 12);
	write_sensor(c, PB_ADCMAXGAIN, 180);
	write_sensor(c, PB_ADCMINGAIN, 12);
	write_sensor(c, PB_R54, 3);
	write_sensor(c, PB_R55, 0);
	write_sensor(c, PB_UPDATEINT, 1);
	write_sensor(c, PB_R15, 800);
	write_sensor(c, PB_R17, 10);

	write_sensor(c, PB_EXPGAIN, 0);

	write_sensor(c, PB_VOFFSET, 0);
	write_sensor(c, PB_ADCGAINH, 11);
	/* Set black level (important!) */
	write_sensor(c, PB_ADCGAINL, 0);

	write_bridge(c, STV_REG00, 0x11);
	write_bridge(c, STV_REG03, 0x45);
	write_bridge(c, STV_REG04, 0x07);

	/* Scan/timing for the sensor */
	write_sensor(c, PB_ROWSPEED, BIT(4)|BIT(3)|BIT(1));
	write_sensor(c, PB_CFILLIN, 14);
	write_sensor(c, PB_VBL, 0);
	write_sensor(c, PB_FINTTIME, 0);
	write_sensor(c, PB_RINTTIME, 123);

	write_bridge(c, STV_REG01, 0xc2);
	write_bridge(c, STV_REG02, 0xb0);
	return c->usb_err < 0 ? -1 : 0;
}

/* stv06xx_start() + pb0100_start() + the control defaults gspca applies
 * right after start */
static int stv06xx_start(struct cam *c)
{
	/* the kernel's per mode ceiling: 847 for 320x240, 923 for 352x288 */
	int max_packet_size = c->width == 320 ? 847 : 923;
	struct sd *sd = c->priv;
	int packet_size = c->iso_maxpkt;

	if (sd->vv6410) {
		/* stv06xx_start() + vv6410_start() */
		write_bridge(c, STV_ISO_SIZE_L, packet_size);
		write_bridge_try(c, STV_Y_CTRL, 0x01);
		write_bridge_try(c, STV_X_CTRL, 0x0a);
		write_bridge_try(c, STV_SCAN_RATE, 0x00);
		write_bridge(c, STV_LED_CTRL, 1);
		write_sensor(c, VV6410_SETUP0, 0);
		write_bridge(c, STV_ISO_ENABLE, 1);
		set_exposure(c, sd->exp);
		set_gain(c, sd->gain);
		sd->mean_fresh = 0;
		sd->settle = AE_SETTLE;
		sd->skip_first = 1;
		return c->usb_err < 0 ? -1 : 0;
	}

	if (packet_size > max_packet_size)
		packet_size = max_packet_size;
	write_bridge(c, STV_ISO_SIZE_L, packet_size);

	/* If we don't have enough bandwidth use a lower framerate */
	if (packet_size < max_packet_size)
		write_sensor(c, PB_ROWSPEED, BIT(4)|BIT(3)|BIT(1));
	else
		write_sensor(c, PB_ROWSPEED, BIT(5)|BIT(3)|BIT(1));

	/* Setup sensor window */
	if (c->width == 320) {
		write_sensor(c, PB_RSTART, 30);
		write_sensor(c, PB_CSTART, 20);
		write_sensor(c, PB_RWSIZE, 240 - 1);
		write_sensor(c, PB_CWSIZE, 320 - 1);
	} else {
		write_sensor(c, PB_RSTART, 8);
		write_sensor(c, PB_CSTART, 4);
		write_sensor(c, PB_RWSIZE, 288 - 1);
		write_sensor(c, PB_CWSIZE, 352 - 1);
	}

	write_bridge(c, STV_Y_CTRL, 0x01);
	write_bridge(c, STV_X_CTRL, 0x0a);
	/* larger -> slower */
	write_bridge(c, STV_SCAN_RATE, 0x20);

	write_sensor(c, PB_CONTROL, BIT(5)|BIT(3)|BIT(1));

	/* Start isochronous streaming */
	write_bridge(c, STV_ISO_ENABLE, 1);

	/* sensor auto exposure off (see AE_TARGET), exposure and gain by hand */
	write_sensor(c, PB_EXPGAIN, 0);
	write_sensor(c, PB_RINTTIME, sd->exp);
	write_sensor(c, PB_ADCGLOBALGAIN, sd->gain);
	sd->mean_fresh = 0;
	sd->settle = AE_SETTLE;
	sd->skip_first = 1;

	return c->usb_err < 0 ? -1 : 0;
}

static int scale(int v, int num, int den, int lo, int hi)
{
	int n = v * num / den;

	if (n == v)
		n += num > den ? 1 : -1;
	return n < lo ? lo : n > hi ? hi : n;
}

/* about every 100ms: nudge exposure, then gain, toward AE_TARGET */
static void stv06xx_poll(struct cam *c)
{
	struct sd *sd = c->priv;
	int mean, num, den, exp, gain;

	if (!sd->auto_exp || !sd->mean_fresh)
		return;
	sd->mean_fresh = 0;
	if (sd->settle > 0) {
		sd->settle--;
		return;
	}
	mean = sd->mean < 1 ? 1 : sd->mean;
	if (mean > AE_TARGET - AE_DEADBAND && mean < AE_TARGET + AE_DEADBAND)
		return;
	/* at most x1.5 or x0.7 per step so it cannot swing wildly */
	num = AE_TARGET;
	den = mean;
	if (num * 2 > den * 3) {
		num = 3;
		den = 2;
	} else if (num * 10 < den * 7) {
		num = 7;
		den = 10;
	}
	exp = sd->exp;
	gain = sd->gain;
	if (num > den) {		/* too dark: exposure first, then gain */
		if (exp < sd->exp_max)
			exp = scale(exp, num, den, sd->exp_min, sd->exp_max);
		else
			gain = scale(gain, num, den, sd->gain_min, sd->gain_max);
	} else {			/* too bright: gain first, then exposure */
		if (gain > sd->gain_min)
			gain = scale(gain, num, den, sd->gain_min, sd->gain_max);
		else
			exp = scale(exp, num, den, sd->exp_min, sd->exp_max);
	}
	if (exp == sd->exp && gain == sd->gain)
		return;			/* at a limit */
	if (c->verbose)
		fprintf(stderr, "stv06xx: mean %d, exposure %d -> %d, gain %d -> %d\n",
			sd->mean, sd->exp, exp, sd->gain, gain);
	if (exp != sd->exp)
		set_exposure(c, exp);
	if (gain != sd->gain)
		set_gain(c, gain);
	sd->exp = exp;
	sd->gain = gain;
	sd->settle = AE_SETTLE;
}

static void stv06xx_stop(struct cam *c)
{
	struct sd *sd = c->priv;

	c->usb_err = 0;
	write_bridge(c, STV_ISO_ENABLE, 0);
	if (!sd)
		return;
	if (sd->vv6410) {	/* vv6410_stop() */
		write_bridge(c, STV_LED_CTRL, 0);
		write_sensor(c, VV6410_SETUP0, VV6410_LOW_POWER_MODE);
		return;
	}
	write_sensor(c, PB_ABORTFRAME, 1);
	/* Set bit 1 to zero */
	write_sensor(c, PB_CONTROL, BIT(5)|BIT(3));
}

/*
 * Each packet contains an integral number of chunks. Each chunk has
 * 2-bytes identification, followed by 2-bytes that describe the chunk
 * length:
 * 8001/8005/C001/C005 - Begin new frame
 * 8002/8006/C002/C006 - End frame
 * 0200/4200           - Contains actual image data, bayer or compressed
 */
static void stv06xx_pkt_scan(struct cam *c, const u8 *data, int len)
{
	struct sd *sd = c->priv;

	while (len) {
		int id, chunk_len, i;

		if (len < 4)
			return;
		id = (data[0] << 8) | data[1];
		chunk_len = (data[2] << 8) | data[3];
		data += 4;
		len -= 4;

		if (len < chunk_len) {
			frame_add(c, DISCARD_PACKET, NULL, 0);
			return;
		}

		switch (id) {
		case 0x0200:
		case 0x4200:
			frame_add(c, INTER_PACKET, data, chunk_len);
			for (i = 0; i < chunk_len; i += 8)
				sd->sum += data[i];
			sd->n += (chunk_len + 7) / 8;
			break;
		case 0x8001:
		case 0x8005:
		case 0xc001:
		case 0xc005:
			frame_add(c, FIRST_PACKET, NULL, 0);
			sd->sum = sd->n = 0;
			break;
		case 0x8002:
		case 0x8006:
		case 0xc002:
			if (sd->skip_first) {
				if (c->frame_len != c->width * c->height) {
					frame_add(c, DISCARD_PACKET, NULL, 0);
					break;
				}
				sd->skip_first = 0;
			}
			frame_add(c, LAST_PACKET, NULL, 0);
			/* only whole frames: a short one would skew the mean */
			if (sd->n >= c->width * c->height / 8) {
				sd->mean = sd->sum / sd->n;
				sd->mean_fresh = 1;
			}
			break;
		default:
			if (c->verbose)
				fprintf(stderr, "stv06xx: chunk %04x, %d bytes\n",
					id, chunk_len);
		}
		data += chunk_len;
		len -= chunk_len;
	}
}

static void stv06xx_test_break(struct cam *c)
{
	write_bridge(c, STV_ISO_ENABLE, 0);
}

const struct cam_driver stv06xx_pb0100_driver = {
	.name = "logitech-quickcam-express-stv0600-pb0100",
	.vid = 0x046d,
	.pid = 0x0840,
	.iso_ep = 0x81,
	.out_format = "bayer grbg8 -> jpeg",
	.bayer = BAYER_GRBG,
	.init = stv06xx_init,
	.start = stv06xx_start,
	.poll = stv06xx_poll,
	.stop = stv06xx_stop,
	.pkt_scan = stv06xx_pkt_scan,
	.test_break = stv06xx_test_break,
};

const struct cam_driver stv06xx_vv6410_driver = {
	.name = "lego-cam-stv0610-vv6410",
	.vid = 0x046d,
	.pid = 0x0850,
	.iso_ep = 0x81,
	.out_format = "bayer grbg8 -> jpeg",
	.bayer = BAYER_GRBG,
	.init = stv06xx_init,
	.start = stv06xx_start,
	.poll = stv06xx_poll,
	.stop = stv06xx_stop,
	.pkt_scan = stv06xx_pkt_scan,
	.test_break = stv06xx_test_break,
};
