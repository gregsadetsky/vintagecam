// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * z-star/vimicro zc3xx bridge + gc0303 sensor (creative 041e:4051).
 * ported from linux drivers/media/usb/gspca/zc3xx.c
 *   Copyright (C) 2009-2012 Jean-Francois Moine
 *   Copyright (C) 2004 2005 2006 Michel Xhaard
 * only the SENSOR_GC0303 paths are kept. the kernel line each block comes
 * from is named in the comments (function names, not line numbers).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cam.h"
#include "gspca/jpeg.h"
#include "gspca/zc3xx-reg.h"

#define REG08_DEF 3		/* default JPEG compression (75%) */
static const u8 jpeg_qual[] = {50, 75, 87};

struct usb_action {
	u8 req;
	u8 val;
	u16 idx;
};

#include "gspca/zc3xx-gc0303.h"

struct sd {
	u8 reg08;
	u8 reg07;	/* bit rate control state (transfer_update) */
	int good;
	u8 jpeg_hdr[JPEG_HDR_SZ];
};

static u8 reg_r(struct cam *c, u16 index)
{
	u8 buf[1] = {0};
	int ret;

	if (c->usb_err < 0)
		return 0;
	ret = libusb_control_transfer(c->h,
			LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR |
			LIBUSB_RECIPIENT_DEVICE,
			0xa1, 0x01, index, buf, 1, 500);
	if (ret != 1) {
		fprintf(stderr, "zc3xx: reg_r %04x err %d (%s)\n", index, ret,
			ret < 0 ? libusb_error_name(ret) : "short");
		c->usb_err = ret < 0 ? ret : LIBUSB_ERROR_IO;
		return 0;
	}
	return buf[0];
}

static void reg_w(struct cam *c, u8 value, u16 index)
{
	int ret;

	if (c->usb_err < 0)
		return;
	ret = libusb_control_transfer(c->h,
			LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR |
			LIBUSB_RECIPIENT_DEVICE,
			0xa0, value, index, NULL, 0, 500);
	if (ret < 0) {
		fprintf(stderr, "zc3xx: reg_w %02x -> %04x err %s\n", value,
			index, libusb_error_name(ret));
		c->usb_err = ret;
	}
}

static u16 i2c_read(struct cam *c, u8 reg)
{
	u8 retbyte;
	u16 retval;

	if (c->usb_err < 0)
		return 0;
	reg_w(c, reg, 0x0092);
	reg_w(c, 0x02, 0x0090);			/* <- read command */
	msleep(20);
	retbyte = reg_r(c, 0x0091);		/* read status */
	if (retbyte != 0x00)
		fprintf(stderr, "zc3xx: i2c_r status error %02x\n", retbyte);
	retval = reg_r(c, 0x0095);		/* read Lowbyte */
	retval |= reg_r(c, 0x0096) << 8;	/* read Hightbyte */
	return retval;
}

static u8 i2c_write(struct cam *c, u8 reg, u8 valL, u8 valH)
{
	u8 retbyte;

	if (c->usb_err < 0)
		return 0;
	reg_w(c, reg, 0x92);
	reg_w(c, valL, 0x93);
	reg_w(c, valH, 0x94);
	reg_w(c, 0x01, 0x90);			/* <- write command */
	msleep(1);
	retbyte = reg_r(c, 0x0091);		/* read status */
	if (retbyte != 0x00)
		fprintf(stderr, "zc3xx: i2c_w status error %02x\n", retbyte);
	return retbyte;
}

static void usb_exchange(struct cam *c, const struct usb_action *action)
{
	while (action->req) {
		switch (action->req) {
		case 0xa0:	/* write register */
			reg_w(c, action->val, action->idx);
			break;
		case 0xa1:	/* read status */
			reg_r(c, action->idx);
			break;
		case 0xaa:
			i2c_write(c, action->val, action->idx & 0xff,
				  action->idx >> 8);
			break;
		case 0xbb:
			i2c_write(c, action->idx >> 8, action->idx & 0xff,
				  action->val);
			break;
		default:
			msleep(action->idx);
			break;
		}
		action++;
		msleep(1);
	}
}

static void setmatrix(struct cam *c)
{
	static const u8 gc0303_matrix[9] =
		{0x6c, 0xea, 0xea, 0xea, 0x6c, 0xea, 0xea, 0xea, 0x6c};
	int i;

	for (i = 0; i < 9; i++)
		reg_w(c, gc0303_matrix[i], 0x010a + i);
}

static void setsharpness(struct cam *c, s32 val)
{
	static const u8 sharpness_tb[][2] = {
		{0x02, 0x03},
		{0x04, 0x07},
		{0x08, 0x0f},
		{0x10, 0x1e}
	};

	reg_w(c, sharpness_tb[val][0], 0x01c6);
	reg_r(c, 0x01c8);
	reg_r(c, 0x01c9);
	reg_r(c, 0x01ca);
	reg_w(c, sharpness_tb[val][1], 0x01cb);
}

