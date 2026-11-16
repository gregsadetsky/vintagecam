// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * --export: turn a recording (.mjpeg + .mjpeg.csv) into a constant frame
 * rate mjpeg stream on stdout that keeps real time: each output tick gets
 * the latest recorded frame, and ticks inside an outage get a black frame.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <turbojpeg.h>
#include "cam.h"

#define GAP_SECONDS 0.5		/* older than this: show black, not a stale frame */

struct row {
	double t;
	long long off;
	int len;
};

static int put(const unsigned char *p, unsigned long n)
{
	while (n > 0) {
		ssize_t w = write(STDOUT_FILENO, p, n);

		if (w <= 0)
			return -1;
		p += w;
		n -= w;
	}
	return 0;
}

int export_main(const char *path, double fps)
{
	char idx_path[1100], line[256];
	struct row *rows = NULL;
	long n = 0, cap = 0, k, j = 0, nticks, last_j = -1;
	long dups = 0, blacks = 0, used = 0;
	unsigned char *buf = NULL, *black = NULL, *zero;
	unsigned long black_len = 0;
	int bufcap = 0, w, h, sub, cs;
	FILE *idx, *vid;
	tjhandle tj;

	if (isatty(STDOUT_FILENO)) {
		fprintf(stderr, "--export writes video to stdout, pipe it to ffmpeg\n");
		return 2;
	}
	if (fps <= 0 || fps > 240) {
		fprintf(stderr, "bad --fps\n");
		return 2;
	}
	snprintf(idx_path, sizeof idx_path, "%s.csv", path);
	idx = fopen(idx_path, "r");
	vid = fopen(path, "rb");
	if (!idx || !vid) {
		fprintf(stderr, "cannot open %s and its .csv\n", path);
		return 1;
	}
	while (fgets(line, sizeof line, idx)) {
		long frame;
		double t, unix_t;
		long long off;
		int len;

		if (sscanf(line, "%ld,%lf,%lf,%lld,%d", &frame, &t, &unix_t,
			   &off, &len) != 5)
			continue;	/* header, or a torn last line */
		if (n == cap) {
			cap = cap ? cap * 2 : 4096;
			rows = realloc(rows, cap * sizeof *rows);
			if (!rows)
				return 1;
		}
		rows[n].t = t;
		rows[n].off = off;
		rows[n].len = len;
		if (len > bufcap)
			bufcap = len;
		n++;
	}
	fclose(idx);
	if (n < 1) {
		fprintf(stderr, "no frames in index\n");
		return 1;
	}
	buf = malloc(bufcap);
	if (!buf)
		return 1;

	/* size from the first frame, then make a matching black jpeg */
	if (fseeko(vid, rows[0].off, SEEK_SET) ||
	    fread(buf, 1, rows[0].len, vid) != (size_t)rows[0].len) {
		fprintf(stderr, "cannot read first frame\n");
		return 1;
	}
	tj = tjInitDecompress();
	if (tjDecompressHeader3(tj, buf, rows[0].len, &w, &h, &sub, &cs)) {
		fprintf(stderr, "first frame: %s\n", tjGetErrorStr2(tj));
		return 1;
	}
	tjDestroy(tj);
	zero = calloc((size_t)w * h, 3);
	tj = tjInitCompress();
	if (!zero || tjCompress2(tj, zero, w, 0, h, TJPF_RGB, &black, &black_len,
				 sub, 75, 0)) {
		fprintf(stderr, "cannot make black frame\n");
		return 1;
	}
	tjDestroy(tj);
	free(zero);

	nticks = (long)(rows[n - 1].t * fps) + 1;
	for (k = 0; k < nticks; k++) {
		double tick = k / fps;

		while (j + 1 < n && rows[j + 1].t <= tick + 1e-9)
			j++;
		if (tick - rows[j].t > GAP_SECONDS) {
			blacks++;
			if (put(black, black_len))
				return 1;
			continue;
		}
		if (j != last_j) {
			if (fseeko(vid, rows[j].off, SEEK_SET) ||
			    fread(buf, 1, rows[j].len, vid) != (size_t)rows[j].len) {
				fprintf(stderr, "short read at frame %ld\n", j);
				return 1;
			}
			last_j = j;
			used++;
		} else {
			dups++;
		}
		if (put(buf, rows[j].len))
			return 1;
	}
	fprintf(stderr,
		"export: %ld recorded frames over %.2fs -> %ld frames at %g fps "
		"(%ld source frames used, %ld repeats, %ld black)\n",
		n, rows[n - 1].t, nticks, fps, used, dups, blacks);
	return 0;
}
