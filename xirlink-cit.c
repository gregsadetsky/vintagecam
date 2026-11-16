// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Xirlink "C-It" / IBM PC Camera, model 2 only (0545:8080 with device
 * version 3.0a), ported from linux drivers/media/usb/gspca/xirlink_cit.c
 *
 * Copyright (C) 2010 Hans de Goede <hdegoede@redhat.com>
 * the kernel driver in turn is based on the old ibmcam driver:
 * (C) Copyright 1999 Johannes Erdfelt, 1999 Randy Dunlap
 *
 * 320x240 and 352x288 are raw 8 bit bayer, the engine turns each frame into
 * a jpeg. 176x144 uses another pixel format and is not ported.
 */
#include <stdio.h>
#include <stdlib.h>
#include "cam.h"

struct sd {
	int sof_read;
	int sof_len;
	int skip_first;		/* the frame in flight at start is not whole */
};

static void cit_write_reg(struct cam *c, u16 value, u16 index)
{
	int r;

	if (c->usb_err < 0)
		return;
	if (index != 0x0124 && index != 0x0127 && index < 0x012d)
		value = poke_lookup(c, 'r', index, value);
	r = libusb_control_transfer(c->h,
			LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR |
			LIBUSB_RECIPIENT_ENDPOINT,
			0x00, value, index, NULL, 0, 1000);
	if (r < 0) {
		fprintf(stderr, "cit: write %04x -> %04x: %s\n", value, index,
			libusb_error_name(r));
		c->usb_err = r;
	}
}

static void cit_read_reg(struct cam *c, u16 index)
{
	u8 buf[8];
	int r;

	if (c->usb_err < 0)
		return;
	r = libusb_control_transfer(c->h,
			LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR |
			LIBUSB_RECIPIENT_ENDPOINT,
			0x01, 0x00, index, buf, 8, 1000);
	if (r < 0) {
		fprintf(stderr, "cit: read %04x: %s\n", index, libusb_error_name(r));
		c->usb_err = r;
	} else if (c->verbose) {
		fprintf(stderr, "cit: register %04x value %02x\n", index, buf[0]);
	}
}

static void cit_model2_Packet2(struct cam *c)
{
	cit_write_reg(c, 0x00ff, 0x012d);
	cit_write_reg(c, 0xfea3, 0x0124);
}

/* v1 is the function, v2 its value: --i2c FUNC=VAL overrides one */
static void cit_model2_Packet1(struct cam *c, u16 v1, u16 v2)
{
	v2 = poke_lookup(c, 'i', v1, v2);
	cit_write_reg(c, 0x00aa, 0x012d);
	cit_write_reg(c, 0x00ff, 0x012e);
	cit_write_reg(c, v1,     0x012f);
	cit_write_reg(c, 0x00ff, 0x0130);
	cit_write_reg(c, 0xc719, 0x0124);
	cit_write_reg(c, v2,     0x0127);
	cit_model2_Packet2(c);
}

/* sd_config: everything else is done in start, as in the kernel */
static int cit_init(struct cam *c)
{
	struct sd *sd = calloc(1, sizeof *sd);

	if (!sd)
		return -1;
	c->priv = sd;
	if (c->width == 0)
		c->width = 320;
	if (c->width == 320) {
		c->height = 240;
	} else if (c->width == 352) {
		c->height = 288;
	} else {
		fprintf(stderr, "cit: width must be 320 or 352\n");
		return -1;
	}
	return 0;
}

/* sd_start() + cit_start_model2() + cit_restart_stream() + the control
 * defaults gspca applies after start */
