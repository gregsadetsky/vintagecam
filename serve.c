// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * vintageserve: every camera being recorded into a folder as a live mjpeg
 * stream over http on this machine only, for obs (browser source) or a
 * browser. like the grid it only reads the files the recorders write, so it
 * cannot disturb a recorder.
 *
 *   http://127.0.0.1:8090/              list of cameras (/cams.json for scripts)
 *   http://127.0.0.1:8090/cam/NAME      a page with just that camera (obs)
 *   http://127.0.0.1:8090/cam/NAME.mjpg the stream itself
 *
 * NAME is the camera name from the recording's file name, with -2, -3 for
 * further cameras of the same model. a name keeps working across recorder
 * restarts: it always points at the newest growing recording of that camera.
 */
#include <arpa/inet.h>
#include <dirent.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define MAX_CAMS 32
#define FRAME_MAX (1 << 20)
#define LIVE_SECONDS 10		/* a recording written this recently is live */

struct camfile {
	char name[160];		/* url name */
	char path[1024];	/* its newest recording */
};

static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static struct camfile cams[MAX_CAMS];
static int n_cams;
static const char *dir;

static int cmp_str(const void *a, const void *b)
{
	return strcmp((const char *)a, (const char *)b);
}

/* file names are <date>-<time>-<camera name>-usb<path>.mjpeg */
static void scan(void)
{
	static char files[MAX_CAMS * 4][256];
	struct camfile found[MAX_CAMS];
	int n_files = 0, n_found = 0, i, k;
	DIR *d = opendir(dir);
	struct dirent *e;
	time_t now = time(NULL);

	if (!d)
		return;
	while ((e = readdir(d)) && n_files < MAX_CAMS * 4) {
		size_t l = strlen(e->d_name);
		char path[1024];
		struct stat st;

		if (l < 24 || l > 250 || strcmp(e->d_name + l - 6, ".mjpeg"))
			continue;
		snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
		if (stat(path, &st) < 0 || now - st.st_mtime > LIVE_SECONDS)
			continue;
		snprintf(files[n_files++], sizeof files[0], "%s", e->d_name);
	}
	closedir(d);
	/* newest first, so the first file seen for a name+port wins */
	qsort(files, n_files, sizeof files[0], cmp_str);
	for (i = n_files - 1; i >= 0 && n_found < MAX_CAMS; i--) {
		char base[160], port[64], *usb;
		int same = 0, dup = 0;

		snprintf(base, sizeof base, "%.*s", (int)strlen(files[i]) - 16 - 6,
			 files[i] + 16);
		usb = strstr(base, "-usb");
		while (usb && strstr(usb + 1, "-usb"))
			usb = strstr(usb + 1, "-usb");
		if (!usb)
			continue;
		snprintf(port, sizeof port, "%s", usb);
		*usb = 0;
		for (k = 0; k < n_found; k++) {
			const char *p = strrchr(found[k].path, '/');
			size_t bl = strlen(base);

			if (strncmp(found[k].name, base, bl) ||
			    (found[k].name[bl] && found[k].name[bl] != '-'))
				continue;
			same++;
			/* an older recording of the same camera on the same port */
			if (p && strstr(p, port) &&
			    !strcmp(strstr(p, port) + strlen(port), ".mjpeg"))
				dup = 1;
		}
		if (dup)
			continue;
		if (same)
			snprintf(found[n_found].name, sizeof found[0].name, "%.150s-%d",
				 base, same + 1);
		else
			snprintf(found[n_found].name, sizeof found[0].name, "%s", base);
		snprintf(found[n_found].path, sizeof found[0].path, "%s/%s", dir,
			 files[i]);
		n_found++;
	}
	pthread_mutex_lock(&mu);
	/* keep the names stable: update known names, add new ones, and keep a
	 * camera that is not live right now pointing at its last recording */
	for (i = 0; i < n_found; i++) {
		for (k = 0; k < n_cams; k++)
			if (!strcmp(cams[k].name, found[i].name))
				break;
		if (k == n_cams && n_cams < MAX_CAMS) {
			n_cams++;
			fprintf(stderr, "serve: http://127.0.0.1:8090/cam/%s\n",
				found[i].name);
		}
		if (k < MAX_CAMS)
			cams[k] = found[i];
	}
	pthread_mutex_unlock(&mu);
}

