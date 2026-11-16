// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * STV0600 bridge + Photobit PB-0100 sensor (Logitech QuickCam Express,
 * 046d:0840), ported from linux drivers/media/usb/gspca/stv06xx/
 * (stv06xx.c, stv06xx_pb0100.c).
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

#define BIT(n) (1u << (n))

/* software auto exposure. the sensor's own (PB_EXPGAIN) stayed near white
 * for every target tried on a real unit, so it is left off.
 * measured on the device: row integration time above about 0xf0 lowers the
 * frame rate, so past that point the gain goes up instead. */
#define AE_TARGET	105	/* mean raw pixel value to aim for */
#define AE_DEADBAND	15
#define AE_SETTLE	3	/* frames to wait after a change */
#define EXP_MIN		1
#define EXP_MAX		0xf0
#define GAIN_MIN	12	/* the kernel's PB_ADCGLOBALGAIN */
#define GAIN_MAX	0xff

struct sd {
	int exp, gain;		/* PB_RINTTIME, PB_ADCGLOBALGAIN */
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
		fprintf(stderr, "stv06xx: bridge write %04x: %s\n", address,
			libusb_error_name(r));
		c->usb_err = r;
	}
}

/* the pb0100 has 16 bit registers */
static void write_sensor(struct cam *c, u8 reg, int value)
{
	u8 buf[I2C_BUFFER_LENGTH];
	int r;

	if (c->usb_err < 0)
		return;
	value = poke_lookup(c, 'i', reg, value);
	memset(buf, 0, sizeof buf);
	buf[0] = reg;
	buf[0x10] = value & 0xff;
	buf[0x11] = (value >> 8) & 0xff;
	buf[0x20] = PB_I2C_ADDR;
	buf[0x21] = 0;			/* number of commands - 1 */
	buf[0x22] = I2C_WRITE_CMD;
	r = libusb_control_transfer(c->h, 0x40, 0x04, 0x0400, 0, buf, sizeof buf,
				    CTRL_TIMEOUT);
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
	u8 buf[I2C_BUFFER_LENGTH];
	int r;

	write_bridge(c, STV_I2C_FLUSH, PB_I2C_FLUSH);
	if (c->usb_err < 0)
		return -1;
	memset(buf, 0, sizeof buf);
	buf[0] = reg;
	buf[0x20] = PB_I2C_ADDR;
	buf[0x21] = 0;
	buf[0x22] = I2C_READ_CMD;
	r = libusb_control_transfer(c->h, 0x40, 0x04, 0x1400, 0, buf, sizeof buf,
				    CTRL_TIMEOUT);
	if (r >= 0)
		r = libusb_control_transfer(c->h, 0xc0, 0x04, 0x1410, 0, buf, 2,
					    CTRL_TIMEOUT);
	if (r < 2) {
		fprintf(stderr, "stv06xx: sensor read %02x: %s\n", reg,
			r < 0 ? libusb_error_name(r) : "short");
		c->usb_err = r < 0 ? r : LIBUSB_ERROR_IO;
		return -1;
	}
	return buf[0] | (buf[1] << 8);
}

/* pb0100_init() */
static int stv06xx_init(struct cam *c)
{
	struct sd *sd = calloc(1, sizeof *sd);
	int ident;

	if (!sd)
		return -1;
	c->priv = sd;
	sd->auto_exp = c->exposure < 0;
	sd->exp = c->exposure < 0 ? 0x30 : c->exposure;
	sd->gain = c->gain < 0 ? GAIN_MIN : c->gain;
	if (sd->exp > 511 || sd->gain > 255) {
		fprintf(stderr, "stv06xx: exposure is 0..511, gain 0..255\n");
		return -1;
	}

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
		if (exp < EXP_MAX)
			exp = scale(exp, num, den, EXP_MIN, EXP_MAX);
		else
			gain = scale(gain, num, den, GAIN_MIN, GAIN_MAX);
	} else {			/* too bright: gain first, then exposure */
		if (gain > GAIN_MIN)
			gain = scale(gain, num, den, GAIN_MIN, GAIN_MAX);
		else
			exp = scale(exp, num, den, EXP_MIN, EXP_MAX);
	}
	if (exp == sd->exp && gain == sd->gain)
		return;			/* at a limit */
	if (c->verbose)
		fprintf(stderr, "stv06xx: mean %d, exposure %d -> %d, gain %d -> %d\n",
			sd->mean, sd->exp, exp, sd->gain, gain);
	if (exp != sd->exp)
		write_sensor(c, PB_RINTTIME, exp);
	if (gain != sd->gain)
		write_sensor(c, PB_ADCGLOBALGAIN, gain);
	sd->exp = exp;
	sd->gain = gain;
	sd->settle = AE_SETTLE;
}

static void stv06xx_stop(struct cam *c)
{
	c->usb_err = 0;
	write_bridge(c, STV_ISO_ENABLE, 0);
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
