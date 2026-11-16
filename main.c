// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * vintagecam: userspace libusb drivers for pre-uvc webcams on macos.
 * one process drives one camera. frames go to any of:
 *   --record DIR   every frame, written by a writer thread (never dropped
 *                  unless the queue cap is hit, which is reported)
 *   --preview      an ffplay window fed the latest frame only (lossy on
 *                  purpose, it can never slow the usb side down)
 *   --stdout       the raw frame stream, for piping
 */
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "cam.h"

#define N_XFERS 4
#define PKTS_PER_XFER 32
#define FRAME_CAP (1 << 20)
#define QUEUE_CAP_BYTES (256u << 20)

static const struct cam_driver *drivers[] = {
	&zc3xx_gc0303_driver,
};
#define N_DRIVERS (sizeof drivers / sizeof drivers[0])

static volatile sig_atomic_t stopping;
static int active_xfers;
static long n_frames, n_frame_bytes, n_iso_errs, n_discards, n_pkts;
static long max_frames = -1;
static double max_seconds;
static int dump_pkts;

/* time of the iso packet being scanned, and of the current frame's first one */
static double cur_pkt_time, frame_start_time;

/* ---- lossless queue -> writer thread (record file and/or stdout) ---- */
struct qframe {
	struct qframe *next;
	long seq;
	double t;
	int len;
	u8 data[];
};
static pthread_mutex_t q_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t q_cv = PTHREAD_COND_INITIALIZER;
static struct qframe *q_head, *q_tail;
static size_t q_bytes, q_peak_bytes;
static long q_dropped;
static int q_done;

static int use_queue, use_stdout;
static int rec_fd = -1;
static FILE *rec_idx;
static char rec_path[1024];
static long rec_written;
static int rec_failed;
static double rec_t_first = -1, rec_t_last;
static double wall_minus_mono;

/* ---- lossy preview: one slot holding the latest frame ---- */
static pthread_mutex_t pv_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t pv_cv = PTHREAD_COND_INITIALIZER;
static u8 *pv_buf;
static int pv_len, pv_fresh, pv_done;
static int pv_fd = -1;
static pid_t pv_pid = -1;
static volatile sig_atomic_t pv_dead;
static long pv_shown;

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

