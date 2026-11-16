// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * vintagecam: userspace libusb drivers for pre-uvc webcams on macos.
 * finds the first supported camera, streams finished frames to stdout.
 */
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "cam.h"

#define N_XFERS 4
#define PKTS_PER_XFER 32
#define FRAME_CAP (1 << 20)

static const struct cam_driver *drivers[] = {
	&zc3xx_gc0303_driver,
};

static volatile sig_atomic_t stopping;
static int active_xfers;
static long n_frames, n_frame_bytes, n_iso_errs, n_discards, n_pkts;
static long max_frames = -1;
static int dump_pkts;
static double max_seconds;

static void on_signal(int sig)
{
	(void)sig;
	stopping = 1;
}

void msleep(int ms)
{
	struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };

	nanosleep(&ts, NULL);
}

static double now(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

int poke_lookup(struct cam *c, char kind, unsigned idx, int dflt)
{
	int i;

	for (i = 0; i < c->n_pokes; i++)
		if (c->pokes[i].kind == kind && c->pokes[i].idx == idx) {
			fprintf(stderr, "poke %s %04x: %02x -> %02x\n",
				kind == 'r' ? "reg" : "i2c", idx, dflt,
				c->pokes[i].val);
			return c->pokes[i].val;
		}
	return dflt;
}

/* same contract as gspca_frame_add() */
void frame_add(struct cam *c, enum pkt_type type, const u8 *data, int len)
{
	if (type == FIRST_PACKET) {
		c->frame_len = 0;
	} else {
		if (c->last_pkt == DISCARD_PACKET) {
			if (type == LAST_PACKET)
				n_discards++;
			return;
		}
	}
	if (type == DISCARD_PACKET) {
		c->last_pkt = DISCARD_PACKET;
		return;
	}
	if (len > 0) {
		if (c->frame_len + len > c->frame_cap) {
			fprintf(stderr, "frame overflow, discarding\n");
			c->last_pkt = DISCARD_PACKET;
			return;
		}
		memcpy(c->frame + c->frame_len, data, len);
		c->frame_len += len;
	}
	c->last_pkt = type;
	if (type == LAST_PACKET) {
		c->on_frame(c, c->frame, c->frame_len);
		c->last_pkt = DISCARD_PACKET;	/* wait for the next FIRST */
	}
}

static void write_frame(struct cam *c, const u8 *data, int len)
{
	(void)c;
	n_frames++;
	n_frame_bytes += len;
	while (len > 0) {
		ssize_t w = write(STDOUT_FILENO, data, len);

		if (w < 0) {
			if (errno == EINTR)
				continue;
			stopping = 1;	/* reader went away */
			return;
		}
		data += w;
		len -= w;
	}
	if (max_frames > 0 && n_frames >= max_frames)
		stopping = 1;
}

static void LIBUSB_CALL iso_cb(struct libusb_transfer *t)
{
	struct cam *c = t->user_data;
	int i;

	if (t->status == LIBUSB_TRANSFER_CANCELLED ||
	    t->status == LIBUSB_TRANSFER_NO_DEVICE) {
		if (t->status == LIBUSB_TRANSFER_NO_DEVICE)
			stopping = 1;
		active_xfers--;
		return;
	}
	for (i = 0; i < t->num_iso_packets; i++) {
		struct libusb_iso_packet_descriptor *p = &t->iso_packet_desc[i];
		const u8 *data = libusb_get_iso_packet_buffer_simple(t, i);

		if (p->status != LIBUSB_TRANSFER_COMPLETED) {
			n_iso_errs++;
			frame_add(c, DISCARD_PACKET, NULL, 0);
			continue;
		}
		if (p->actual_length == 0)
			continue;
		n_pkts++;
		if (dump_pkts > 0) {
			unsigned int j, n = p->actual_length;

			dump_pkts--;
			fprintf(stderr, "pkt len %4u:", n);
			for (j = 0; j < n && j < 20; j++)
				fprintf(stderr, " %02x", data[j]);
			if (n > 24) {
				fprintf(stderr, " ...");
				for (j = n - 4; j < n; j++)
					fprintf(stderr, " %02x", data[j]);
			}
			fprintf(stderr, "\n");
		}
		c->drv->pkt_scan(c, data, p->actual_length);
	}
	if (stopping || libusb_submit_transfer(t) < 0)
		active_xfers--;
}

static void usage(void)
{
	fprintf(stderr,
		"usage: vintagecam [--width 320|640] [--freq 0|50|60] [--frames N] [--seconds S]\n"
		"                  [--alt N] [--dump-packets N] [-v]\n"
		"                  [--reg HEXIDX=HEXVAL] [--i2c HEXREG=HEXVAL] > out\n"
		"       vintagecam --list\n");
	exit(2);
}

int main(int argc, char **argv)
{
	struct cam cam = { .width = 640, .light_freq = 0 };
	struct cam *c = &cam;
	struct libusb_transfer *xfers[N_XFERS] = {0};
	struct libusb_config_descriptor *cfg = NULL;
	libusb_context *ctx;
	const struct libusb_interface *intf;
	int alt = -1, maxpkt = 0, list = 0, r, i, rc = 1;
	unsigned d;
	double t_stat, t_poll, t0;
	long last_frames = 0;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--width") && i + 1 < argc)
			c->width = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--freq") && i + 1 < argc)
			c->light_freq = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--frames") && i + 1 < argc)
			max_frames = atol(argv[++i]);
		else if (!strcmp(argv[i], "--seconds") && i + 1 < argc)
			max_seconds = atof(argv[++i]);
		else if (!strcmp(argv[i], "--alt") && i + 1 < argc)
			alt = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--dump-packets") && i + 1 < argc)
			dump_pkts = atoi(argv[++i]);
		else if ((!strcmp(argv[i], "--reg") || !strcmp(argv[i], "--i2c")) &&
			 i + 1 < argc && c->n_pokes < 32) {
			c->pokes[c->n_pokes].kind = argv[i][2] == 'r' ? 'r' : 'i';
			if (sscanf(argv[++i], "%x=%x", &c->pokes[c->n_pokes].idx,
				   &c->pokes[c->n_pokes].val) != 2)
				usage();
			c->n_pokes++;
		} else if (!strcmp(argv[i], "--list"))
			list = 1;
		else if (!strcmp(argv[i], "-v"))
			c->verbose = 1;
		else
			usage();
	}

	r = libusb_init(&ctx);
	if (r < 0) {
		fprintf(stderr, "libusb_init: %s\n", libusb_error_name(r));
		return 1;
	}

	if (list) {
		for (d = 0; d < sizeof drivers / sizeof drivers[0]; d++) {
			libusb_device_handle *h = libusb_open_device_with_vid_pid(
				ctx, drivers[d]->vid, drivers[d]->pid);

			printf("%04x:%04x %-32s %s\n", drivers[d]->vid,
			       drivers[d]->pid, drivers[d]->name,
			       h ? "connected" : "not connected");
			if (h)
				libusb_close(h);
		}
		libusb_exit(ctx);
		return 0;
	}

	if (isatty(STDOUT_FILENO)) {
		fprintf(stderr, "stdout is a terminal, pipe it somewhere (see make view)\n");
		return 2;
	}

	for (d = 0; d < sizeof drivers / sizeof drivers[0] && !c->h; d++) {
		c->h = libusb_open_device_with_vid_pid(ctx, drivers[d]->vid,
						       drivers[d]->pid);
		if (c->h)
			c->drv = drivers[d];
	}
	if (!c->h) {
		fprintf(stderr, "no supported camera found (try --list)\n");
		goto out_ctx;
	}
	fprintf(stderr, "camera: %s (%04x:%04x)\n", c->drv->name, c->drv->vid,
		c->drv->pid);

	/* macos leaves vendor-class devices unconfigured */
	r = libusb_get_configuration(c->h, &i);
	if (r < 0 || i != 1) {
		r = libusb_set_configuration(c->h, 1);
		if (r < 0) {
			fprintf(stderr, "set configuration: %s\n",
				libusb_error_name(r));
			goto out_close;
		}
	}
	r = libusb_claim_interface(c->h, 0);
	if (r < 0) {
		fprintf(stderr, "claim interface: %s\n", libusb_error_name(r));
		goto out_close;
	}

	r = libusb_get_config_descriptor(libusb_get_device(c->h), 0, &cfg);
	if (r < 0) {
		fprintf(stderr, "config descriptor: %s\n", libusb_error_name(r));
		goto out_release;
	}
	intf = &cfg->interface[0];
	if (alt < 0)
		alt = intf->num_altsetting - 1;	/* biggest packets */
	if (alt < 1 || alt >= intf->num_altsetting) {
		fprintf(stderr, "bad alt %d (1..%d)\n", alt,
			intf->num_altsetting - 1);
		goto out_release;
	}
	for (i = 0; i < intf->altsetting[alt].bNumEndpoints; i++) {
		const struct libusb_endpoint_descriptor *ep =
			&intf->altsetting[alt].endpoint[i];

		if (ep->bEndpointAddress == c->drv->iso_ep)
			maxpkt = ep->wMaxPacketSize & 0x7ff;
	}
	if (maxpkt <= 0) {
		fprintf(stderr, "iso endpoint %02x not found in alt %d\n",
			c->drv->iso_ep, alt);
		goto out_release;
	}

	c->frame_cap = FRAME_CAP;
	c->frame = malloc(c->frame_cap);
	c->last_pkt = DISCARD_PACKET;
	c->on_frame = write_frame;
	if (!c->frame)
		goto out_release;

	if (c->drv->init(c) < 0) {
		fprintf(stderr, "driver init failed\n");
		goto out_release;
	}

	r = libusb_set_interface_alt_setting(c->h, 0, alt);
	if (r < 0) {
		fprintf(stderr, "set alt %d: %s\n", alt, libusb_error_name(r));
		goto out_release;
	}
	if (c->drv->start(c) < 0) {
		fprintf(stderr, "driver start failed\n");
		goto out_alt0;
	}
	fprintf(stderr, "streaming %dx%d %s, alt %d (%d bytes/packet)\n",
		c->width, c->height, c->drv->out_format, alt, maxpkt);

	signal(SIGINT, on_signal);
	signal(SIGTERM, on_signal);
	signal(SIGPIPE, SIG_IGN);

	for (i = 0; i < N_XFERS; i++) {
		u8 *buf = malloc((size_t)maxpkt * PKTS_PER_XFER);

		xfers[i] = libusb_alloc_transfer(PKTS_PER_XFER);
		if (!buf || !xfers[i])
			goto out_cancel;
		libusb_fill_iso_transfer(xfers[i], c->h, c->drv->iso_ep, buf,
					 maxpkt * PKTS_PER_XFER, PKTS_PER_XFER,
					 iso_cb, c, 1000);
		libusb_set_iso_packet_lengths(xfers[i], maxpkt);
		r = libusb_submit_transfer(xfers[i]);
		if (r < 0) {
			fprintf(stderr, "iso submit: %s\n", libusb_error_name(r));
			goto out_cancel;
		}
		active_xfers++;
	}

	t0 = t_stat = t_poll = now();
	while (!stopping && active_xfers > 0) {
		struct timeval tv = { 0, 50000 };
		double t;

		libusb_handle_events_timeout(ctx, &tv);
		t = now();
		if (max_seconds > 0 && t - t0 >= max_seconds)
			stopping = 1;
		if (t - t_poll >= 0.1 && c->drv->poll && !stopping) {
			t_poll = t;
			c->drv->poll(c);
		}
		if (c->verbose && t - t_stat >= 1.0) {
			fprintf(stderr,
				"%.1f fps, %ld frames, %ld discarded, %ld iso errs, %ld pkts\n",
				(n_frames - last_frames) / (t - t_stat),
				n_frames, n_discards, n_iso_errs, n_pkts);
			last_frames = n_frames;
			t_stat = t;
		}
	}
	rc = n_frames > 0 ? 0 : 1;
	fprintf(stderr,
		"%ld frames in %.1fs (%.1f fps), avg %ld bytes, %ld discarded, %ld iso errs\n",
		n_frames, now() - t0, n_frames / (now() - t0),
		n_frames ? n_frame_bytes / n_frames : 0, n_discards, n_iso_errs);

out_cancel:
	stopping = 1;
	for (i = 0; i < N_XFERS; i++)
		if (xfers[i])
			libusb_cancel_transfer(xfers[i]);
	t0 = now();
	while (active_xfers > 0 && now() - t0 < 2.0) {
		struct timeval tv = { 0, 50000 };

		libusb_handle_events_timeout(ctx, &tv);
	}
out_alt0:
	libusb_set_interface_alt_setting(c->h, 0, 0);
	c->drv->stop(c);
out_release:
	if (cfg)
		libusb_free_config_descriptor(cfg);
	libusb_release_interface(c->h, 0);
out_close:
	libusb_close(c->h);
out_ctx:
	libusb_exit(ctx);
	return rc;
}
