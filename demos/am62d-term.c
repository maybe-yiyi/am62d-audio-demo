#include <cJSON.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>

#define WEBRTC_FIFO "/tmp/am62d_webrtc"
#define YAMNET_FIFO "/tmp/am62d_yamnet"
#define BAR_W 20
#define DB_FLOOR -120.0f
#define LINEBUF_CAP 8192

struct reader {
	char buf[LINEBUF_CAP];
	int len;
};

static int webrtc_rows = 0;
static int yamnet_rows = 0;

static void draw_bar(float val, float lo, float hi)
{
	float frac = (val - lo) / (hi - lo);
	if (frac < 0.0f) frac = 0.0f;
	if (frac > 1.0f) frac = 1.0f;
	int filled = (int)(frac * BAR_W + 0.5f);
	const char *col = frac >= 0.75f ? "\033[1;32m"
			: frac >= 0.40f ? "\033[33m"
			: "\033[2;37m";
	fputs(col, stdout);
	for (int i = 0; i < BAR_W; i++)
		fputs(i < filled ? "\xe2\x96\x88" : "\xe2\x96\x91", stdout);
	fputs("\033[0m", stdout);
}

static void render_webrtc(float raw_rms, float raw_peak, float raw_floor,
			float proc_rms, float proc_peak, float proc_floor)
{
	float raw_snr = raw_rms - raw_floor;
	if (raw_snr < 0.0f)
		raw_snr = 0.0f;
	float proc_snr = proc_rms - proc_floor;
	if (proc_snr < 0.0f)
		proc_snr = 0.0f;

	if (webrtc_rows + yamnet_rows > 0)
		printf("\033[%dA", webrtc_rows + yamnet_rows);

	printf("%-38s%-38s\n", "  RAW MIC INPUT", "       AFTER WEBRTC NS");

	printf("  RMS   %6.1f dBFS  ", raw_rms);
	draw_bar(raw_rms, -60.0f, 0.0f);
	printf("    RMS   %6.1f dBFS  ", proc_rms);
	draw_bar(proc_rms, -60.0f, 0.0f);
	printf("\033[K\n");

	printf("  Peak  %6.1f dBFS  ", raw_peak);
	draw_bar(raw_peak, -60.0f, 0.0f);
	printf("    Peak  %6.1f dBFS  ", proc_peak);
	draw_bar(proc_peak, -60.0f, 0.0f);
	printf("\033[K\n");

	printf("  Floor %6.1f dBFS  ", raw_floor);
	draw_bar(raw_floor, -60.0f, 0.0f);
	printf("    Floor %6.1f dBFS  ", proc_floor);
	draw_bar(proc_floor, -60.0f, 0.0f);
	printf("\033[K\n");

	printf("  SNR   %6.1f dB    ", raw_snr);
	draw_bar(raw_snr, 0.0f, 40.0f);
	printf("    SNR   %6.1f dB    ", proc_snr);
	draw_bar(proc_snr, 0.0f, 40.0f);
	printf("\033[K\n");

	printf("\033[K\n");

	webrtc_rows = 6;

	if (yamnet_rows > 0)
		printf("\033[%dB", yamnet_rows);

	fflush(stdout);
}

static void render_yamnet(cJSON *labels, cJSON *scores)
{
	/* webrtc block must be established first since yamnet always renders below it */
	if (webrtc_rows == 0)
		return;

	int n = cJSON_GetArraySize(scores);
	if (yamnet_rows > 0)
		printf("\033[%dA", yamnet_rows);

	int rendered = 0;
	for (int i = 0; i < n; i++) {
		cJSON *lbl = cJSON_GetArrayItem(labels, i);
		cJSON *sc = cJSON_GetArrayItem(scores, i);
		if (!lbl || !sc || !cJSON_IsString(lbl) || !lbl->valuestring)
			break;
		rendered++;
		float s = (float)sc->valuedouble;
		if (s < 0.0f) s = 0.0f;
		if (s > 1.0f) s = 1.0f;
		int filled = (int)(s * BAR_W + 0.5f);
		const char *col = s >= 0.5f ? "\033[1;32m"
				: s >= 0.2f ? "\033[33m"
				: "\033[2;37m";
		printf("%s%-12s\033[0m [", col, lbl->valuestring);
		for (int j = 0; j < BAR_W; j++)
			fputs(j < filled ? "\xe2\x96\x88" : "\xe2\x96\x91", stdout);
		printf("] \033[1m%.3f\033[0m\033[K\n", s);
	}

	for (int i = rendered; i < yamnet_rows; i++)
		printf("\033[K\n");

	yamnet_rows = rendered;
	fflush(stdout);
}

