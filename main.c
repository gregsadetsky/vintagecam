// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * vintagecam: userspace libusb drivers for pre-uvc webcams on macos.
 * one process drives one camera. frames go to any of:
 *   --record DIR   every good frame, written by the sink thread
 *   --preview      an ffplay window fed the latest good frame only (lossy on
 *                  purpose, it can never slow the usb side down)
 *   --stdout       the raw frame stream, for piping
 *
 * threads:
 *   usb (main)  iso packets -> frames -> queue. never blocks on disk.
 *   sink        decodes every frame to check it, drops bad ones, writes
 *               good ones, keeps the health numbers the watchdog reads.
 *   preview     blocking writes to the ffplay pipe.
 *
 * the main loop is a watchdog: no good frame for a while, a run of bad
 * frames, usb errors or the device vanishing all lead to a restart, first of
 * the stream, then of the whole device, then waiting for it to come back.
 * the recording file stays open through all of that.
 */
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <turbojpeg.h>
#include "cam.h"

#define N_XFERS 4
#define PKTS_PER_XFER 32
#define FRAME_CAP (1 << 20)
#define QUEUE_CAP_BYTES (256u << 20)

#define STALL_SECONDS 2.0	/* no good frame for this long -> restart */
#define START_GRACE 1.0		/* extra allowance right after a (re)start */
#define BAD_RUN_LIMIT 20	/* this many bad frames in a row -> restart */
#define BAD_SAVE_LIMIT 200	/* bad frames kept for a post mortem */

static const struct cam_driver *drivers[] = {
	&zc3xx_gc0303_driver,
	&stv06xx_pb0100_driver,
	&zc3xx_pas106_driver,
	&xirlink_cit_model2_driver,
	&stv06xx_vv6410_driver,
	&ov519_ov7648_driver,
};
#define N_DRIVERS (sizeof drivers / sizeof drivers[0])

enum fault { FAULT_NONE, FAULT_STALL, FAULT_CORRUPT, FAULT_CTRL, FAULT_SUBMIT,
	     FAULT_GONE, FAULT_START };
static const char *fault_name[] = { "none", "no good frames", "bad frame run",
	"control transfer error", "iso submit error", "device gone",
	"start failed" };

static volatile sig_atomic_t stopping;
static libusb_context *ctx;
static struct libusb_transfer *xfers[N_XFERS];
/* per transfer state. a transfer that did not come back in time is marked
 * abandoned and frees itself whenever it does return. counting alone was not
 * enough: late returns made the count wrong, the next drain freed transfers
 * still in flight, and the recorder crashed at exit (seen after a real stall). */
struct xctx {
	struct cam *c;
	int in_flight, abandoned;
};
static struct xctx *xctxs[N_XFERS];
static int abandoned_out;
static int active_xfers, stream_halt, stream_gen;
static enum fault fault;
static int cur_alt = -1, cur_maxpkt;
static long n_frames, n_frame_bytes, n_iso_errs, n_xfer_errs, n_discards;
static long n_restarts, n_reopens;
static long max_frames = -1;
static double max_seconds, t_run0;
static int dump_pkts;

/* time of the iso packet being scanned, and of the current frame's first one */
static double cur_pkt_time, frame_start_time;

/* ---- queue: usb thread -> sink thread ---- */
struct qframe {
	struct qframe *next;
	double t;
	int gen, cam_seq, len;
	u8 data[];
};
static pthread_mutex_t q_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t q_cv = PTHREAD_COND_INITIALIZER;
static struct qframe *q_head, *q_tail;
static size_t q_bytes, q_peak_bytes;
static long q_dropped;
static int q_done;

/* ---- health, written by the sink thread, read by the watchdog ---- */
static pthread_mutex_t h_mu = PTHREAD_MUTEX_INITIALIZER;
static double h_last_good;
static int h_bad_run;
static long h_good, h_bad, h_cam_missed;

/* ---- sinks ---- */
static int use_stdout;
static int rec_fd = -1, bad_fd = -1;
static FILE *rec_idx, *ev_file;
static char rec_path[1024];
static long rec_written, bad_saved;
static int rec_failed;
static double rec_t_first = -1, rec_t_last;
static double wall_minus_mono;
static int want_w, want_h;
static enum bayer want_bayer;
static pthread_mutex_t ev_mu = PTHREAD_MUTEX_INITIALIZER;

/* ---- lossy preview: one slot holding the latest frame ---- */
static pthread_mutex_t pv_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t pv_cv = PTHREAD_COND_INITIALIZER;
static u8 *pv_buf;
static int pv_len, pv_fresh, pv_done;
static int pv_fd = -1;
static pid_t pv_pid = -1;
static volatile sig_atomic_t pv_dead;
static long pv_shown;

