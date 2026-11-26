// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _GNU_SOURCE
#include "ui/settings_grid.h"

#include "capture/save.h"
#include "paths.h"
#include "version.h"

#include <curl/curl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#define FETCH_MAX (40L * 1024 * 1024)
#define KEEP_MIN 240

bool grid_is_image(const char *path) {
	const char *dot = path ? strrchr(path, '.') : NULL;
	return dot && (strcasecmp(dot, ".png") == 0 || strcasecmp(dot, ".jpg") == 0 ||
				   strcasecmp(dot, ".jpeg") == 0 || strcasecmp(dot, ".webp") == 0);
}

cairo_surface_t *grid_fit(cairo_surface_t *src, int max) {
	int w = cairo_image_surface_get_width(src), h = cairo_image_surface_get_height(src);
	if (w <= 0 || h <= 0) return NULL;
	double s = (double)max / (w > h ? w : h);
	if (s > 1) s = 1;
	int nw = (int)(w * s + 0.5), nh = (int)(h * s + 0.5);
	cairo_surface_t *out =
		cairo_image_surface_create(CAIRO_FORMAT_ARGB32, nw > 0 ? nw : 1, nh > 0 ? nh : 1);
	cairo_t *cr = cairo_create(out);
	cairo_scale(cr, s, s);
	cairo_set_source_surface(cr, src, 0, 0);
	cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
	cairo_paint(cr);
	cairo_destroy(cr);
	return out;
}

cairo_surface_t *grid_load_fit(const char *path, int max) {
	cairo_surface_t *full = grabit_load_image_surface(path, "history");
	if (!full) return NULL;
	cairo_surface_t *out =
		cairo_surface_status(full) == CAIRO_STATUS_SUCCESS ? grid_fit(full, max) : NULL;
	cairo_surface_destroy(full);
	return out;
}

struct fetch {
	FILE *f;
	long n;
	bool io;
	atomic_bool *stop;
};

static size_t fetch_write(char *p, size_t size, size_t nmemb, void *ud) {
	struct fetch *x = ud;
	size_t t = size * nmemb;
	if (x->n + (long)t > FETCH_MAX) return 0;
	x->n += (long)t;
	size_t w = fwrite(p, 1, t, x->f);
	if (w < t) x->io = true;
	return w;
}

static int fetch_progress(void *ud, curl_off_t a, curl_off_t b, curl_off_t c, curl_off_t d) {
	(void)a;
	(void)b;
	(void)c;
	(void)d;
	return atomic_load(((struct fetch *)ud)->stop) ? 1 : 0;
}

static bool image_type(const char *ctype) {
	return ctype && (strncasecmp(ctype, "image/png", 9) == 0 ||
					 strncasecmp(ctype, "image/jpeg", 10) == 0 ||
					 strncasecmp(ctype, "image/webp", 10) == 0);
}

static void curl_init(void) {
	curl_global_init(CURL_GLOBAL_DEFAULT);
}

int grid_fetch(atomic_bool *stop, const char *url, const char *dest) {
	static pthread_once_t once = PTHREAD_ONCE_INIT;
	pthread_once(&once, curl_init);
	CURL *curl = curl_easy_init();
	FILE *f = curl ? fopen(dest, "wbe") : NULL;
	if (!f) {
		if (curl) curl_easy_cleanup(curl);
		return -1;
	}
	struct fetch x = {.f = f, .stop = stop};
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fetch_write);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &x);
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, fetch_progress);
	curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &x);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "grabit/" GRABIT_VERSION);
#if LIBCURL_VERSION_NUM >= 0x075500
	curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
	curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
#endif
	CURLcode rc = curl_easy_perform(curl);
	long code = 0;
	char *ctype = NULL;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
	curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &ctype);
	bool image = image_type(ctype);
	curl_easy_cleanup(curl);
	if (fclose(f) != 0) x.io = true;
	if (rc == CURLE_WRITE_ERROR || x.io) return x.io ? -1 : 1;
	if (rc != CURLE_OK) return -1;
	if (code >= 200 && code < 300) return image ? 0 : 1;
	return code >= 400 && code < 500 && code != 408 && code != 429 ? 1 : -1;
}

