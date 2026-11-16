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

/* raw 8 bit bayer layouts, named by the first two rows like v4l2 does */
enum bayer { BAYER_NONE, BAYER_GRBG, BAYER_RGGB, BAYER_BGGR, BAYER_GBRG };

struct cam;

struct cam_driver {
	const char *name;
	u16 vid, pid;
	u16 bcd;		/* device version to match, 0 = any */
	u16 alt_vid, alt_pid;	/* a second id the same camera was seen with, 0 = none */
	u8 iso_ep;
	const char *out_format;	/* what a finished frame is, e.g. "mjpeg" */
	enum bayer bayer;	/* not NONE: frames are raw, the engine makes jpegs */
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
	int width, height;	/* width 0 = the driver's default */
	int light_freq;		/* 0, 50, 60 */
	int exposure, gain;	/* -1 = the driver decides (auto if it can) */
	int iso_maxpkt;		/* packet size of the chosen alt setting */

	/* debug: override values the driver's init tables write.
	 * kind 'r' bridge register, 'i' sensor i2c register */
	struct { char kind; unsigned idx, val; int used; } pokes[32];
	int n_pokes;

	/* frame assembly */
	u8 *frame;
	int frame_len, frame_cap;
	enum pkt_type last_pkt;
	int frame_seq;		/* camera's own frame counter, -1 if none */
	const char *want_restart;	/* driver asks for a stream restart, and why */
	int drv_state;		/* small driver memory that outlives priv, 0 = unset */
	void (*on_frame)(struct cam *c, const u8 *data, int len);

	void *priv;
};

void frame_add(struct cam *c, enum pkt_type type, const u8 *data, int len);
void msleep(int ms);
int poke_lookup(struct cam *c, char kind, unsigned idx, int dflt);

extern const struct cam_driver zc3xx_gc0303_driver;
extern const struct cam_driver stv06xx_pb0100_driver;
extern const struct cam_driver zc3xx_pas106_driver;
extern const struct cam_driver xirlink_cit_model2_driver;
extern const struct cam_driver stv06xx_vv6410_driver;
extern const struct cam_driver ov519_ov7648_driver;

int export_main(const char *mjpeg_path, double fps);

#endif