static int cit_start(struct cam *c)
{
	struct sd *sd = c->priv;
	int clock_div, wide = c->width == 352;

	cit_write_reg(c, 0x0000, 0x0100);	/* LED on */
	cit_read_reg(c, 0x0116);
	cit_write_reg(c, 0x0060, 0x0116);
	cit_write_reg(c, 0x0002, 0x0112);
	cit_write_reg(c, 0x00bc, 0x012c);
	cit_write_reg(c, 0x0008, 0x012b);
	cit_write_reg(c, 0x0000, 0x0108);
	cit_write_reg(c, 0x0001, 0x0133);
	cit_write_reg(c, 0x0001, 0x0102);
	if (wide) {	/* 352x288 */
		cit_write_reg(c, 0x002c, 0x0103);	/* All except 320x240 */
		cit_write_reg(c, 0x0000, 0x0104);	/* Same */
		cit_write_reg(c, 0x0024, 0x0105);	/* 176x144, 352x288 */
	} else {	/* 320x240 */
		cit_write_reg(c, 0x0028, 0x0103);	/* Unique to this mode */
		cit_write_reg(c, 0x0000, 0x0104);	/* Same */
		cit_write_reg(c, 0x001e, 0x0105);	/* 320x240, 352x240 */
	}
	cit_write_reg(c, 0x0039, 0x010a);	/* All except 176x144 */
	cit_write_reg(c, 0x0070, 0x0119);	/* All except 176x144 */
	sd->sof_len = 2;

	cit_write_reg(c, 0x0000, 0x0100);	/* LED on */
	cit_write_reg(c, 0x0040, 0x0111);
	cit_write_reg(c, 0x00c0, 0x0111);
	cit_write_reg(c, 0x009b, 0x010f);
	cit_write_reg(c, 0x00bb, 0x010f);

	/* Hardware settings, may affect CMOS sensor; not user controls! */
	cit_model2_Packet1(c, 0x000a, 0x005c);
	cit_model2_Packet1(c, 0x0004, 0x0000);
	cit_model2_Packet1(c, 0x0006, 0x00fb);
	cit_model2_Packet1(c, 0x0008, 0x0000);
	cit_model2_Packet1(c, 0x000c, 0x0009);
	cit_model2_Packet1(c, 0x0012, 0x000a);
	cit_model2_Packet1(c, 0x002a, 0x0000);
	cit_model2_Packet1(c, 0x002c, 0x0000);
	cit_model2_Packet1(c, 0x002e, 0x0008);
	cit_model2_Packet1(c, 0x0030, 0x0000);

	if (wide) {
		cit_model2_Packet1(c, 0x0014, 0x0003);
		cit_model2_Packet1(c, 0x0016, 0x0002); /* Horizontal shift */
		cit_model2_Packet1(c, 0x0018, 0x004a); /* Another hardware setting */
		clock_div = 16;
	} else {
		cit_model2_Packet1(c, 0x0014, 0x0009);
		cit_model2_Packet1(c, 0x0016, 0x0005); /* Horizontal shift */
		cit_model2_Packet1(c, 0x0018, 0x0044); /* Another hardware setting */
		clock_div = 8;
	}
	/* frame rate, range [0..0x1f], see the long note in the kernel */
	cit_model2_Packet1(c, 0x001c, clock_div);
	cit_model2_Packet1(c, 0x0026, wide ? 0x0048 : 0x0044);
	cit_model2_Packet1(c, 0x0028, 1);	/* lighting control default */
	/* color balance rg2 */
	cit_model2_Packet1(c, 0x001e, 0x002f);
	/* saturation */
	cit_model2_Packet1(c, 0x0020, 0x0034);
	/* color balance yb */
	cit_model2_Packet1(c, 0x0022, 0x00a0);
	/* Hardware control command */
	cit_model2_Packet1(c, 0x0030, 0x0004);

	/* Program max isoc packet size */
	cit_write_reg(c, c->iso_maxpkt >> 8, 0x0106);
	cit_write_reg(c, c->iso_maxpkt & 0xff, 0x0107);

	/* cit_restart_stream() */
	cit_write_reg(c, 0x00c0, 0x010c);	/* Go! */
	libusb_clear_halt(c->h, c->drv->iso_ep);
	sd->sof_read = 0;
	sd->skip_first = 1;

	/* control defaults: brightness 32 of 0..63, hue 63 of 0..127 */
	cit_model2_Packet1(c, 0x001a, 0x60 + 32 * 2254 / 1000);
	cit_model2_Packet1(c, 0x0024, 63);

	return c->usb_err < 0 ? -1 : 0;
}

/* sd_stopN() + sd_stop0() */
static void cit_stop(struct cam *c)
{
	c->usb_err = 0;
	cit_write_reg(c, 0x0000, 0x010c);
	cit_model2_Packet1(c, 0x0030, 0x0004);
	cit_write_reg(c, 0x0080, 0x0100);	/* LED Off */
	cit_write_reg(c, 0x0020, 0x0111);
	cit_write_reg(c, 0x00a0, 0x0111);
	cit_model2_Packet1(c, 0x0030, 0x0002);
	cit_write_reg(c, 0x0020, 0x0111);
	cit_write_reg(c, 0x0000, 0x0112);
}

/* the start of frame marker is 00 ff. the kernel notes it wants a longer
 * signature to avoid false positives. */
static const u8 *cit_find_sof(struct sd *sd, const u8 *data, int len)
{
	int i;

	for (i = 0; i < len; i++) {
		switch (sd->sof_read) {
		case 0:
			if (data[i] == 0x00)
				sd->sof_read++;
			break;
		case 1:
			sd->sof_read = 0;
			if (data[i] == 0xff)
				return data + i + (sd->sof_len - 1);
			break;
		}
	}
	return NULL;
}

static void cit_pkt_scan(struct cam *c, const u8 *data, int len)
{
	struct sd *sd = c->priv;
	const u8 *sof = cit_find_sof(sd, data, len);

	if (sof) {
		int n = sof - data, want = c->width * c->height;

		/* finish decoding current frame */
		if (n > sd->sof_len)
			n -= sd->sof_len;
		else
			n = 0;
		/* not in the kernel: 00 ff can also be two pixels. frames were
		 * always exactly width x height bytes on a real unit, so a
		 * marker that would end the frame early is taken as picture. */
		if (c->last_pkt != DISCARD_PACKET && c->frame_len + n < want) {
			frame_add(c, INTER_PACKET, data, len);
			return;
		}
		if (sd->skip_first && c->last_pkt != DISCARD_PACKET &&
		    c->frame_len + n != want)
			frame_add(c, DISCARD_PACKET, NULL, 0);
		else if (c->last_pkt != DISCARD_PACKET)
			sd->skip_first = 0;
		frame_add(c, LAST_PACKET, data, n);
		frame_add(c, FIRST_PACKET, NULL, 0);
		len -= sof - data;
		data = sof;
	}
	frame_add(c, INTER_PACKET, data, len);
}

/* test hook: the kernel's stopN write, stops the stream */
static void cit_test_break(struct cam *c)
{
	c->usb_err = 0;
	cit_write_reg(c, 0x0000, 0x010c);
}

const struct cam_driver xirlink_cit_model2_driver = {
	.name = "ibm-pc-camera-xirlink-cit-model2",
	.vid = 0x0545,
	.pid = 0x8080,
	.bcd = 0x030a,
	.iso_ep = 0x81,
	.out_format = "bayer grbg8 -> jpeg",
	.bayer = BAYER_GRBG,
	.init = cit_init,
	.start = cit_start,
	.stop = cit_stop,
	.pkt_scan = cit_pkt_scan,
	.test_break = cit_test_break,
};