static void *scan_main(void *arg)
{
	(void)arg;
	for (;;) {
		scan();
		sleep(2);
	}
	return NULL;
}

static int cam_path(const char *name, char *path, size_t cap)
{
	int k, ok = 0;

	pthread_mutex_lock(&mu);
	for (k = 0; k < n_cams; k++)
		if (!strcmp(cams[k].name, name)) {
			snprintf(path, cap, "%s", cams[k].path);
			ok = 1;
		}
	pthread_mutex_unlock(&mu);
	return ok;
}

/* last complete row of a recording's index: offset and size of a frame */
static int last_row(const char *mjpeg, long long *off, int *len)
{
	char path[1100], buf[1024], *p, *line;
	FILE *f;
	long size, start, frame;
	double t, unix_t;
	size_t n;

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
	p = strrchr(buf, '\n');
	if (!p)
		return -1;
	*p = 0;
	line = strrchr(buf, '\n');
	line = line ? line + 1 : buf;
	if (sscanf(line, "%ld,%lf,%lf,%lld,%d", &frame, &t, &unix_t, off, len) != 5)
		return -1;
	return 0;
}

static int send_all(int fd, const void *data, size_t len)
{
	const char *p = data;

	while (len > 0) {
		ssize_t w = send(fd, p, len, 0);

		if (w <= 0)
			return -1;
		p += w;
		len -= w;
	}
	return 0;
}

static void send_text(int fd, const char *status, const char *type, const char *body)
{
	char head[256];

	snprintf(head, sizeof head,
		 "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
		 "Cache-Control: no-store\r\nConnection: close\r\n\r\n",
		 status, type, strlen(body));
	send_all(fd, head, strlen(head));
	send_all(fd, body, strlen(body));
}

static void stream(int fd, const char *name)
{
	static const char head[] =
		"HTTP/1.1 200 OK\r\n"
		"Content-Type: multipart/x-mixed-replace; boundary=frame\r\n"
		"Cache-Control: no-store\r\nConnection: close\r\n\r\n";
	unsigned char *jpg = malloc(FRAME_MAX);
	char path[1024], part[128];
	long long off, last_off = -1;
	char last_path[1024] = "";
	int len;

	if (!jpg || send_all(fd, head, sizeof head - 1) < 0) {
		free(jpg);
		return;
	}
	for (;;) {
		struct timespec ts = { 0, 30 * 1000000L };
		int f;

		nanosleep(&ts, NULL);
		if (!cam_path(name, path, sizeof path) ||
		    last_row(path, &off, &len) < 0 || len <= 0 || len > FRAME_MAX)
			continue;
		if (off == last_off && !strcmp(path, last_path))
			continue;
		f = open(path, O_RDONLY);
		if (f < 0)
			continue;
		if (pread(f, jpg, len, off) != len) {
			close(f);
			continue;
		}
		close(f);
		snprintf(part, sizeof part,
			 "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %d\r\n\r\n",
			 len);
		if (send_all(fd, part, strlen(part)) < 0 ||
		    send_all(fd, jpg, len) < 0 || send_all(fd, "\r\n", 2) < 0)
			break;
		last_off = off;
		snprintf(last_path, sizeof last_path, "%s", path);
	}
	free(jpg);
}

/* camera names only ever hold these characters, anything else is refused */
static int name_ok(const char *s)
{
	if (!*s || strlen(s) > 150)
		return 0;
	for (; *s; s++)
		if (!((*s >= 'a' && *s <= 'z') || (*s >= '0' && *s <= '9') ||
		      *s == '-' || *s == '.'))
			return 0;
	return 1;
}