static cairo_surface_t *thumb_of(int px, cairo_surface_t *src) {
	if (!src) return NULL;
	int w = cairo_image_surface_get_width(src), h = cairo_image_surface_get_height(src);
	int shortest = w < h ? w : h, longest = w > h ? w : h;
	int max = shortest > 0 ? (int)((double)px * longest / shortest) : px;
	if (max > px * 4) max = px * 4;
	cairo_surface_t *out = grid_fit(src, max);
	cairo_surface_destroy(src);
	return out;
}

void grid_mark_none(const char *id) {
	char path[PATH_MAX];
	history_thumb_path(id, ".none", path, sizeof path);
	FILE *f = path[0] ? fopen(path, "we") : NULL;
	if (f) fclose(f);
}

static cairo_surface_t *remote(struct grid_thumbs *t, const char *id, const char *url) {
	char cache[PATH_MAX], part[PATH_MAX + 16];
	history_thumb_path(id, ".png", cache, sizeof cache);
	if (!cache[0]) return NULL;
	if (access(cache, R_OK) == 0) {
		cairo_surface_t *s = grabit_load_image_surface(cache, "history");
		if (s) return thumb_of(t->px, s);
		unlink(cache);
	}
	history_thumb_path(id, ".none", part, sizeof part);
	if (access(part, F_OK) == 0 || paths_mkdir_p(history_thumb_dir()) != 0) return NULL;
	grid_temp_path(cache, part, sizeof part);
	int rc = grid_fetch(&t->stop, url, part);
	cairo_surface_t *big = rc == 0 ? grid_load_fit(part, GRID_CACHE_PX) : NULL;
	unlink(part);
	if (!big) {
		if (rc > 0 && !atomic_load(&t->stop)) grid_mark_none(id);
		return NULL;
	}
	grid_temp_path(cache, part, sizeof part);
	if (grabit_save_png_surface(big, part, 6) == 0 && rename(part, cache) != 0) unlink(part);
	return thumb_of(t->px, big);
}

void grid_temp_path(const char *base, char *out, size_t n) {
	static atomic_uint counter;
	snprintf(out, n, "%s.%d.%u.part", base, (int)getpid(), atomic_fetch_add(&counter, 1));
}

static size_t pick(struct grid_thumbs *t, bool net) {
	for (size_t k = 0; k < t->n_want; k++) {
		size_t i = t->want[k];
		if (i >= t->n || atomic_load(&t->state[i]) != TH_NONE) continue;
		if ((net ? t->urls[i] : t->paths[i]) != NULL) return i;
	}
	return SET_NONE;
}

static atomic_int g_live;

void grid_thread_enter(void) {
	atomic_fetch_add(&g_live, 1);
}

void grid_thread_leave(void) {
	atomic_fetch_sub(&g_live, 1);
}

void grid_threads_wait(int max_ms) {
	for (int waited = 0; atomic_load(&g_live) > 0 && waited < max_ms; waited += 5)
		usleep(5000);
}

static void destroy(struct grid_thumbs *t) {
	for (size_t i = 0; i < t->n; i++) {
		free(t->ids[i]);
		free(t->paths[i]);
		free(t->urls[i]);
		if (t->thumbs[i]) cairo_surface_destroy(t->thumbs[i]);
	}
	free(t->ids);
	free(t->paths);
	free(t->urls);
	free(t->media);
	free(t->secs);
	free(t->ffmpeg);
	free(t->state);
	free(t->thumbs);
	free(t->want);
	pthread_cond_destroy(&t->cv);
	pthread_mutex_destroy(&t->mu);
	free(t);
}

static void unref(struct grid_thumbs *t) {
	if (atomic_fetch_sub(&t->refs, 1) == 1) destroy(t);
}