static int open_fifo_retry(const char *path)
{
	int fd;
	while ((fd = open(path, O_RDONLY | O_NONBLOCK)) < 0)
		usleep(200000);
	return fd;
}

static void handle_line(const char *fifo_path, char *line)
{
	cJSON *root = cJSON_Parse(line);
	if (!root)
		return;

	if (strcmp(fifo_path, WEBRTC_FIFO) == 0) {
		cJSON *raw = cJSON_GetObjectItem(root, "raw");
		cJSON *proc = cJSON_GetObjectItem(root, "proc");
		if (raw && proc) {
			cJSON *rr = cJSON_GetObjectItem(raw, "rms");
			cJSON *rp = cJSON_GetObjectItem(raw, "peak");
			cJSON *rf = cJSON_GetObjectItem(raw, "floor");
			cJSON *pr = cJSON_GetObjectItem(proc, "rms");
			cJSON *pp = cJSON_GetObjectItem(proc, "peak");
			cJSON *pf = cJSON_GetObjectItem(proc, "floor");
			if (rr && rp && rf && pr && pp && pf)
				render_webrtc((float)rr->valuedouble, (float)rp->valuedouble,
					(float)rf->valuedouble, (float)pr->valuedouble,
					(float)pp->valuedouble, (float)pf->valuedouble);
		}
	} else {
		cJSON *labels = cJSON_GetObjectItem(root, "labels");
		cJSON *scores = cJSON_GetObjectItem(root, "scores");
		if (labels && scores)
			render_yamnet(labels, scores);
	}

	cJSON_Delete(root);
}

static void process_reader(struct reader *r, const char *path)
{
	char *start = r->buf;
	char *end = r->buf + r->len;
	char *nl;
	while ((nl = (char *)memchr(start, '\n', (size_t)(end - start))) != NULL) {
		*nl = '\0';
		if (nl > start)
			handle_line(path, start);
		start = nl + 1;
	}
	int remaining = (int)(end - start);
	if (remaining > 0 && start != r->buf)
		memmove(r->buf, start, (size_t)remaining);
	r->len = remaining;
	if (r->len >= LINEBUF_CAP - 1) {
		fprintf(stderr, "am62d-term: line buffer overflow, discarding\n");
		r->len = 0;
	}
}

int main(void)
{
	fprintf(stderr, "am62d-term: waiting for pipeline FIFOs...\n");
	int fd_webrtc = open_fifo_retry(WEBRTC_FIFO);
	int fd_yamnet = open_fifo_retry(YAMNET_FIFO);
	fprintf(stderr, "am62d-term: connected\n");

	int fds[2] = { fd_webrtc, fd_yamnet };
	const char *paths[2] = { WEBRTC_FIFO, YAMNET_FIFO };
	int maxfd = fd_webrtc > fd_yamnet ? fd_webrtc : fd_yamnet;
	struct reader readers[2] = {{{0}, 0}, {{0}, 0}};

	for (;;) {
		fd_set rfds;
		FD_ZERO(&rfds);
		FD_SET(fds[0], &rfds);
		FD_SET(fds[1], &rfds);

		struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
		if (select(maxfd + 1, &rfds, NULL, NULL, &tv) < 0) {
			if (errno == EINTR)
				continue;
			break;
		}

		for (int i = 0; i < 2; i++) {
			if (!FD_ISSET(fds[i], &rfds))
				continue;

			struct reader *r = &readers[i];
			ssize_t n = read(fds[i], r->buf + r->len,
					(size_t)(LINEBUF_CAP - r->len - 1));
			if (n <= 0) {
				if (n < 0 && (errno == EAGAIN || errno == EINTR))
					continue;
				/* EOF or real error => close and reopen */
				close(fds[i]);
				r->len = 0;
				fds[i] = open_fifo_retry(paths[i]);
				maxfd = fds[0] > fds[1] ? fds[0] : fds[1];
				continue;
			}

			r->len += (int)n;
			process_reader(r, paths[i]);
		}
	}

	close(fds[0]);
	close(fds[1]);
	return 0;
}