static void setcontrast(struct cam *c, s32 gamma, s32 brightness, s32 contrast)
{
	const u8 *Tgamma;
	int g, i, adj, gp1, gp2;
	u8 gr[16];
	static const u8 delta_b[16] =		/* delta for brightness */
		{0x50, 0x38, 0x2d, 0x28, 0x24, 0x21, 0x1e, 0x1d,
		 0x1d, 0x1b, 0x1b, 0x1b, 0x19, 0x18, 0x18, 0x18};
	static const u8 delta_c[16] =		/* delta for contrast */
		{0x2c, 0x1a, 0x12, 0x0c, 0x0a, 0x06, 0x06, 0x06,
		 0x04, 0x06, 0x04, 0x04, 0x03, 0x03, 0x02, 0x02};
	static const u8 gamma_tb[6][16] = {
		{0x00, 0x00, 0x03, 0x0d, 0x1b, 0x2e, 0x45, 0x5f,
		 0x79, 0x93, 0xab, 0xc1, 0xd4, 0xe5, 0xf3, 0xff},
		{0x01, 0x0c, 0x1f, 0x3a, 0x53, 0x6d, 0x85, 0x9c,
		 0xb0, 0xc2, 0xd1, 0xde, 0xe9, 0xf2, 0xf9, 0xff},
		{0x04, 0x16, 0x30, 0x4e, 0x68, 0x81, 0x98, 0xac,
		 0xbe, 0xcd, 0xda, 0xe4, 0xed, 0xf5, 0xfb, 0xff},
		{0x13, 0x38, 0x59, 0x79, 0x92, 0xa7, 0xb9, 0xc8,
		 0xd4, 0xdf, 0xe7, 0xee, 0xf4, 0xf9, 0xfc, 0xff},
		{0x20, 0x4b, 0x6e, 0x8d, 0xa3, 0xb5, 0xc5, 0xd2,
		 0xdc, 0xe5, 0xec, 0xf2, 0xf6, 0xfa, 0xfd, 0xff},
		{0x24, 0x44, 0x64, 0x84, 0x9d, 0xb2, 0xc4, 0xd3,
		 0xe0, 0xeb, 0xf4, 0xff, 0xff, 0xff, 0xff, 0xff},
	};

	Tgamma = gamma_tb[gamma - 1];

	contrast -= 128; /* -128 / 127 */
	brightness -= 128; /* -128 / 92 */
	adj = 0;
	gp1 = gp2 = 0;
	for (i = 0; i < 16; i++) {
		g = Tgamma[i] + delta_b[i] * brightness / 256
				- delta_c[i] * contrast / 256 - adj / 2;
		if (g > 0xff)
			g = 0xff;
		else if (g < 0)
			g = 0;
		reg_w(c, g, 0x0120 + i);	/* gamma */
		if (contrast > 0)
			adj--;
		else if (contrast < 0)
			adj++;
		if (i > 1)
			gr[i - 1] = (g - gp2) / 2;
		else if (i != 0)
			gr[0] = gp1 == 0 ? 0 : (g - gp1);
		gp2 = gp1;
		gp1 = g;
	}
	gr[15] = (0xff - gp2) / 2;
	for (i = 0; i < 16; i++)
		reg_w(c, gr[i], 0x0130 + i);	/* gradient */
}

static void setlightfreq(struct cam *c, int scale)
{
	const struct usb_action *t;

	switch (c->light_freq) {
	case 50:
		t = scale ? gc0303_50HZScale : gc0303_50HZ;
		break;
	case 60:
		t = scale ? gc0303_60HZScale : gc0303_60HZ;
		break;
	default:
		t = scale ? gc0303_NoFlickerScale : gc0303_NoFlicker;
		break;
	}
	usb_exchange(c, t);
}

/* diagnostic only: the gc0303 leg of vga_3wr_probe(), minus the write to
 * reg 0x0010 (the kernel skips probing this id entirely with the note
 * "may probe but with no write in reg 0x0010"). expect 0x0011. */
static u16 probe_gc0303(struct cam *c)
{
	reg_w(c, 0x01, 0x0000);
	reg_w(c, 0x01, 0x0001);
	reg_w(c, 0x98, 0x008b);
	reg_w(c, 0x03, 0x0012);
	msleep(2);
	reg_w(c, 0x01, 0x0012);
	return i2c_read(c, 0x00);
}

/* sd_config + sd_init, gc0303 path */
static int zc3xx_init(struct cam *c)
{
	struct sd *sd = calloc(1, sizeof *sd);
	u16 id;

	if (!sd)
		return -1;
	c->priv = sd;
	sd->reg08 = REG08_DEF;

	if (c->width != 640 && c->width != 320) {
		fprintf(stderr, "zc3xx: width must be 320 or 640\n");
		return -1;
	}
	c->height = c->width * 3 / 4;

	id = probe_gc0303(c);
	fprintf(stderr, "zc3xx: sensor id read 0x%04x (gc0303 = 0x0011)\n", id);

	reg_w(c, 0x02, 0x0010);
	reg_r(c, 0x0010);
	reg_w(c, 0x01, 0x0000);		/* switch off the led */
	return c->usb_err;
}

