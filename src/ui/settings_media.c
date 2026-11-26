// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _GNU_SOURCE
#include "ui/settings_grid.h"

#include "capture/save.h"
#include "config/config.h"
#include "paths.h"
#include "record/rec_cfg.h"
#include "util/util.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

#define LOG_CAP 16384
#define MAX_ARGS 40

bool media_is_url(const char *s) {
	return s && (strncasecmp(s, "http://", 7) == 0 || strncasecmp(s, "https://", 8) == 0);
}

static size_t name_len(const char *s) {
	return media_is_url(s) ? strcspn(s, "?#") : strlen(s);
}

static bool ends_with(const char *s, const char *ext) {
	size_t n = name_len(s), l = strlen(ext);
	return n >= l && strncasecmp(s + n - l, ext, l) == 0;
}

bool media_path(const char *path) {
	static const char *const EXTS[] = {".gif", ".mp4", ".webm", ".mkv", ".mov", ".m4v", ".avi"};
	for (size_t i = 0; path && i < sizeof EXTS / sizeof *EXTS; i++)
		if (ends_with(path, EXTS[i])) return true;
	return false;
}

bool media_entry(const struct history_entry *e) {
	return e->kind != HISTORY_OCR &&
		   (e->video || media_path(e->path) || (media_is_url(e->url) && media_path(e->url)));
}

bool media_is_gif(const struct history_entry *e) {
	const char *s = e->path ? e->path : e->url;
	return s && ends_with(s, ".gif");
}

const char *media_source(const struct history_entry *e) {
	if (history_has_file(e)) return e->path;
	return e->kind == HISTORY_UPLOAD && media_is_url(e->url) ? e->url : NULL;
}

char *media_ffmpeg(struct config *cfg) {
	const char *bin = rec_cfg_ffmpeg(cfg);
	return grabit_in_path(bin) ? strdup(bin) : NULL;
}

void media_secs_text(int secs, char *out, size_t n) {
	if (secs < 0)
		snprintf(out, n, "%s", "");
	else
		grabit_format_duration(secs, out, n);
}

static size_t ff_argv(char **argv, const char *ffmpeg, const char *const *pre, const char *src,
					  char *fsrc, size_t n, const char *const *tail) {
	size_t i = 0;
	argv[i++] = (char *)ffmpeg;
	argv[i++] = (char *)"-hide_banner";
	argv[i++] = (char *)"-nostdin";
	for (size_t k = 0; pre && pre[k]; k++)
		argv[i++] = (char *)pre[k];
	argv[i++] = (char *)"-protocol_whitelist";
	if (media_is_url(src)) {
		argv[i++] = (char *)"http,https,tcp,tls";
		argv[i++] = (char *)"-rw_timeout";
		argv[i++] = (char *)"10000000";
		argv[i++] = (char *)"-i";
		argv[i++] = (char *)src;
	} else {
		argv[i++] = (char *)"file";
		argv[i++] = (char *)"-i";
		snprintf(fsrc, n, "file:%s", src);
		argv[i++] = fsrc;
	}
	for (size_t k = 0; tail[k]; k++)
		argv[i++] = (char *)tail[k];
	argv[i] = NULL;
	return i;
}