static void *client_main(void *arg)
{
	int fd = (int)(long)arg, k;
	char req[2048], path[512], body[16384], known[1024];
	ssize_t n = recv(fd, req, sizeof req - 1, 0);

	if (n <= 0)
		goto out;
	req[n] = 0;
	if (sscanf(req, "GET %511s", path) != 1) {
		send_text(fd, "400 Bad Request", "text/plain", "bad request\n");
		goto out;
	}
	if (!strcmp(path, "/")) {
		int o = snprintf(body, sizeof body,
			"<!doctype html><title>vintagecam</title>"
			"<style>body{background:#111;color:#ddd;font:14px sans-serif}"
			"a{color:#9cf}div{display:inline-block;margin:8px}"
			"img{display:block;width:352px}</style>");

		pthread_mutex_lock(&mu);
		for (k = 0; k < n_cams && o < (int)sizeof body - 600; k++)
			o += snprintf(body + o, sizeof body - o,
				"<div><a href=\"/cam/%s\"><img src=\"/cam/%s.mjpg\">%s</a></div>",
				cams[k].name, cams[k].name, cams[k].name);
		if (!n_cams)
			o += snprintf(body + o, sizeof body - o,
				"no camera is being recorded into this folder yet");
		pthread_mutex_unlock(&mu);
		send_text(fd, "200 OK", "text/html", body);
		goto out;
	}
	if (!strcmp(path, "/cams.json")) {
		int o = snprintf(body, sizeof body, "[");

		pthread_mutex_lock(&mu);
		for (k = 0; k < n_cams && o < (int)sizeof body - 200; k++)
			o += snprintf(body + o, sizeof body - o, "%s\"%s\"",
				      k ? "," : "", cams[k].name);
		pthread_mutex_unlock(&mu);
		snprintf(body + o, sizeof body - o, "]\n");
		send_text(fd, "200 OK", "application/json", body);
		goto out;
	}
	if (!strncmp(path, "/cam/", 5)) {
		char *name = path + 5;
		size_t l = strlen(name);
		int want_stream = l > 5 && !strcmp(name + l - 5, ".mjpg");

		if (want_stream)
			name[l - 5] = 0;
		if (!name_ok(name) || !cam_path(name, known, sizeof known)) {
			send_text(fd, "404 Not Found", "text/plain", "no such camera\n");
			goto out;
		}
		if (want_stream) {
			stream(fd, name);
			goto out;
		}
		snprintf(body, sizeof body,
			"<!doctype html><title>%s</title>"
			"<style>html,body{margin:0;height:100%%;background:#000;overflow:hidden}"
			"img{width:100%%;height:100%%;object-fit:contain;image-rendering:pixelated}"
			"</style><img src=\"/cam/%s.mjpg\">", name, name);
		send_text(fd, "200 OK", "text/html", body);
		goto out;
	}
	send_text(fd, "404 Not Found", "text/plain", "not found\n");
out:
	close(fd);
	return NULL;
}

int main(int argc, char **argv)
{
	struct sockaddr_in addr = { 0 };
	int port = 8090, s, one = 1, i;
	pthread_t th;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--port") && i + 1 < argc)
			port = atoi(argv[++i]);
		else if (argv[i][0] != '-' && !dir)
			dir = argv[i];
		else
			dir = NULL, i = argc;
	}
	if (!dir || port < 1024 || port > 65535) {
		fprintf(stderr, "usage: vintageserve DIR [--port N]\n");
		return 2;
	}
	signal(SIGPIPE, SIG_IGN);
	s = socket(AF_INET, SOCK_STREAM, 0);
	setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);	/* this machine only */
	if (s < 0 || bind(s, (struct sockaddr *)&addr, sizeof addr) < 0 ||
	    listen(s, 16) < 0) {
		perror("vintageserve: listen");
		return 1;
	}
	fprintf(stderr, "serve: http://127.0.0.1:%d/\n", port);
	scan();
	pthread_create(&th, NULL, scan_main, NULL);
	for (;;) {
		int c = accept(s, NULL, NULL);
		pthread_t ct;

		if (c < 0)
			continue;
		if (pthread_create(&ct, NULL, client_main, (void *)(long)c))
			close(c);
		else
			pthread_detach(ct);
	}
}
