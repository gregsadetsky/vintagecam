/* SPDX-License-Identifier: GPL-2.0-or-later */
/* shared bits between the usb engine (main.c) and the per-camera drivers.
 * shaped like linux gspca on purpose so subdrivers port over mechanically. */
#ifndef CAM_H
#define CAM_H

#include <stdint.h>
#include <libusb.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef int32_t s32;

enum pkt_type { DISCARD_PACKET, FIRST_PACKET, INTER_PACKET, LAST_PACKET };

struct cam;

struct cam_driver {
	const char *name;
	u16 vid, pid;
	u8 iso_ep;
	const char *out_format;	/* what a finished frame is, e.g. "mjpeg" */
	int (*init)(struct cam *c);	/* once after claim */
	int (*start)(struct cam *c);	/* after set alt, before iso submit */
	void (*poll)(struct cam *c);	/* about every 100ms while streaming */
	void (*stop)(struct cam *c);	/* after alt 0 */
	void (*pkt_scan)(struct cam *c, const u8 *data, int len);
	/* test hook: make the camera itself misbehave (optional) */
	void (*test_break)(struct cam *c);
};

struct cam {
	libusb_device_handle *h;
	const struct cam_driver *drv;
	int usb_err;		/* sticky, like gspca_dev->usb_err */
	int verbose;

	/* options */
	int width, height;
	int light_freq;		/* 0, 50, 60 */

	/* debug: override values the driver's init tables write.
	 * kind 'r' bridge register, 'i' sensor i2c register */
	struct { char kind; unsigned idx, val; int used; } pokes[32];
	int n_pokes;

	/* frame assembly */
	u8 *frame;
	int frame_len, frame_cap;
	enum pkt_type last_pkt;
	int frame_seq;		/* camera's own frame counter, -1 if none */
	void (*on_frame)(struct cam *c, const u8 *data, int len);

	void *priv;
};

void frame_add(struct cam *c, enum pkt_type type, const u8 *data, int len);
void msleep(int ms);
int poke_lookup(struct cam *c, char kind, unsigned idx, int dflt);

extern const struct cam_driver zc3xx_gc0303_driver;

int export_main(const char *mjpeg_path, double fps);

#endif