static pid_t spawn_ff(char *const argv[], int out, int err) {
	posix_spawn_file_actions_t fa;
	posix_spawnattr_t at;
	if (posix_spawn_file_actions_init(&fa) != 0) return -1;
	if (posix_spawnattr_init(&at) != 0) {
		posix_spawn_file_actions_destroy(&fa);
		return -1;
	}
	sigset_t none, def;
	sigemptyset(&none);
	sigemptyset(&def);
	sigaddset(&def, SIGPIPE);
	posix_spawnattr_setsigmask(&at, &none);
	posix_spawnattr_setsigdefault(&at, &def);
	posix_spawnattr_setflags(&at, POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF);
	posix_spawn_file_actions_addopen(&fa, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
	if (out >= 0)
		posix_spawn_file_actions_adddup2(&fa, out, STDOUT_FILENO);
	else
		posix_spawn_file_actions_addopen(&fa, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
	if (err >= 0)
		posix_spawn_file_actions_adddup2(&fa, err, STDERR_FILENO);
	else
		posix_spawn_file_actions_addopen(&fa, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
	pid_t pid = -1;
	int rc = posix_spawnp(&pid, argv[0], &fa, &at, argv, environ);
	posix_spawnattr_destroy(&at);
	posix_spawn_file_actions_destroy(&fa);
	return rc == 0 ? pid : -1;
}

static void stop_reap(pid_t pid) {
	kill(pid, SIGTERM);
	(void)grabit_reap_grace(pid, NULL, 500);
}

static void parse_info(const char *log, struct media_info *info) {
	const char *d = strstr(log, "Duration: ");
	int hh, mm;
	double ss;
	if (d && sscanf(d + 10, "%d:%d:%lf", &hh, &mm, &ss) == 3 && hh >= 0 && mm >= 0 && ss >= 0)
		info->secs = hh * 3600 + mm * 60 + (int)(ss + 0.5);
	const char *v = strstr(log, "Video: ");
	if (!v) return;
	for (const char *p = v; *p && *p != '\n'; p++) {
		if (!isdigit((unsigned char)*p) || p[-1] != ' ') continue;
		int w, h;
		if (sscanf(p, "%dx%d", &w, &h) == 2 && w > 0 && h > 0 && w < 20000 && h < 20000) {
			info->w = w;
			info->h = h;
			return;
		}
	}
}

int media_grab(const char *ffmpeg, const char *src, const char *dest, int max, int timeout_ms,
			   atomic_bool *stop, struct media_info *info) {
	*info = (struct media_info){.secs = -1};
	int ep[2];
	if (pipe2(ep, O_CLOEXEC) != 0) return -1;
	char scale[160], fsrc[PATH_MAX + 8];
	snprintf(scale, sizeof scale,
			 "scale=w='min(%d,iw)':h='min(%d,ih)':force_original_aspect_ratio=decrease", max, max);
	const char *const tail[] = {"-map", "0:v:0", "-frames:v", "1", "-vf", scale, "-f",
								"image2pipe", "-c:v", "png", "-y", dest, NULL};
	char *argv[MAX_ARGS];
	ff_argv(argv, ffmpeg, NULL, src, fsrc, sizeof fsrc, tail);
	pid_t pid = spawn_ff(argv, -1, ep[1]);
	close(ep[1]);
	if (pid < 0) {
		close(ep[0]);
		return -1;
	}
	char log[LOG_CAP];
	size_t len = 0;
	int64_t end = grabit_now_ms() + timeout_ms;
	bool killed = false;
	for (;;) {
		int64_t now = grabit_now_ms();
		if ((stop && atomic_load(stop)) || now >= end) {
			killed = true;
			break;
		}
		struct pollfd pf = {.fd = ep[0], .events = POLLIN};
		int r = grabit_poll_deadline(&pf, 1, end < now + 100 ? end : now + 100);
		if (r < 0) break;
		if (r == 0) continue;
		char buf[4096];
		ssize_t n = read(ep[0], buf, sizeof buf);
		if (n < 0 && errno == EINTR) continue;
		if (n <= 0) break;
		size_t take = len + (size_t)n < LOG_CAP ? (size_t)n : 0;
		memcpy(log + len, buf, take);
		len += take;
	}
	close(ep[0]);
	log[len] = '\0';
	parse_info(log, info);
	if (killed) {
		stop_reap(pid);
		return -1;
	}
	int status = 0;
	struct stat sb;
	if (grabit_waitpid_intr(pid, &status) == 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0 &&
		stat(dest, &sb) == 0 && sb.st_size > 0)
		return 0;
	return 1;
}

static bool read_meta(const char *id, struct media_info *info) {
	char path[PATH_MAX];
	history_thumb_path(id, ".meta", path, sizeof path);
	FILE *f = path[0] ? fopen(path, "re") : NULL;
	if (!f) return false;
	struct media_info m = {.secs = -1};
	bool ok = fscanf(f, "%d %d %d", &m.secs, &m.w, &m.h) == 3;
	fclose(f);
	if (ok) *info = m;
	return ok;
}

static void write_meta(const char *id, const struct media_info *info) {
	char path[PATH_MAX], buf[64];
	history_thumb_path(id, ".meta", path, sizeof path);
	int n = snprintf(buf, sizeof buf, "%d %d %d\n", info->secs, info->w, info->h);
	if (path[0] && n > 0) (void)paths_atomic_write(path, buf, (size_t)n);
}

cairo_surface_t *media_poster(const char *ffmpeg, const char *id, const char *src,
							  atomic_bool *stop, struct media_info *info) {
	*info = (struct media_info){.secs = -1};
	char cache[PATH_MAX], none[PATH_MAX], part[PATH_MAX + 16];
	history_thumb_path(id, ".png", cache, sizeof cache);
	history_thumb_path(id, ".none", none, sizeof none);
	if (!cache[0] || !none[0]) return NULL;
	if (access(cache, R_OK) == 0) {
		cairo_surface_t *s = grabit_load_image_surface(cache, "history");
		if (s) {
			read_meta(id, info);
			return s;
		}
		unlink(cache);
	}
	if (access(none, F_OK) == 0 || paths_mkdir_p(history_thumb_dir()) != 0) return NULL;
	grid_temp_path(cache, part, sizeof part);
	int rc = media_grab(ffmpeg, src, part, GRID_CACHE_PX, media_is_url(src) ? 20000 : 8000, stop,
						info);
	cairo_surface_t *s = rc == 0 ? grabit_load_image_surface(part, "history") : NULL;
	if (s) {
		if (rename(part, cache) != 0) unlink(part);
		write_meta(id, info);
		return s;
	}
	unlink(part);
	if (rc > 0 && !media_is_url(src) && !(stop && atomic_load(stop))) grid_mark_none(id);
	return NULL;
}

struct media_job {
	atomic_int refs;
	atomic_bool stop;
	atomic_bool done;
	char *ffmpeg;
	char *path;
	int max;
	cairo_surface_t *out;
};

static void job_unref(struct media_job *j) {
	if (atomic_fetch_sub(&j->refs, 1) != 1) return;
	if (j->out) cairo_surface_destroy(j->out);
	free(j->ffmpeg);
	free(j->path);
	free(j);
}

static void *job_thread(void *arg) {
	struct media_job *j = arg;
	char base[PATH_MAX], part[PATH_MAX + 16];
	snprintf(base, sizeof base, "%s/preview.png", history_thumb_dir());
	if (paths_mkdir_p(history_thumb_dir()) == 0) {
		grid_temp_path(base, part, sizeof part);
		struct media_info info;
		if (media_grab(j->ffmpeg, j->path, part, j->max, 8000, &j->stop, &info) == 0)
			j->out = grabit_load_image_surface(part, "upload");
		unlink(part);
	}
	atomic_store(&j->done, true);
	job_unref(j);
	grid_thread_leave();
	return NULL;
}

struct media_job *media_job_start(const char *ffmpeg, const char *path, int max) {
	struct media_job *j = calloc(1, sizeof *j);
	if (!j) return NULL;
	atomic_store(&j->refs, 2);
	j->ffmpeg = strdup(ffmpeg);
	j->path = strdup(path);
	j->max = max;
	pthread_t th;
	grid_thread_enter();
	if (!j->ffmpeg || !j->path || pthread_create(&th, NULL, job_thread, j) != 0) {
		grid_thread_leave();
		atomic_store(&j->refs, 1);
		job_unref(j);
		return NULL;
	}
	pthread_detach(th);
	return j;
}

bool media_job_take(struct media_job *j, cairo_surface_t **out) {
	if (!atomic_load(&j->done)) return false;
	*out = j->out;
	j->out = NULL;
	return true;
}

void media_job_end(struct media_job *j) {
	if (!j) return;
	atomic_store(&j->stop, true);
	job_unref(j);
}

struct media_player {
	atomic_int refs;
	atomic_bool stop;
	atomic_bool paused;
	atomic_bool failed;
	pthread_mutex_t mu;
	pthread_cond_t cv;
	pid_t pid;
	char *ffmpeg;
	char *id;
	char *src;
	bool alpha;
	int box_w;
	int box_h;
	int w;
	int h;
	unsigned char *ready;
	unsigned char *back;
	unsigned seq;
	unsigned shown;
	struct media_info info;
};

static void player_unref(struct media_player *p) {
	if (atomic_fetch_sub(&p->refs, 1) != 1) return;
	free(p->ready);
	free(p->back);
	free(p->ffmpeg);
	free(p->id);
	free(p->src);
	pthread_cond_destroy(&p->cv);
	pthread_mutex_destroy(&p->mu);
	free(p);
}

static bool read_full(int fd, unsigned char *buf, size_t n) {
	size_t got = 0;
	while (got < n) {
		ssize_t r = read(fd, buf + got, n - got);
		if (r < 0 && errno == EINTR) continue;
		if (r <= 0) return false;
		got += (size_t)r;
	}
	return true;
}

static void premultiply(unsigned char *px, size_t n) {
	for (size_t i = 0; i < n; i++, px += 4) {
		unsigned a = px[3];
		if (a == 255) continue;
		px[0] = (unsigned char)((px[0] * a + 127) / 255);
		px[1] = (unsigned char)((px[1] * a + 127) / 255);
		px[2] = (unsigned char)((px[2] * a + 127) / 255);
	}
}

static int64_t ts_ms(const struct timespec *t) {
	return (int64_t)t->tv_sec * 1000 + t->tv_nsec / 1000000;
}

static void pace(struct media_player *p, struct timespec *next) {
	next->tv_nsec += 1000000000L / GRID_PLAY_FPS;
	if (next->tv_nsec >= 1000000000L) {
		next->tv_sec++;
		next->tv_nsec -= 1000000000L;
	}
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	if (ts_ms(&now) - ts_ms(next) > 250) *next = now;
	pthread_mutex_lock(&p->mu);
	while (!atomic_load(&p->stop)) {
		if (atomic_load(&p->paused)) {
			pthread_cond_wait(&p->cv, &p->mu);
			clock_gettime(CLOCK_MONOTONIC, next);
			continue;
		}
		if (pthread_cond_timedwait(&p->cv, &p->mu, next) == ETIMEDOUT) break;
	}
	pthread_mutex_unlock(&p->mu);
}

static bool fit_size(struct media_player *p, struct media_info *info) {
	if (!read_meta(p->id, info) || info->w <= 0 || info->h <= 0) {
		cairo_surface_t *poster = media_poster(p->ffmpeg, p->id, p->src, &p->stop, info);
		if (poster && (info->w <= 0 || info->h <= 0)) {
			info->w = cairo_image_surface_get_width(poster);
			info->h = cairo_image_surface_get_height(poster);
		}
		if (poster) cairo_surface_destroy(poster);
	}
	if (info->w <= 0 || info->h <= 0) return false;
	double s = (double)p->box_w / info->w;
	if ((double)p->box_h / info->h < s) s = (double)p->box_h / info->h;
	if (s > 1) s = 1;
	int w = (int)(info->w * s) & ~1, h = (int)(info->h * s) & ~1;
	p->w = w < 2 ? 2 : w;
	p->h = h < 2 ? 2 : h;
	return true;
}

static void play(struct media_player *p) {
	struct media_info info;
	if (!fit_size(p, &info)) return;
	size_t frame = (size_t)p->w * (size_t)p->h * 4;
	unsigned char *a = malloc(frame), *b = malloc(frame);
	if (!a || !b) {
		free(a);
		free(b);
		return;
	}
	pthread_mutex_lock(&p->mu);
	p->ready = a;
	p->back = b;
	p->info = info;
	pthread_mutex_unlock(&p->mu);
	char vf[128], fsrc[PATH_MAX + 8];
	snprintf(vf, sizeof vf, "fps=%d,scale=%d:%d:flags=bilinear,format=bgra", GRID_PLAY_FPS, p->w,
			 p->h);
	const char *const local[] = {"-loglevel", "error", "-stream_loop", "-1", NULL};
	const char *const pre[] = {"-loglevel", "error", NULL};
	const char *const tail[] = {"-map", "0:v:0", "-an", "-sn", "-vf", vf, "-f", "rawvideo",
								"-pix_fmt", "bgra", "pipe:1", NULL};
	char *argv[MAX_ARGS];
	ff_argv(argv, p->ffmpeg, media_is_url(p->src) ? pre : local, p->src, fsrc, sizeof fsrc, tail);
	struct timespec next;
	clock_gettime(CLOCK_MONOTONIC, &next);
	while (!atomic_load(&p->stop)) {
		int op[2];
		if (pipe2(op, O_CLOEXEC) != 0) return;
		pid_t pid = spawn_ff(argv, op[1], -1);
		close(op[1]);
		if (pid < 0) {
			close(op[0]);
			return;
		}
		pthread_mutex_lock(&p->mu);
		p->pid = pid;
		if (atomic_load(&p->stop)) kill(pid, SIGTERM);
		pthread_mutex_unlock(&p->mu);
		size_t frames = 0;
		while (!atomic_load(&p->stop) && read_full(op[0], p->back, frame)) {
			if (p->alpha) premultiply(p->back, (size_t)p->w * (size_t)p->h);
			pthread_mutex_lock(&p->mu);
			unsigned char *t = p->ready;
			p->ready = p->back;
			p->back = t;
			p->seq++;
			pthread_mutex_unlock(&p->mu);
			frames++;
			pace(p, &next);
		}
		close(op[0]);
		pthread_mutex_lock(&p->mu);
		p->pid = 0;
		pthread_mutex_unlock(&p->mu);
		stop_reap(pid);
		if (frames == 0) return;
	}
}

static void *play_thread(void *arg) {
	struct media_player *p = arg;
	play(p);
	if (!atomic_load(&p->stop)) atomic_store(&p->failed, true);
	player_unref(p);
	grid_thread_leave();
	return NULL;
}

struct media_player *player_start(const char *ffmpeg, const char *id, const char *src, int box_w,
								  int box_h) {
	struct media_player *p = calloc(1, sizeof *p);
	if (!p) return NULL;
	pthread_mutex_init(&p->mu, NULL);
	pthread_condattr_t ca;
	pthread_condattr_init(&ca);
	pthread_condattr_setclock(&ca, CLOCK_MONOTONIC);
	pthread_cond_init(&p->cv, &ca);
	pthread_condattr_destroy(&ca);
	atomic_store(&p->refs, 2);
	p->ffmpeg = strdup(ffmpeg);
	p->id = strdup(id);
	p->src = strdup(src);
	p->alpha = ends_with(src, ".gif");
	p->box_w = box_w > 64 ? box_w : 64;
	p->box_h = box_h > 64 ? box_h : 64;
	p->info.secs = -1;
	pthread_t th;
	grid_thread_enter();
	if (!p->ffmpeg || !p->id || !p->src || pthread_create(&th, NULL, play_thread, p) != 0) {
		grid_thread_leave();
		atomic_store(&p->refs, 1);
		player_unref(p);
		return NULL;
	}
	pthread_detach(th);
	return p;
}

bool player_frame(struct media_player *p, cairo_surface_t **surf) {
	pthread_mutex_lock(&p->mu);
	if (!p->ready || p->seq == p->shown) {
		pthread_mutex_unlock(&p->mu);
		return false;
	}
	if (!*surf || cairo_image_surface_get_width(*surf) != p->w ||
		cairo_image_surface_get_height(*surf) != p->h) {
		if (*surf) cairo_surface_destroy(*surf);
		*surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, p->w, p->h);
	}
	bool ok = cairo_surface_status(*surf) == CAIRO_STATUS_SUCCESS;
	if (ok) {
		cairo_surface_flush(*surf);
		unsigned char *d = cairo_image_surface_get_data(*surf);
		int stride = cairo_image_surface_get_stride(*surf);
		size_t row = (size_t)p->w * 4;
		for (int y = 0; y < p->h; y++)
			memcpy(d + (size_t)y * (size_t)stride, p->ready + (size_t)y * row, row);
		cairo_surface_mark_dirty(*surf);
	}
	p->shown = p->seq;
	pthread_mutex_unlock(&p->mu);
	return ok;
}

void player_toggle(struct media_player *p) {
	pthread_mutex_lock(&p->mu);
	atomic_store(&p->paused, !atomic_load(&p->paused));
	pthread_cond_broadcast(&p->cv);
	pthread_mutex_unlock(&p->mu);
}

bool player_paused(const struct media_player *p) {
	return atomic_load(&p->paused);
}

bool player_failed(const struct media_player *p) {
	return atomic_load(&p->failed);
}

int player_secs(struct media_player *p) {
	pthread_mutex_lock(&p->mu);
	int s = p->info.secs;
	pthread_mutex_unlock(&p->mu);
	return s;
}

void player_stop(struct media_player *p) {
	if (!p) return;
	atomic_store(&p->stop, true);
	pthread_mutex_lock(&p->mu);
	if (p->pid > 0) kill(p->pid, SIGTERM);
	pthread_cond_broadcast(&p->cv);
	pthread_mutex_unlock(&p->mu);
	player_unref(p);
}