/* sd_start, gc0303 path */
static int zc3xx_start(struct cam *c)
{
	struct sd *sd = c->priv;
	int scale = c->width == 320;

	jpeg_define(sd->jpeg_hdr, c->height, c->width, 0x21);	/* JPEG 422 */

	usb_exchange(c, scale ? gc0303_InitialScale : gc0303_Initial);

	reg_r(c, 0x0002);		/* --> 0x40 */
	reg_w(c, 0x09, 0x01ad);		/* (from win traces) */
	reg_w(c, 0x15, 0x01ae);
	reg_w(c, 0x0d, 0x003a);
	reg_w(c, 0x02, 0x003b);
	reg_w(c, 0x00, 0x0038);

	setmatrix(c);
	setsharpness(c, 2);
	setcontrast(c, 3, 128, 128);	/* gamma[SENSOR_GC0303] = 3 */
	setmatrix(c);			/* one more time? */

	/* setquality */
	jpeg_set_qual(sd->jpeg_hdr, jpeg_qual[sd->reg08 >> 1]);
	reg_w(c, sd->reg08, ZC3XX_R008_CLOCKSETTING);

	/* Start with BRC disabled, poll will enable it if needed */
	sd->reg07 = 0;
	sd->good = 0;
	reg_w(c, 0x00, 0x0007);
	setlightfreq(c, scale);

	reg_w(c, 0x42, 0x0180);		/* setautogain(1) */
	return c->usb_err;
}

/* one turn of transfer_update(): fifo overflow -> bit rate control */
static void zc3xx_poll(struct cam *c)
{
	struct sd *sd = c->priv;
	int change;
	u8 reg11;

	c->usb_err = 0;
	reg11 = reg_r(c, 0x0011);
	if (c->usb_err)
		return;

	change = reg11 & 0x01;
	if (change) {				/* overflow */
		sd->good = 0;
		if (sd->reg07 == 0)	/* Bit Rate Control not enabled? */
			sd->reg07 = 0x32;	/* Allow 98 bytes / unit */
		else if (sd->reg07 > 2)
			sd->reg07 -= 2;	/* Decrease allowed bytes / unit */
		else
			change = 0;
	} else {				/* no overflow */
		sd->good++;
		if (sd->good >= 10) {
			sd->good = 0;
			if (sd->reg07) {	/* BRC enabled? */
				change = 1;
				if (sd->reg07 < 0x32)
					sd->reg07 += 2;
				else
					sd->reg07 = 0;
			}
		}
	}
	if (change) {
		if (c->verbose)
			fprintf(stderr, "zc3xx: brc reg07 -> %02x\n", sd->reg07);
		reg_w(c, sd->reg07, 0x0007);
	}
}

/* sd_stop0 -> send_unknown(), gc0303 has no extra writes */
static void zc3xx_stop(struct cam *c)
{
	c->usb_err = 0;
	reg_w(c, 0x01, 0x0000);		/* bridge reset */
}

static void zc3xx_pkt_scan(struct cam *c, const u8 *data, int len)
{
	struct sd *sd = c->priv;

	/* check the JPEG end of frame */
	if (len >= 3 && data[len - 3] == 0xff && data[len - 2] == 0xd9) {
		/*fixme: what does the last byte mean?*/
		frame_add(c, LAST_PACKET, data, len - 1);
		return;
	}

	/* check the JPEG start of a frame */
	if (len >= 2 && data[0] == 0xff && data[1] == 0xd8) {
		/* put the JPEG header in the new frame */
		frame_add(c, FIRST_PACKET, sd->jpeg_hdr, JPEG_HDR_SZ);

		/* remove the webcam's header:
		 * ff d8 ff fe 00 0e 00 00 ss ss 00 01 ww ww hh hh pp pp
		 *	- 'ss ss' is the frame sequence number (BE)
		 *	- 'ww ww' and 'hh hh' are the window dimensions (BE)
		 *	- 'pp pp' is the packet sequence number (BE)
		 */
		if (len < 18)
			return;
		data += 18;
		len -= 18;
	}
	frame_add(c, INTER_PACKET, data, len);
}

const struct cam_driver zc3xx_gc0303_driver = {
	.name = "creative-vf0250-zc3xx-gc0303",
	.vid = 0x041e,
	.pid = 0x4051,
	.iso_ep = 0x81,
	.out_format = "mjpeg",
	.init = zc3xx_init,
	.start = zc3xx_start,
	.poll = zc3xx_poll,
	.stop = zc3xx_stop,
	.pkt_scan = zc3xx_pkt_scan,
};
