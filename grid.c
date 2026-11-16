// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * vintagegrid: one window showing the latest frame of every recording that
 * is growing in a directory. it only reads the files the recorders write
 * (.mjpeg + .mjpeg.csv), so it cannot slow a recorder down or take a camera
 * away from one, and it can be started and closed at any time.
 *
 * a tile goes dim when its recording has had no new frame for 2 seconds.
 * tiles are in file name order: by start time, then camera name.
 */
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <turbojpeg.h>

#define MAX_TILES 16
#define STALE_SECONDS 2.0
#define FRAME_MAX (1 << 20)

struct tile {
	char path[1024];
	long long last_off;	/* frame currently shown */
	unsigned char *rgb;	/* decoded, cell sized */
	double last_unix;
	int have;
};

static struct tile tiles[MAX_TILES];
static int n_tiles;
static int cell_w = 352, cell_h = 288, cols = 3, rows = 2;
static volatile sig_atomic_t stopping;

static void on_signal(int sig)
{
	(void)sig;
	stopping = 1;
}

static double wall(void)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);
	return tv.tv_sec + tv.tv_usec / 1e6;
}

static int cmp_tiles(const void *a, const void *b)
{
	return strcmp(((const struct tile *)a)->path, ((const struct tile *)b)->path);
}

/* add recordings that were written to after `since` and are not known yet */
static void scan_dir(const char *dir, double since)
{
	DIR *d = opendir(dir);
	struct dirent *e;
	int added = 0, i;

	if (!d)
		return;
	while ((e = readdir(d)) && n_tiles < MAX_TILES) {
		size_t l = strlen(e->d_name);
		char path[1024];
		struct stat st;

		if (l < 7 || strcmp(e->d_name + l - 6, ".mjpeg"))
			continue;
		snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
		if (stat(path, &st) < 0 || st.st_mtime < since)
			continue;
		for (i = 0; i < n_tiles; i++)
			if (!strcmp(tiles[i].path, path))
				break;
		if (i < n_tiles)
			continue;
		memset(&tiles[n_tiles], 0, sizeof tiles[0]);
		snprintf(tiles[n_tiles].path, sizeof tiles[0].path, "%s", path);
		tiles[n_tiles].last_off = -1;
		tiles[n_tiles].rgb = calloc((size_t)cell_w * cell_h, 3);
		if (!tiles[n_tiles].rgb)
			break;
		fprintf(stderr, "grid: showing %s\n", e->d_name);
		n_tiles++;
		added = 1;
	}
	closedir(d);
	if (added)
		qsort(tiles, n_tiles, sizeof tiles[0], cmp_tiles);
}

/* last complete row of the index: unix time, offset and size of a frame */
static int last_row(const char *mjpeg, double *unix_t, long long *off, int *len)
{
	char path[1100], buf[1024], *p, *line;
	FILE *f;
	long size, start;
	size_t n;
	long frame;
	double t;

	snprintf(path, sizeof path, "%s.csv", mjpeg);
	f = fopen(path, "r");
	if (!f)
		return -1;
	fseek(f, 0, SEEK_END);
	size = ftell(f);
	start = size > (long)sizeof buf - 1 ? size - ((long)sizeof buf - 1) : 0;
	fseek(f, start, SEEK_SET);
	n = fread(buf, 1, sizeof buf - 1, f);
	fclose(f);
	buf[n] = 0;
	p = strrchr(buf, '\n');		/* end of the last complete row */
	if (!p)
		return -1;
	*p = 0;
	line = strrchr(buf, '\n');
	line = line ? line + 1 : buf;
	if (sscanf(line, "%ld,%lf,%lf,%lld,%d", &frame, &t, unix_t, off, len) != 5)
		return -1;		/* the header row */
	return 0;
}

/* nearest neighbour fit of src into a cell, aspect kept, centred */
static void fit(const unsigned char *src, int sw, int sh, unsigned char *dst)
{
	int dw = cell_w, dh = cell_w * sh / sw, x, y, x0, y0;

	if (dh > cell_h) {
		dh = cell_h;
		dw = cell_h * sw / sh;
	}
	x0 = (cell_w - dw) / 2;
	y0 = (cell_h - dh) / 2;
	memset(dst, 0, (size_t)cell_w * cell_h * 3);
	for (y = 0; y < dh; y++) {
		const unsigned char *srow = src + (size_t)(y * sh / dh) * sw * 3;
		unsigned char *drow = dst + ((size_t)(y + y0) * cell_w + x0) * 3;

		for (x = 0; x < dw; x++)
			memcpy(drow + x * 3, srow + (x * sw / dw) * 3, 3);
	}
}