static int write_all(int fd, const u8 *data, int len)
{
	while (len > 0) {
		ssize_t w = write(fd, data, len);

		if (w < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		data += w;
		len -= w;
	}
	return 0;
}

int poke_lookup(struct cam *c, char kind, unsigned idx, int dflt)
{
	int i;

	for (i = 0; i < c->n_pokes; i++)
		if (c->pokes[i].kind == kind && c->pokes[i].idx == idx) {
			fprintf(stderr, "poke %s %04x: %02x -> %02x\n",
				kind == 'r' ? "reg" : "i2c", idx, dflt,
				c->pokes[i].val);
			c->pokes[i].used = 1;
			return c->pokes[i].val;
		}
	return dflt;
}

/* same contract as gspca_frame_add() */
void frame_add(struct cam *c, enum pkt_type type, const u8 *data, int len)
{
	if (type == FIRST_PACKET) {
		c->frame_len = 0;
		frame_start_time = cur_pkt_time;
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

/* runs on the usb thread: must never block on disk or on a reader */
static void on_frame(struct cam *c, const u8 *data, int len)
{
	(void)c;
	if (use_queue) {
		struct qframe *f = malloc(sizeof *f + len);

		pthread_mutex_lock(&q_mu);
		if (!f || q_bytes + len > QUEUE_CAP_BYTES) {
			q_dropped++;
			free(f);
		} else {
			f->next = NULL;
			f->seq = n_frames;
			f->t = frame_start_time;
			f->len = len;
			memcpy(f->data, data, len);
			if (q_tail)
				q_tail->next = f;
			else
				q_head = f;
			q_tail = f;
			q_bytes += len;
			if (q_bytes > q_peak_bytes)
				q_peak_bytes = q_bytes;
			pthread_cond_signal(&q_cv);
		}
		pthread_mutex_unlock(&q_mu);
	}
	if (pv_fd >= 0 && !pv_dead && len <= FRAME_CAP) {
		pthread_mutex_lock(&pv_mu);
		memcpy(pv_buf, data, len);
		pv_len = len;
		pv_fresh = 1;
		pthread_cond_signal(&pv_cv);
		pthread_mutex_unlock(&pv_mu);
	}
	n_frames++;
	n_frame_bytes += len;
	if (max_frames > 0 && n_frames >= max_frames)
		stopping = 1;
}

static void *writer_main(void *arg)
{
	long long offset = 0;

	(void)arg;
	for (;;) {
		struct qframe *f;

		pthread_mutex_lock(&q_mu);
		while (!q_head && !q_done)
			pthread_cond_wait(&q_cv, &q_mu);
		f = q_head;
		if (f) {
			q_head = f->next;
			if (!q_head)
				q_tail = NULL;
			q_bytes -= f->len;
		}
		pthread_mutex_unlock(&q_mu);
		if (!f)
			break;

		if (rec_fd >= 0 && !rec_failed) {
			if (write_all(rec_fd, f->data, f->len) < 0) {
				fprintf(stderr, "RECORD WRITE FAILED: %s\n",
					strerror(errno));
				rec_failed = 1;
				stopping = 1;
			} else {
				if (rec_t_first < 0)
					rec_t_first = f->t;
				rec_t_last = f->t;
				fprintf(rec_idx, "%ld,%.6f,%.6f,%lld,%d\n",
					f->seq, f->t - rec_t_first,
					f->t + wall_minus_mono, offset, f->len);
				offset += f->len;
				rec_written++;
				fflush(rec_idx);	/* a hard kill loses no rows */
			}
		}
		if (use_stdout && write_all(STDOUT_FILENO, f->data, f->len) < 0) {
			use_stdout = 0;		/* reader went away */
			if (rec_fd < 0)
				stopping = 1;
		}
		free(f);
	}
	if (rec_idx)
		fflush(rec_idx);
	return NULL;
}

static void *preview_main(void *arg)
{
	u8 *buf = malloc(FRAME_CAP);

	(void)arg;
	while (buf) {
		int len;

		pthread_mutex_lock(&pv_mu);
		while (!pv_fresh && !pv_done)
			pthread_cond_wait(&pv_cv, &pv_mu);
		if (pv_done) {
			pthread_mutex_unlock(&pv_mu);
			break;
		}
		len = pv_len;
		memcpy(buf, pv_buf, len);
		pv_fresh = 0;
		pthread_mutex_unlock(&pv_mu);

		if (write_all(pv_fd, buf, len) < 0) {
			/* window closed. recording carries on without it */
			pv_dead = 1;
			fprintf(stderr, "preview closed%s\n",
				use_queue ? ", still capturing" : "");
			if (!use_queue)
				stopping = 1;
			break;
		}
		pv_shown++;
	}
	free(buf);
	return NULL;
}

static int start_preview(const char *title)
{
	int fds[2];

	if (pipe(fds) < 0)
		return -1;
	pv_pid = fork();
	if (pv_pid < 0)
		return -1;
	if (pv_pid == 0) {
		dup2(fds[0], STDIN_FILENO);
		close(fds[0]);
		close(fds[1]);
		/* own process group: ctrl-c reaches us first, we close the
		 * pipe and ffplay exits on eof */
		setpgid(0, 0);
		execlp("ffplay", "ffplay", "-loglevel", "error", "-f", "mjpeg",
		       "-fflags", "nobuffer", "-flags", "low_delay",
		       "-autoexit", "-window_title", title, "-i", "-",
		       (char *)NULL);
		perror("exec ffplay");
		_exit(127);
	}
	close(fds[0]);
	pv_fd = fds[1];
	pv_buf = malloc(FRAME_CAP);
	return pv_buf ? 0 : -1;
}

static int start_record(const char *dir, const char *name, const char *usbpath)
{
	char stamp[32], idx_path[1040];
	time_t t = time(NULL);
	struct tm tm;

	if (mkdir(dir, 0755) < 0 && errno != EEXIST) {
		fprintf(stderr, "mkdir %s: %s\n", dir, strerror(errno));
		return -1;
	}
	localtime_r(&t, &tm);
	strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", &tm);
	snprintf(rec_path, sizeof rec_path, "%s/%s-%s-usb%s.mjpeg", dir, stamp,
		 name, usbpath);
	snprintf(idx_path, sizeof idx_path, "%s.csv", rec_path);
	/* O_EXCL: never write over an existing recording */
	rec_fd = open(rec_path, O_WRONLY | O_CREAT | O_EXCL, 0644);
	if (rec_fd < 0) {
		fprintf(stderr, "open %s: %s\n", rec_path, strerror(errno));
		return -1;
	}
	rec_idx = fopen(idx_path, "wx");
	if (!rec_idx) {
		fprintf(stderr, "open %s: %s\n", idx_path, strerror(errno));
		return -1;
	}
	fprintf(rec_idx, "frame,t_seconds,unix_time,offset,bytes\n");
	return 0;
}

static void LIBUSB_CALL iso_cb(struct libusb_transfer *t)
{
	struct cam *c = t->user_data;
	double t_done = now();
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
		/* full speed iso: one packet per 1ms usb frame, so packet i
		 * arrived this long before the transfer completed */
		cur_pkt_time = t_done - (t->num_iso_packets - 1 - i) * 0.001;
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

struct found {
	libusb_device *dev;
	const struct cam_driver *drv;
	char path[40];		/* bus-port.port, stable per physical port */
};

static int find_cams(libusb_context *ctx, struct found *out, int max)
{
	libusb_device **list;
	ssize_t cnt = libusb_get_device_list(ctx, &list);
	int n = 0;
	ssize_t i;
	unsigned d;

	for (i = 0; i < cnt && n < max; i++) {
		struct libusb_device_descriptor dd;
		u8 ports[8];
		int np, k, off;

		if (libusb_get_device_descriptor(list[i], &dd) < 0)
			continue;
		for (d = 0; d < N_DRIVERS; d++)
			if (drivers[d]->vid == dd.idVendor &&
			    drivers[d]->pid == dd.idProduct)
				break;
		if (d == N_DRIVERS)
			continue;
		out[n].dev = libusb_ref_device(list[i]);
		out[n].drv = drivers[d];
		off = snprintf(out[n].path, sizeof out[n].path, "%d",
			       libusb_get_bus_number(list[i]));
		np = libusb_get_port_numbers(list[i], ports, 8);
		for (k = 0; k < np; k++)
			off += snprintf(out[n].path + off,
					sizeof out[n].path - off, "%c%d",
					k ? '.' : '-', ports[k]);
		n++;
	}
	if (cnt >= 0)
		libusb_free_device_list(list, 1);
	return n;
}

static void usage(void)
{
	fprintf(stderr,
		"usage: vintagecam [--record DIR] [--preview] [--stdout]\n"
		"                  [--device USBPATH] [--width 640|320] [--freq 0|50|60]\n"
		"                  [--frames N] [--seconds S] [--alt N] [-v]\n"
		"                  [--dump-packets N] [--reg HEX=HEX] [--i2c HEX=HEX]\n"
		"       vintagecam --list\n");
	exit(2);
}

int main(int argc, char **argv)
{
	struct cam cam = { .width = 640, .light_freq = 0 };
	struct cam *c = &cam;
	struct libusb_transfer *xfers[N_XFERS] = {0};
	struct libusb_config_descriptor *cfg = NULL;
	struct found cams[16];
	struct timeval wall;
	libusb_context *ctx;
	const struct libusb_interface *intf;
	const char *rec_dir = NULL, *want_dev = NULL, *usbpath = "";
	pthread_t writer_th, preview_th;
	int have_writer = 0, have_preview_th = 0, want_preview = 0;
	int alt = -1, maxpkt = 0, list = 0, r, i, n_cams, rc = 1;
	double t_stat, t_poll, t0 = 0, t_end = 0;
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
		else if (!strcmp(argv[i], "--record") && i + 1 < argc)
			rec_dir = argv[++i];
		else if (!strcmp(argv[i], "--device") && i + 1 < argc)
			want_dev = argv[++i];
		else if (!strcmp(argv[i], "--preview"))
			want_preview = 1;
		else if (!strcmp(argv[i], "--stdout"))
			use_stdout = 1;
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
	n_cams = find_cams(ctx, cams, 16);

	if (list) {
		for (i = 0; i < n_cams; i++)
			printf("%-10s %04x:%04x %s\n", cams[i].path,
			       cams[i].drv->vid, cams[i].drv->pid,
			       cams[i].drv->name);
		if (!n_cams)
			printf("no supported camera connected\n");
		rc = 0;
		goto out_ctx;
	}

	if (!rec_dir && !want_preview && !use_stdout) {
		fprintf(stderr, "nothing to do with the frames: pass --preview, "
			"--record DIR and/or --stdout\n");
		rc = 2;
		goto out_ctx;
	}
	if (use_stdout && isatty(STDOUT_FILENO)) {
		fprintf(stderr, "--stdout needs a pipe or a file, not a terminal\n");
		rc = 2;
		goto out_ctx;
	}

	for (i = 0; i < n_cams; i++)
		if (!want_dev || !strcmp(want_dev, cams[i].path))
			break;
	if (i == n_cams) {
		if (want_dev)
			fprintf(stderr, "no supported camera at usb path %s (try --list)\n",
				want_dev);
		else
			fprintf(stderr, "no supported camera found\n");
		goto out_ctx;
	}
	if (!want_dev && n_cams > 1)
		fprintf(stderr, "%d cameras connected, using %s (pick with --device)\n",
			n_cams, cams[i].path);
	c->drv = cams[i].drv;
	usbpath = cams[i].path;
	r = libusb_open(cams[i].dev, &c->h);
	if (r < 0) {
		fprintf(stderr, "open: %s\n", libusb_error_name(r));
		goto out_ctx;
	}
	fprintf(stderr, "camera: %s (%04x:%04x) at usb %s\n", c->drv->name,
		c->drv->vid, c->drv->pid, usbpath);

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
	c->on_frame = on_frame;
	if (!c->frame)
		goto out_release;

	if (c->drv->init(c) < 0) {
		fprintf(stderr, "driver init failed\n");
		goto out_release;
	}

	signal(SIGINT, on_signal);
	signal(SIGTERM, on_signal);
	signal(SIGPIPE, SIG_IGN);

	gettimeofday(&wall, NULL);
	wall_minus_mono = wall.tv_sec + wall.tv_usec / 1e6 - now();

	if (rec_dir && start_record(rec_dir, c->drv->name, usbpath) < 0)
		goto out_release;
	use_queue = rec_dir || use_stdout;
	if (use_queue) {
		if (pthread_create(&writer_th, NULL, writer_main, NULL))
			goto out_release;
		have_writer = 1;
	}
	if (want_preview) {
		char title[128];

		snprintf(title, sizeof title, "%s usb %s", c->drv->name, usbpath);
		if (start_preview(title) < 0 ||
		    pthread_create(&preview_th, NULL, preview_main, NULL)) {
			fprintf(stderr, "could not start preview\n");
			goto out_threads;
		}
		have_preview_th = 1;
	}

	r = libusb_set_interface_alt_setting(c->h, 0, alt);
	if (r < 0) {
		fprintf(stderr, "set alt %d: %s\n", alt, libusb_error_name(r));
		goto out_threads;
	}
	if (c->drv->start(c) < 0) {
		fprintf(stderr, "driver start failed\n");
		goto out_alt0;
	}
	fprintf(stderr, "streaming %dx%d %s, alt %d (%d bytes/packet)\n",
		c->width, c->height, c->drv->out_format, alt, maxpkt);
	if (rec_dir)
		fprintf(stderr, "recording to %s\n", rec_path);

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
				"%.1f fps, %ld frames, %ld discarded, %ld iso errs, queue %zu KB, %ld queue drops\n",
				(n_frames - last_frames) / (t - t_stat),
				n_frames, n_discards, n_iso_errs,
				q_bytes >> 10, q_dropped);
			last_frames = n_frames;
			t_stat = t;
		}
	}
	rc = n_frames > 0 ? 0 : 1;

out_cancel:
	stopping = 1;
	t_end = now();
	for (i = 0; i < N_XFERS; i++)
		if (xfers[i])
			libusb_cancel_transfer(xfers[i]);
	while (active_xfers > 0 && now() - t_end < 2.0) {
		struct timeval tv = { 0, 50000 };

		libusb_handle_events_timeout(ctx, &tv);
	}
out_alt0:
	libusb_set_interface_alt_setting(c->h, 0, 0);
	c->drv->stop(c);
out_threads:
	if (have_writer) {
		pthread_mutex_lock(&q_mu);
		q_done = 1;
		pthread_cond_signal(&q_cv);
		pthread_mutex_unlock(&q_mu);
		pthread_join(writer_th, NULL);
	}
	if (have_preview_th) {
		pthread_mutex_lock(&pv_mu);
		pv_done = 1;
		pthread_cond_signal(&pv_cv);
		pthread_mutex_unlock(&pv_mu);
		pthread_join(preview_th, NULL);
	}
	if (pv_fd >= 0)
		close(pv_fd);
	if (pv_pid > 0)
		waitpid(pv_pid, NULL, 0);
	if (rec_fd >= 0) {
		if (fsync(rec_fd) < 0 || close(rec_fd) < 0)
			rec_failed = 1;
		if (rec_idx && fclose(rec_idx) != 0)
			rec_failed = 1;
	}

	if (t0 > 0) {
		fprintf(stderr,
			"%ld frames in %.1fs (%.1f fps), avg %ld bytes, %ld discarded, %ld iso errs\n",
			n_frames, t_end - t0, n_frames / (t_end - t0),
			n_frames ? n_frame_bytes / n_frames : 0, n_discards,
			n_iso_errs);
		if (want_preview)
			fprintf(stderr, "preview: %ld frames shown\n", pv_shown);
	}
	if (rec_dir && rec_path[0]) {
		fprintf(stderr,
			"recorded %ld of %ld frames to %s (queue drops %ld, queue peak %zu KB)%s\n",
			rec_written, n_frames, rec_path, q_dropped,
			q_peak_bytes >> 10, rec_failed ? " WRITE ERRORS" : "");
		if (rec_written > 1)
			fprintf(stderr,
				"to mkv: ffmpeg -f mjpeg -framerate %.3f -i %s -c copy %s.mkv\n",
				(rec_written - 1) / (rec_t_last - rec_t_first),
				rec_path, rec_path);
		if (rec_failed || q_dropped || rec_written != n_frames)
			rc = 1;
	}
out_release:
	if (cfg)
		libusb_free_config_descriptor(cfg);
	libusb_release_interface(c->h, 0);
out_close:
	libusb_close(c->h);
out_ctx:
	for (i = 0; i < n_cams; i++)
		libusb_unref_device(cams[i].dev);
	libusb_exit(ctx);
	return rc;
}