/* ---- test hooks (--test-faults) ---- */
enum { TF_STALL, TF_CORRUPT, TF_RESET, TF_BREAK, TF_ABANDON };
static int test_abandon;
static struct { double at; int kind, done; } tfaults[16];
static int n_tfaults;
static int sim_stall;
static double sim_corrupt_until;

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

/* one line to stderr and to the recording's event log */
static void event(const char *fmt, ...)
{
	char msg[512];
	va_list ap;
	double t = now();

	va_start(ap, fmt);
	vsnprintf(msg, sizeof msg, fmt, ap);
	va_end(ap);
	pthread_mutex_lock(&ev_mu);
	fprintf(stderr, "[%8.2fs] %s\n", t_run0 > 0 ? t - t_run0 : 0.0, msg);
	if (ev_file) {
		fprintf(ev_file, "%.3f,%.3f,%s\n", t + wall_minus_mono,
			rec_t_first >= 0 ? t - rec_t_first : 0.0, msg);
		fflush(ev_file);
	}
	pthread_mutex_unlock(&ev_mu);
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
			c->last_pkt = DISCARD_PACKET;
			n_discards++;
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
	struct qframe *f = malloc(sizeof *f + len);

	pthread_mutex_lock(&q_mu);
	if (!f || q_bytes + len > QUEUE_CAP_BYTES) {
		q_dropped++;
		free(f);
	} else {
		f->next = NULL;
		f->t = frame_start_time;
		f->gen = stream_gen;
		f->cam_seq = c->frame_seq;
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
	n_frames++;
	n_frame_bytes += len;
}

/* decode the whole frame. anything short of a clean decode at the expected
 * size is a bad frame (turbojpeg reports libjpeg warnings such as
 * "premature end of data" or "extraneous bytes" as failures here). */
static const char *frame_problem(tjhandle tj, u8 *scratch, const u8 *d, int len)
{
	int w, h, sub, cs;

	if (len < 4 || d[0] != 0xff || d[1] != 0xd8)
		return "no jpeg start";
	if (d[len - 2] != 0xff || d[len - 1] != 0xd9)
		return "no jpeg end";
	if (tjDecompressHeader3(tj, d, len, &w, &h, &sub, &cs))
		return tjGetErrorStr2(tj);
	if (w != want_w || h != want_h)
		return "wrong size";
	if (tjDecompress2(tj, d, len, scratch, w, 0, h, TJPF_GRAY, TJFLAG_FASTDCT))
		return tjGetErrorStr2(tj);
	return NULL;
}

/* bilinear demosaic of 8 bit bayer to rgb. a missing colour is the mean of
 * the sites of that colour among the 8 neighbours. */
static void bayer_to_rgb(const u8 *in, u8 *out, int w, int h, enum bayer pat)
{
	/* colour (0 r, 1 g, 2 b) at [row parity * 2 + column parity] */
	static const u8 sites[5][4] = {
		{ 1, 1, 1, 1 },
		{ 1, 0, 2, 1 },		/* GRBG */
		{ 0, 1, 1, 2 },		/* RGGB */
		{ 2, 1, 1, 0 },		/* BGGR */
		{ 1, 2, 0, 1 },		/* GBRG */
	};
	const u8 *site = sites[pat];
	int x, y, dx, dy;

	for (y = 0; y < h; y++) {
		for (x = 0; x < w; x++) {
			int sum[3] = { 0, 0, 0 }, cnt[3] = { 0, 0, 0 };
			int own = site[(y & 1) * 2 + (x & 1)], ch;

			for (dy = -1; dy <= 1; dy++) {
				for (dx = -1; dx <= 1; dx++) {
					int xx = x + dx, yy = y + dy;

					if (xx < 0 || yy < 0 || xx >= w || yy >= h)
						continue;
					ch = site[(yy & 1) * 2 + (xx & 1)];
					sum[ch] += in[yy * w + xx];
					cnt[ch]++;
				}
			}
			for (ch = 0; ch < 3; ch++)
				out[ch] = ch == own ? in[y * w + x] :
					  cnt[ch] ? sum[ch] / cnt[ch] : 0;
			out += 3;
		}
	}
}

static void preview_push(const u8 *data, int len)
{
	if (pv_fd < 0 || pv_dead || len > FRAME_CAP)
		return;
	pthread_mutex_lock(&pv_mu);
	memcpy(pv_buf, data, len);
	pv_len = len;
	pv_fresh = 1;
	pthread_cond_signal(&pv_cv);
	pthread_mutex_unlock(&pv_mu);
}

static void *sink_main(void *arg)
{
	tjhandle tj = tjInitDecompress(), tjc = tjInitCompress();
	u8 *scratch = malloc((size_t)want_w * want_h);
	u8 *rgb = malloc((size_t)want_w * want_h * 3);
	unsigned long jpg_cap = tjBufSize(want_w, want_h, TJSAMP_444);
	u8 *jpg = malloc(jpg_cap);
	long long offset = 0;
	int gen = -1, prev_seq = -1;

	(void)arg;
	for (;;) {
		struct qframe *f;
		const char *why;
		const u8 *out;
		int out_len;

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

		out = f->data;
		out_len = f->len;
		if (!tj || !tjc || !scratch || !rgb || !jpg) {
			why = "no decoder";
		} else if (want_bayer) {
			/* a raw frame has nothing to check but its size */
			unsigned long n = jpg_cap;
			u8 *dst = jpg;

			if (f->len != want_w * want_h) {
				why = "wrong raw frame size";
			} else {
				bayer_to_rgb(f->data, rgb, want_w, want_h, want_bayer);
				if (tjCompress2(tjc, rgb, want_w, 0, want_h, TJPF_RGB,
						&dst, &n, TJSAMP_444, 95,
						TJFLAG_NOREALLOC)) {
					why = tjGetErrorStr2(tjc);
				} else {
					why = NULL;
					out = jpg;
					out_len = (int)n;
				}
			}
		} else {
			why = frame_problem(tj, scratch, f->data, f->len);
		}

		pthread_mutex_lock(&h_mu);
		if (f->gen != gen) {		/* stream was restarted */
			gen = f->gen;
			prev_seq = -1;
		}
		/* the counter was seen wrapping from 65495 to 0 with no frame
		 * lost, so a backwards step is a wrap, not a gap */
		if (f->cam_seq > prev_seq && prev_seq >= 0) {
			int gap = f->cam_seq - prev_seq - 1;

			if (gap > 0 && gap < 1000)
				h_cam_missed += gap;
		}
		prev_seq = f->cam_seq;
		if (why) {
			h_bad++;
			h_bad_run++;
		} else {
			h_good++;
			h_bad_run = 0;
			h_last_good = now();
		}
		pthread_mutex_unlock(&h_mu);

		if (why) {
			if (h_bad <= 5 || h_bad % 100 == 0)
				event("bad frame (%ld so far): %s, %d bytes",
				      h_bad, why, f->len);
			if (bad_fd >= 0 && bad_saved < BAD_SAVE_LIMIT) {
				write_all(bad_fd, f->data, f->len);
				bad_saved++;
			}
			free(f);
			continue;
		}

		if (rec_fd >= 0 && !rec_failed) {
			if (write_all(rec_fd, out, out_len) < 0) {
				rec_failed = 1;
				event("RECORD WRITE FAILED: %s", strerror(errno));
				stopping = 1;
			} else {
				if (rec_t_first < 0)
					rec_t_first = f->t;
				rec_t_last = f->t;
				fprintf(rec_idx, "%ld,%.6f,%.6f,%lld,%d,%d\n",
					rec_written, f->t - rec_t_first,
					f->t + wall_minus_mono, offset, out_len,
					f->cam_seq);
				fflush(rec_idx);	/* a hard kill loses no rows */
				offset += out_len;
				rec_written++;
			}
		}
		if (use_stdout && write_all(STDOUT_FILENO, out, out_len) < 0) {
			use_stdout = 0;		/* reader went away */
			if (rec_fd < 0)
				stopping = 1;
		}
		preview_push(out, out_len);
		if (max_frames > 0 && h_good >= max_frames)
			stopping = 1;
		free(f);
	}
	free(scratch);
	free(rgb);
	free(jpg);
	if (tj)
		tjDestroy(tj);
	if (tjc)
		tjDestroy(tjc);
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
			event("preview closed%s",
			      rec_fd >= 0 || use_stdout ? ", still capturing" : "");
			if (rec_fd < 0 && !use_stdout)
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
	char stamp[32], p[1100];
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
	/* O_EXCL / "wx": never write over an existing recording */
	rec_fd = open(rec_path, O_WRONLY | O_CREAT | O_EXCL, 0644);
	if (rec_fd < 0) {
		fprintf(stderr, "open %s: %s\n", rec_path, strerror(errno));
		return -1;
	}
	snprintf(p, sizeof p, "%s.csv", rec_path);
	rec_idx = fopen(p, "wx");
	snprintf(p, sizeof p, "%s.events.csv", rec_path);
	ev_file = fopen(p, "wx");
	snprintf(p, sizeof p, "%s.bad", rec_path);
	bad_fd = open(p, O_WRONLY | O_CREAT | O_EXCL, 0644);
	if (!rec_idx || !ev_file || bad_fd < 0) {
		fprintf(stderr, "cannot create files next to %s: %s\n", rec_path,
			strerror(errno));
		return -1;
	}
	fprintf(rec_idx, "frame,t_seconds,unix_time,offset,bytes,cam_seq\n");
	fprintf(ev_file, "unix_time,t_seconds,event\n");
	return 0;
}

static void LIBUSB_CALL iso_cb(struct libusb_transfer *t)
{
	struct xctx *x = t->user_data;
	struct cam *c = x->c;
	double t_done = now();
	int i;

	if (x->abandoned) {
		libusb_free_transfer(t);	/* frees its buffer too */
		free(x);
		abandoned_out--;
		return;
	}
	if (t->status == LIBUSB_TRANSFER_CANCELLED ||
	    t->status == LIBUSB_TRANSFER_NO_DEVICE) {
		if (t->status == LIBUSB_TRANSFER_NO_DEVICE && !fault)
			fault = FAULT_GONE;
		x->in_flight = 0;
		active_xfers--;
		return;
	}
	if (t->status != LIBUSB_TRANSFER_COMPLETED) {
		n_xfer_errs++;
		frame_add(c, DISCARD_PACKET, NULL, 0);
		goto resubmit;
	}
	for (i = 0; i < t->num_iso_packets; i++) {
		struct libusb_iso_packet_descriptor *p = &t->iso_packet_desc[i];
		u8 *data = libusb_get_iso_packet_buffer_simple(t, i);

		if (p->status != LIBUSB_TRANSFER_COMPLETED) {
			n_iso_errs++;
			frame_add(c, DISCARD_PACKET, NULL, 0);
			continue;
		}
		if (p->actual_length == 0 || sim_stall)
			continue;
		if (t_done < sim_corrupt_until && p->actual_length > 64) {
			data[p->actual_length / 2] ^= 0x5a;
			data[p->actual_length / 3] = 0xff;
			data[p->actual_length / 3 + 1] = 0xc4;
		}
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
resubmit:
	if (stream_halt || stopping) {
		x->in_flight = 0;
		active_xfers--;
	} else if (libusb_submit_transfer(t) < 0) {
		x->in_flight = 0;
		active_xfers--;
		if (!fault)
			fault = FAULT_SUBMIT;
	}
}

struct found {
	libusb_device *dev;
	const struct cam_driver *drv;
	char path[40];		/* bus-port.port, stable per physical port */
};

static int find_cams(struct found *out, int max)
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
			    drivers[d]->pid == dd.idProduct &&
			    (!drivers[d]->bcd || drivers[d]->bcd == dd.bcdDevice))
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

static void free_cams(struct found *cams, int n)
{
	while (n-- > 0)
		libusb_unref_device(cams[n].dev);
}

static void close_camera(struct cam *c)
{
	double t = now();

	if (!c->h)
		return;
	/* let abandoned transfers come home before their device is closed */
	while (abandoned_out > 0 && now() - t < 1.0) {
		struct timeval tv = { 0, 50000 };

		libusb_handle_events_timeout(ctx, &tv);
	}
	libusb_release_interface(c->h, 0);
	libusb_close(c->h);
	c->h = NULL;
	free(c->priv);
	c->priv = NULL;
}

/* open the camera at usb path `path` ("" = first one found, and remember
 * where it was), configure, claim, pick the alt setting, run driver init.
 * quiet = 1 while polling for a camera that went away. in that mode, if
 * nothing is at the old path and exactly one camera of the same model is
 * connected, follow it to its new port. with two or more of the same model
 * there is no telling them apart (no serial numbers), so keep waiting. */
static int open_camera(struct cam *c, char *path, int want_alt, int quiet)
{
	static int twins_warned;
	struct found cams[16];
	struct libusb_config_descriptor *cfg = NULL;
	const struct libusb_interface *intf;
	int n = find_cams(cams, 16), i, r, cfgval, alt, maxpkt = 0;
	char moved_from[40] = "", new_path[40] = "";

	/* when coming back (quiet), a camera of another model sitting on my old
	 * port is not mine: seen for real when two cameras swapped ports */
	for (i = 0; i < n; i++)
		if ((!path[0] || !strcmp(path, cams[i].path)) &&
		    (!quiet || !c->drv || cams[i].drv == c->drv))
			break;
	if (i == n && quiet && c->drv) {
		static int stranger_warned;
		int same = 0, k, at = -1;

		for (k = 0; k < n; k++)
			if (!strcmp(path, cams[k].path) && !stranger_warned) {
				stranger_warned = 1;
				event("a different camera (%s) is on usb %s, leaving it alone",
				      cams[k].drv->name, path);
			}

		for (k = 0; k < n; k++)
			if (cams[k].drv == c->drv) {
				same++;
				at = k;
			}
		if (same == 1) {
			i = at;
			snprintf(moved_from, sizeof moved_from, "%s", path);
		} else if (same > 1 && !twins_warned) {
			twins_warned = 1;
			event("%d cameras of this model connected, none at usb %s: "
			      "not guessing which one is mine", same, path);
		}
	}
	if (i == n) {
		if (!quiet) {
			if (path[0])
				fprintf(stderr, "no supported camera at usb path %s (try --list)\n", path);
			else
				fprintf(stderr, "no supported camera found\n");
		}
		free_cams(cams, n);
		return -1;
	}
	if (!path[0] && n > 1)
		fprintf(stderr, "%d cameras connected, using %s (pick with --device)\n",
			n, cams[i].path);
	c->drv = cams[i].drv;
	r = libusb_open(cams[i].dev, &c->h);
	if (r >= 0 && !moved_from[0])
		snprintf(path, 40, "%s", cams[i].path);
	snprintf(new_path, sizeof new_path, "%s", cams[i].path);
	free_cams(cams, n);
	if (r < 0) {
		if (!quiet)
			fprintf(stderr, "open: %s\n", libusb_error_name(r));
		c->h = NULL;
		return -1;
	}

	/* macos leaves vendor-class devices unconfigured */
	r = libusb_get_configuration(c->h, &cfgval);
	if (r < 0 || cfgval != 1)
		r = libusb_set_configuration(c->h, 1);
	if (r >= 0)
		r = libusb_claim_interface(c->h, 0);
	if (r >= 0)
		r = libusb_get_config_descriptor(libusb_get_device(c->h), 0, &cfg);
	if (r < 0) {
		if (!quiet)
			fprintf(stderr, "configure/claim: %s\n", libusb_error_name(r));
		goto fail;
	}
	intf = &cfg->interface[0];
	alt = want_alt < 0 ? intf->num_altsetting - 1 : want_alt;
	if (alt < 1 || alt >= intf->num_altsetting) {
		fprintf(stderr, "bad alt %d (1..%d)\n", alt, intf->num_altsetting - 1);
		goto fail;
	}
	for (i = 0; i < intf->altsetting[alt].bNumEndpoints; i++) {
		const struct libusb_endpoint_descriptor *ep =
			&intf->altsetting[alt].endpoint[i];

		if (ep->bEndpointAddress == c->drv->iso_ep)
			maxpkt = ep->wMaxPacketSize & 0x7ff;
	}
	libusb_free_config_descriptor(cfg);
	cfg = NULL;
	if (maxpkt <= 0) {
		fprintf(stderr, "iso endpoint %02x not found in alt %d\n",
			c->drv->iso_ep, alt);
		goto fail;
	}
	cur_alt = alt;
	cur_maxpkt = maxpkt;
	c->iso_maxpkt = maxpkt;

	c->usb_err = 0;
	if (c->drv->init(c) < 0) {
		if (!quiet)
			fprintf(stderr, "driver init failed\n");
		goto fail;
	}
	if (moved_from[0]) {
		snprintf(path, 40, "%s", new_path);
		event("camera moved from usb %s to usb %s", moved_from, path);
	}
	twins_warned = 0;
	return 0;
fail:
	if (cfg)
		libusb_free_config_descriptor(cfg);
	close_camera(c);
	return -1;
}

static void drain_xfers(void)
{
	double t = now();
	int i;

	while (active_xfers > 0 && now() - t < (test_abandon ? 0.0 : 2.0)) {
		struct timeval tv = { 0, 50000 };

		libusb_handle_events_timeout(ctx, &tv);
	}
	if (active_xfers > 0) {
		event("%d iso transfers never returned, abandoning them",
		      active_xfers);
		test_abandon = 0;
	}
	for (i = 0; i < N_XFERS; i++) {
		if (xfers[i] && xctxs[i] && xctxs[i]->in_flight) {
			/* never free one in flight: it frees itself on return */
			xctxs[i]->abandoned = 1;
			abandoned_out++;
		} else {
			if (xfers[i])
				libusb_free_transfer(xfers[i]);	/* and its buffer */
			free(xctxs[i]);
		}
		xfers[i] = NULL;
		xctxs[i] = NULL;
	}
	active_xfers = 0;
}

static void stop_stream(struct cam *c, int device_ok)
{
	int i;

	stream_halt = 1;
	for (i = 0; i < N_XFERS; i++)
		if (xfers[i])
			libusb_cancel_transfer(xfers[i]);
	drain_xfers();
	if (c->h && device_ok) {
		libusb_set_interface_alt_setting(c->h, 0, 0);
		c->drv->stop(c);
	}
}

static int start_stream(struct cam *c)
{
	int i, r;

	fault = FAULT_NONE;
	stream_halt = 0;
	sim_stall = 0;
	stream_gen++;
	c->usb_err = 0;
	c->frame_len = 0;
	c->frame_seq = -1;
	c->last_pkt = DISCARD_PACKET;

	r = libusb_set_interface_alt_setting(c->h, 0, cur_alt);
	if (r < 0) {
		event("set alt %d: %s", cur_alt, libusb_error_name(r));
		return -1;
	}
	if (c->drv->start(c) < 0) {
		event("driver start failed");
		return -1;
	}
	for (i = 0; i < N_XFERS; i++) {
		u8 *buf = malloc((size_t)cur_maxpkt * PKTS_PER_XFER);

		xfers[i] = libusb_alloc_transfer(PKTS_PER_XFER);
		xctxs[i] = calloc(1, sizeof *xctxs[i]);
		if (!buf || !xfers[i] || !xctxs[i])
			return -1;
		xctxs[i]->c = c;
		libusb_fill_iso_transfer(xfers[i], c->h, c->drv->iso_ep, buf,
					 cur_maxpkt * PKTS_PER_XFER,
					 PKTS_PER_XFER, iso_cb, xctxs[i], 1000);
		xfers[i]->flags = LIBUSB_TRANSFER_FREE_BUFFER;
		libusb_set_iso_packet_lengths(xfers[i], cur_maxpkt);
		r = libusb_submit_transfer(xfers[i]);
		if (r < 0) {
			event("iso submit: %s", libusb_error_name(r));
			return -1;
		}
		xctxs[i]->in_flight = 1;
		active_xfers++;
	}
	pthread_mutex_lock(&h_mu);
	h_bad_run = 0;
	h_last_good = now() + START_GRACE;
	pthread_mutex_unlock(&h_mu);
	return 0;
}

static void run_test_faults(struct cam *c, double t)
{
	int i, r;

	for (i = 0; i < n_tfaults; i++) {
		if (tfaults[i].done || t - t_run0 < tfaults[i].at)
			continue;
		tfaults[i].done = 1;
		switch (tfaults[i].kind) {
		case TF_STALL:
			event("TEST: dropping all iso data until the next restart");
			sim_stall = 1;
			break;
		case TF_CORRUPT:
			event("TEST: corrupting iso data for 4 seconds");
			sim_corrupt_until = t + 4.0;
			break;
		case TF_RESET:
			r = libusb_reset_device(c->h);
			event("TEST: usb port reset of the camera -> %s",
			      libusb_error_name(r));
			if (r == LIBUSB_ERROR_NOT_FOUND && !fault)
				fault = FAULT_GONE;
			break;
		case TF_ABANDON:
			event("TEST: stall, and do not wait for cancelled transfers");
			sim_stall = 1;
			test_abandon = 1;
			break;
		case TF_BREAK:
			event("TEST: breaking the camera's clock register");
			if (c->drv->test_break)
				c->drv->test_break(c);
			break;
		}
	}
}

static void usage(void)
{
	fprintf(stderr,
		"usage: vintagecam [--record DIR] [--preview] [--stdout]\n"
		"                  [--device USBPATH] [--width N] [--freq 0|50|60]\n"
		"                  [--frames N] [--seconds S] [--alt N] [-v]\n"
		"                  [--dump-packets N] [--reg HEX=HEX] [--i2c HEX=HEX]\n"
		"                  [--bayer grbg|rggb|bggr|gbrg] [--exposure N] [--gain N]\n"
		"                  [--test-faults SEC:stall|corrupt|reset|break|abandon,...]\n"
		"       vintagecam --list\n"
		"       vintagecam --export FILE.mjpeg [--fps N] | ffmpeg ...\n");
	exit(2);
}

int main(int argc, char **argv)
{
	struct cam cam = { .width = 0, .light_freq = 0, .exposure = -1, .gain = -1 };
	enum bayer bayer_override = BAYER_NONE;
	struct cam *c = &cam;
	const char *rec_dir = NULL, *export_path = NULL;
	char dev_path[40] = "";
	pthread_t sink_th, preview_th;
	int have_sink = 0, have_preview_th = 0, want_preview = 0;
	int want_alt = -1, list = 0, r, i, rc = 1, fails = 0;
	double t_stat, t_poll, t_end, export_fps = 30;
	long last_frames = 0, good_at_restart = 0;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--width") && i + 1 < argc)
			c->width = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--freq") && i + 1 < argc)
			c->light_freq = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--exposure") && i + 1 < argc)
			c->exposure = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--gain") && i + 1 < argc)
			c->gain = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--frames") && i + 1 < argc)
			max_frames = atol(argv[++i]);
		else if (!strcmp(argv[i], "--seconds") && i + 1 < argc)
			max_seconds = atof(argv[++i]);
		else if (!strcmp(argv[i], "--alt") && i + 1 < argc)
			want_alt = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--dump-packets") && i + 1 < argc)
			dump_pkts = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--record") && i + 1 < argc)
			rec_dir = argv[++i];
		else if (!strcmp(argv[i], "--device") && i + 1 < argc)
			snprintf(dev_path, sizeof dev_path, "%s", argv[++i]);
		else if (!strcmp(argv[i], "--export") && i + 1 < argc)
			export_path = argv[++i];
		else if (!strcmp(argv[i], "--fps") && i + 1 < argc)
			export_fps = atof(argv[++i]);
		else if (!strcmp(argv[i], "--bayer") && i + 1 < argc) {
			static const char *names[] = { "", "grbg", "rggb", "bggr", "gbrg" };
			int k;

			i++;
			for (k = 1; k < 5; k++)
				if (!strcmp(argv[i], names[k]))
					bayer_override = k;
			if (!bayer_override)
				usage();
		} else if (!strcmp(argv[i], "--preview"))
			want_preview = 1;
		else if (!strcmp(argv[i], "--stdout"))
			use_stdout = 1;
		else if (!strcmp(argv[i], "--test-faults") && i + 1 < argc) {
			char *s = argv[++i], *tok;

			while ((tok = strsep(&s, ",")) && n_tfaults < 16) {
				char kind[16];

				if (sscanf(tok, "%lf:%15s", &tfaults[n_tfaults].at,
					   kind) != 2)
					usage();
				if (!strcmp(kind, "stall"))
					tfaults[n_tfaults].kind = TF_STALL;
				else if (!strcmp(kind, "corrupt"))
					tfaults[n_tfaults].kind = TF_CORRUPT;
				else if (!strcmp(kind, "reset"))
					tfaults[n_tfaults].kind = TF_RESET;
				else if (!strcmp(kind, "break"))
					tfaults[n_tfaults].kind = TF_BREAK;
				else if (!strcmp(kind, "abandon"))
					tfaults[n_tfaults].kind = TF_ABANDON;
				else
					usage();
				n_tfaults++;
			}
		} else if ((!strcmp(argv[i], "--reg") || !strcmp(argv[i], "--i2c")) &&
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

	if (export_path)
		return export_main(export_path, export_fps);

	r = libusb_init(&ctx);
	if (r < 0) {
		fprintf(stderr, "libusb_init: %s\n", libusb_error_name(r));
		return 1;
	}

	if (list) {
		struct found cams[16];
		int n = find_cams(cams, 16);

		for (i = 0; i < n; i++)
			printf("%-10s %04x:%04x %s\n", cams[i].path,
			       cams[i].drv->vid, cams[i].drv->pid,
			       cams[i].drv->name);
		if (!n)
			printf("no supported camera connected\n");
		free_cams(cams, n);
		libusb_exit(ctx);
		return 0;
	}

	if (!rec_dir && !want_preview && !use_stdout) {
		fprintf(stderr, "nothing to do with the frames: pass --preview, "
			"--record DIR and/or --stdout\n");
		libusb_exit(ctx);
		return 2;
	}
	if (use_stdout && isatty(STDOUT_FILENO)) {
		fprintf(stderr, "--stdout needs a pipe or a file, not a terminal\n");
		libusb_exit(ctx);
		return 2;
	}

	c->frame_cap = FRAME_CAP;
	c->frame = malloc(c->frame_cap);
	c->on_frame = on_frame;
	if (!c->frame || open_camera(c, dev_path, want_alt, 0) < 0) {
		libusb_exit(ctx);
		return 1;
	}
	want_w = c->width;
	want_h = c->height;
	want_bayer = c->drv->bayer;
	if (want_bayer && bayer_override)
		want_bayer = bayer_override;
	fprintf(stderr, "camera: %s (%04x:%04x) at usb %s\n", c->drv->name,
		c->drv->vid, c->drv->pid, dev_path);

	signal(SIGINT, on_signal);
	signal(SIGTERM, on_signal);
	signal(SIGPIPE, SIG_IGN);

	{
		struct timeval wall;

		gettimeofday(&wall, NULL);
		wall_minus_mono = wall.tv_sec + wall.tv_usec / 1e6 - now();
	}

	if (rec_dir && start_record(rec_dir, c->drv->name, dev_path) < 0)
		goto out;
	if (pthread_create(&sink_th, NULL, sink_main, NULL))
		goto out;
	have_sink = 1;
	if (want_preview) {
		char title[128];

		snprintf(title, sizeof title, "%s usb %s", c->drv->name, dev_path);
		if (start_preview(title) < 0 ||
		    pthread_create(&preview_th, NULL, preview_main, NULL)) {
			fprintf(stderr, "could not start preview\n");
			goto out;
		}
		have_preview_th = 1;
	}

	t_run0 = t_stat = t_poll = now();
	if (start_stream(c) < 0)
		fault = FAULT_START;
	else
		event("streaming %dx%d %s, alt %d (%d bytes/packet)%s%s", c->width,
		      c->height, c->drv->out_format, cur_alt, cur_maxpkt,
		      rec_dir ? ", recording to " : "", rec_dir ? rec_path : "");

	while (!stopping) {
		struct timeval tv = { 0, 50000 };
		double t, last_good;
		int bad_run, reopen;
		long good;

		/* ---- normal running ---- */
		if (!fault) {
			libusb_handle_events_timeout(ctx, &tv);
			t = now();
			if (max_seconds > 0 && t - t_run0 >= max_seconds)
				stopping = 1;
			if (n_tfaults)
				run_test_faults(c, t);
			if (t - t_poll >= 0.1 && c->drv->poll && !stopping && !fault) {
				t_poll = t;
				c->drv->poll(c);
				if (c->usb_err && !fault)
					fault = FAULT_CTRL;
			}
			pthread_mutex_lock(&h_mu);
			last_good = h_last_good;
			bad_run = h_bad_run;
			pthread_mutex_unlock(&h_mu);
			if (!fault && bad_run >= BAD_RUN_LIMIT)
				fault = FAULT_CORRUPT;
			if (!fault && t - last_good > STALL_SECONDS)
				fault = FAULT_STALL;
			if (c->verbose && t - t_stat >= 1.0) {
				fprintf(stderr,
					"%.1f fps, %ld frames, %ld bad, %ld discarded, %ld iso errs, queue %zu KB, %ld restarts\n",
					(n_frames - last_frames) / (t - t_stat),
					n_frames, h_bad, n_discards, n_iso_errs,
					q_bytes >> 10, n_restarts);
				last_frames = n_frames;
				t_stat = t;
			}
			continue;
		}

		/* ---- recovery ---- */
		pthread_mutex_lock(&h_mu);
		good = h_good;
		pthread_mutex_unlock(&h_mu);
		/* did the last restart produce anything? if not, go harder */
		fails = good > good_at_restart ? 0 : fails + 1;
		good_at_restart = good;
		reopen = fault == FAULT_GONE || fault == FAULT_START || fails >= 2 ||
			 !c->h;
		n_restarts++;
		event("FAULT: %s. recovery #%ld: %s", fault_name[fault], n_restarts,
		      reopen ? "reopening the device" : "restarting the stream");

		stop_stream(c, fault != FAULT_GONE);
		if (reopen) {
			int waited = 0;

			n_reopens++;
			if (c->h && fails >= 3) {
				r = libusb_reset_device(c->h);
				event("usb port reset -> %s", libusb_error_name(r));
			}
			close_camera(c);
			while (!stopping && open_camera(c, dev_path, want_alt, 1) < 0) {
				if (!waited++)
					event("waiting for the camera at usb %s", dev_path);
				msleep(500);
				if (max_seconds > 0 && now() - t_run0 >= max_seconds)
					stopping = 1;
			}
			if (stopping)
				break;
			if (waited)
				event("camera is back");
		}
		if (fails > 1)
			msleep(fails > 10 ? 5000 : fails * 500);	/* don't thrash */
		if (start_stream(c) < 0) {
			stop_stream(c, 0);
			fault = FAULT_START;
		} else {
			event("stream restarted");
		}
	}

	t_end = now();
	stop_stream(c, 1);
	rc = 0;
out:
	if (have_sink) {
		pthread_mutex_lock(&q_mu);
		q_done = 1;
		pthread_cond_signal(&q_cv);
		pthread_mutex_unlock(&q_mu);
		pthread_join(sink_th, NULL);
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
	if (bad_fd >= 0)
		close(bad_fd);

	if (t_run0 > 0 && rc == 0) {
		fprintf(stderr,
			"%ld frames in %.1fs (%.1f fps), avg %ld bytes: %ld good, %ld bad (dropped), "
			"%ld incomplete, %ld iso packet errs, %ld transfer errs\n",
			n_frames, t_end - t_run0, n_frames / (t_end - t_run0),
			n_frames ? n_frame_bytes / n_frames : 0, h_good, h_bad,
			n_discards, n_iso_errs, n_xfer_errs);
		fprintf(stderr,
			"camera frame counter skipped %ld, recoveries %ld (%ld with device reopen), "
			"queue drops %ld, queue peak %zu KB\n",
			h_cam_missed, n_restarts, n_reopens, q_dropped,
			q_peak_bytes >> 10);
		if (want_preview)
			fprintf(stderr, "preview: %ld frames shown\n", pv_shown);
		if (h_good == 0)
			rc = 1;
	}
	if (rec_dir && rec_path[0]) {
		fprintf(stderr, "recorded %ld of %ld good frames to %s%s\n",
			rec_written, h_good, rec_path,
			rec_failed ? " WRITE ERRORS" : "");
		if (rec_written > 1)
			fprintf(stderr,
				"to mkv: ./vintagecam --export %s --fps 30 | "
				"ffmpeg -f mjpeg -framerate 30 -i - -c copy %s.mkv\n",
				rec_path, rec_path);
		if (rec_failed || q_dropped || rec_written != h_good)
			rc = 1;
	}
	if (ev_file)
		fclose(ev_file);
	close_camera(c);
	libusb_exit(ctx);
	return rc;
}