static void update_tile(struct tile *t, tjhandle tj, unsigned char *jpg,
			unsigned char **scratch, size_t *scratch_cap)
{
	long long off;
	int len, w, h, sub, cs, fd;
	double unix_t;

	if (last_row(t->path, &unix_t, &off, &len) < 0)
		return;
	t->last_unix = unix_t;
	if (off == t->last_off || len <= 0 || len > FRAME_MAX)
		return;
	fd = open(t->path, O_RDONLY);
	if (fd < 0)
		return;
	if (pread(fd, jpg, len, off) != len) {
		close(fd);
		return;
	}
	close(fd);
	if (tjDecompressHeader3(tj, jpg, len, &w, &h, &sub, &cs))
		return;
	if ((size_t)w * h * 3 > *scratch_cap) {
		free(*scratch);
		*scratch_cap = (size_t)w * h * 3;
		*scratch = malloc(*scratch_cap);
		if (!*scratch) {
			*scratch_cap = 0;
			return;
		}
	}
	if (tjDecompress2(tj, jpg, len, *scratch, w, 0, h, TJPF_RGB, TJFLAG_FASTDCT))
		return;
	fit(*scratch, w, h, t->rgb);
	t->last_off = off;
	t->have = 1;
}

int main(int argc, char **argv)
{
	const char *dir = NULL;
	int W, H, i, x, y, fps = 15, to_stdout = 0;
	unsigned char *canvas, *jpg, *out, *scratch = NULL;
	size_t scratch_cap = 0;
	unsigned long out_cap;
	tjhandle tjd, tjc;
	FILE *player;
	char cmd[512];
	double since, t_scan = 0;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--cols") && i + 1 < argc)
			cols = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--rows") && i + 1 < argc)
			rows = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--stdout"))
			to_stdout = 1;	/* the grid as mjpeg, for piping */
		else if (!strcmp(argv[i], "--all"))
			t_scan = -1;	/* also recordings that are not growing */
		else if (argv[i][0] != '-' && !dir)
			dir = argv[i];
		else
			dir = NULL, i = argc;
	}
	if (!dir || cols < 1 || rows < 1 || cols * rows > MAX_TILES) {
		fprintf(stderr, "usage: vintagegrid DIR [--cols N] [--rows N] [--all] [--stdout]\n");
		return 2;
	}
	W = cols * cell_w;
	H = rows * cell_h;
	since = t_scan < 0 ? 0 : wall() - 30;
	t_scan = 0;

	canvas = malloc((size_t)W * H * 3);
	jpg = malloc(FRAME_MAX);
	out_cap = tjBufSize(W, H, TJSAMP_420);
	out = malloc(out_cap);
	tjd = tjInitDecompress();
	tjc = tjInitCompress();
	if (!canvas || !jpg || !out || !tjd || !tjc)
		return 1;

	signal(SIGINT, on_signal);
	signal(SIGTERM, on_signal);
	signal(SIGPIPE, SIG_IGN);
	snprintf(cmd, sizeof cmd,
		 "ffplay -loglevel error -f mjpeg -framerate %d -fflags nobuffer "
		 "-flags low_delay -autoexit -window_title 'vintagecam grid' -i -", fps);
	player = to_stdout ? stdout : popen(cmd, "w");
	if (!player) {
		perror("ffplay");
		return 1;
	}

	while (!stopping) {
		struct timespec ts = { 0, 1000000000L / fps };
		unsigned long n = out_cap;
		unsigned char *dst = out;
		double now = wall();

		if (now - t_scan > 2.0) {
			scan_dir(dir, since);
			t_scan = now;
		}
		memset(canvas, 0, (size_t)W * H * 3);
		for (i = 0; i < n_tiles && i < cols * rows; i++) {
			struct tile *t = &tiles[i];
			int stale, cx = (i % cols) * cell_w, cy = (i / cols) * cell_h;

			update_tile(t, tjd, jpg, &scratch, &scratch_cap);
			if (!t->have)
				continue;
			stale = now - t->last_unix > STALE_SECONDS;
			for (y = 0; y < cell_h; y++) {
				unsigned char *d = canvas + ((size_t)(cy + y) * W + cx) * 3;
				const unsigned char *s = t->rgb + (size_t)y * cell_w * 3;

				if (!stale) {
					memcpy(d, s, (size_t)cell_w * 3);
					continue;
				}
				for (x = 0; x < cell_w * 3; x++)
					d[x] = s[x] / 4;
			}
		}
		if (tjCompress2(tjc, canvas, W, 0, H, TJPF_RGB, &dst, &n, TJSAMP_420,
				85, TJFLAG_NOREALLOC))
			break;
		if (fwrite(out, 1, n, player) != n || fflush(player))
			break;		/* window closed */
		nanosleep(&ts, NULL);
	}
	if (!to_stdout)
		pclose(player);
	return 0;
}