static void *worker(struct grid_thumbs *t, bool net) {
	pthread_mutex_lock(&t->mu);
	while (!t->quit) {
		size_t i = pick(t, net);
		if (i == SET_NONE) {
			pthread_cond_wait(&t->cv, &t->mu);
			continue;
		}
		atomic_store(&t->state[i], TH_LOADING);
		pthread_mutex_unlock(&t->mu);
		struct media_info info = {.secs = -1};
		cairo_surface_t *s;
		if (t->media[i])
			s = thumb_of(t->px, media_poster(t->ffmpeg, t->ids[i], net ? t->urls[i] : t->paths[i],
											 &t->stop, &info));
		else if (net)
			s = remote(t, t->ids[i], t->urls[i]);
		else
			s = thumb_of(t->px, grabit_load_image_surface(t->paths[i], "history"));
		pthread_mutex_lock(&t->mu);
		t->thumbs[i] = s;
		t->secs[i] = info.secs;
		atomic_store(&t->state[i], TH_DONE);
		atomic_fetch_add(&t->finished, 1);
	}
	pthread_mutex_unlock(&t->mu);
	unref(t);
	grid_thread_leave();
	return NULL;
}

static void *local_thread(void *arg) {
	return worker(arg, false);
}

static void *net_thread(void *arg) {
	return worker(arg, true);
}

static cairo_surface_t *take(struct grid_thumbs *old, const char *id, int *secs) {
	for (size_t i = 0; old && i < old->n; i++) {
		if (!old->thumbs[i] || atomic_load(&old->state[i]) != TH_DONE || strcmp(old->ids[i], id) != 0)
			continue;
		cairo_surface_t *s = old->thumbs[i];
		*secs = old->secs[i];
		old->thumbs[i] = NULL;
		atomic_store(&old->state[i], TH_NONE);
		return s;
	}
	return NULL;
}

struct grid_thumbs *thumbs_new(const struct history_list *h, struct grid_thumbs *old, int px,
							   const char *ffmpeg) {
	struct grid_thumbs *t = calloc(1, sizeof *t);
	if (!t) return NULL;
	pthread_mutex_init(&t->mu, NULL);
	pthread_cond_init(&t->cv, NULL);
	atomic_store(&t->refs, 1);
	size_t n = h->n;
	t->px = px;
	t->ids = calloc(n ? n : 1, sizeof *t->ids);
	t->paths = calloc(n ? n : 1, sizeof *t->paths);
	t->urls = calloc(n ? n : 1, sizeof *t->urls);
	t->state = calloc(n ? n : 1, sizeof *t->state);
	t->thumbs = calloc(n ? n : 1, sizeof *t->thumbs);
	t->media = calloc(n ? n : 1, sizeof *t->media);
	t->secs = calloc(n ? n : 1, sizeof *t->secs);
	t->ffmpeg = ffmpeg ? strdup(ffmpeg) : NULL;
	if (!t->ids || !t->paths || !t->urls || !t->state || !t->thumbs || !t->media || !t->secs)
		return t;
	t->n = n;
	bool reuse = old && old->px == px;
	if (reuse) pthread_mutex_lock(&old->mu);
	for (size_t i = 0; i < n; i++) {
		const struct history_entry *e = &h->v[i];
		t->ids[i] = strdup(e->id);
		t->secs[i] = -1;
		t->thumbs[i] = reuse ? take(old, e->id, &t->secs[i]) : NULL;
		bool file = history_has_file(e), media = media_entry(e);
		const char *src = media && t->ffmpeg ? media_source(e) : NULL;
		t->media[i] = src != NULL;
		if (src)
			*(file ? &t->paths[i] : &t->urls[i]) = strdup(src);
		else if (!media && file && grid_is_image(e->path))
			t->paths[i] = strdup(e->path);
		else if (!media && !file && e->kind == HISTORY_UPLOAD && e->url)
			t->urls[i] = strdup(e->url);
		enum grid_thumb_state s = t->thumbs[i]				  ? TH_DONE
								  : t->paths[i] || t->urls[i] ? TH_NONE
															  : TH_SKIP;
		atomic_store(&t->state[i], s);
	}
	if (reuse) pthread_mutex_unlock(&old->mu);
	return t;
}

static bool spawn(struct grid_thumbs *t, void *(*fn)(void *)) {
	pthread_t th;
	atomic_fetch_add(&t->refs, 1);
	grid_thread_enter();
	if (pthread_create(&th, NULL, fn, t) != 0) {
		atomic_fetch_sub(&t->refs, 1);
		grid_thread_leave();
		return false;
	}
	pthread_detach(th);
	return true;
}

void thumbs_start(struct grid_thumbs *t) {
	if (!t || t->running || t->n == 0) return;
	bool a = spawn(t, local_thread);
	bool b = spawn(t, net_thread);
	t->running = a || b;
}

void thumbs_release(struct grid_thumbs *t) {
	if (!t) return;
	pthread_mutex_lock(&t->mu);
	t->quit = true;
	atomic_store(&t->stop, true);
	pthread_cond_broadcast(&t->cv);
	pthread_mutex_unlock(&t->mu);
	unref(t);
}

static bool wanted(const struct grid_thumbs *t, size_t i) {
	for (size_t k = 0; k < t->n_want; k++)
		if (t->want[k] == i) return true;
	return false;
}

void thumbs_want(struct grid_thumbs *t, const size_t *idx, size_t n) {
	if (!t || !t->running) return;
	pthread_mutex_lock(&t->mu);
	if (n > t->cap_want) {
		size_t *v = realloc(t->want, n * sizeof *v);
		if (!v) {
			pthread_mutex_unlock(&t->mu);
			return;
		}
		t->want = v;
		t->cap_want = n;
	}
	if (n) memcpy(t->want, idx, n * sizeof *idx);
	t->n_want = n;
	size_t loaded = 0;
	for (size_t i = 0; i < t->n; i++)
		loaded += atomic_load(&t->state[i]) == TH_DONE && t->thumbs[i];
	size_t keep = n * 2 > KEEP_MIN ? n * 2 : KEEP_MIN;
	for (size_t i = 0; i < t->n && loaded > keep; i++) {
		if (atomic_load(&t->state[i]) != TH_DONE || !t->thumbs[i] || wanted(t, i)) continue;
		cairo_surface_destroy(t->thumbs[i]);
		t->thumbs[i] = NULL;
		atomic_store(&t->state[i], TH_NONE);
		loaded--;
	}
	pthread_cond_broadcast(&t->cv);
	pthread_mutex_unlock(&t->mu);
}

cairo_surface_t *thumbs_get(struct grid_thumbs *t, size_t i) {
	return t && i < t->n && atomic_load(&t->state[i]) == TH_DONE ? t->thumbs[i] : NULL;
}

bool thumbs_pending(struct grid_thumbs *t, size_t i) {
	if (!t || i >= t->n) return false;
	unsigned char s = atomic_load(&t->state[i]);
	return s == TH_NONE || s == TH_LOADING;
}

bool thumbs_poll(struct grid_thumbs *t) {
	if (!t) return false;
	size_t done = atomic_load(&t->finished);
	if (done == t->seen) return false;
	t->seen = done;
	return true;
}

bool thumbs_busy(struct grid_thumbs *t) {
	if (!t || !t->running) return false;
	for (size_t k = 0; k < t->n_want; k++)
		if (thumbs_pending(t, t->want[k])) return true;
	return false;
}

int thumbs_secs(struct grid_thumbs *t, size_t i) {
	return t && i < t->n && atomic_load(&t->state[i]) == TH_DONE ? t->secs[i] : -1;
}

int thumbs_px(const struct grid_thumbs *t) {
	return t ? t->px : 0;
}
