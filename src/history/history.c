// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "history/history.h"

#include "config/config.h"
#include "log.h"
#include "paths.h"
#include "upload/upload.h"
#include "util/util.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <time.h>
#include <unistd.h>

#include <json-c/json.h>

static const char *const KIND_NAMES[] = {
	[HISTORY_UPLOAD] = "upload",
	[HISTORY_FILE] = "file",
	[HISTORY_OCR] = "ocr",
};

const char *history_kind_name(enum history_kind kind) {
	return kind == HISTORY_UPLOAD || kind == HISTORY_FILE || kind == HISTORY_OCR
			   ? KIND_NAMES[kind]
			   : "?";
}

static enum history_kind kind_from(const char *s) {
	for (enum history_kind k = HISTORY_UPLOAD; k <= HISTORY_OCR; k <<= 1)
		if (s && strcmp(s, KIND_NAMES[k]) == 0) return k;
	return 0;
}

const char *history_file(void) {
	static char path[PATH_MAX];
	snprintf(path, sizeof path, "%s/history.jsonl", paths_state_dir());
	return path;
}

static char g_thumb_dir[PATH_MAX];

static void find_thumb_dir(void) {
	const char *xdg = getenv("XDG_CACHE_HOME"), *home = getenv("HOME");
	if (xdg && xdg[0] == '/')
		snprintf(g_thumb_dir, sizeof g_thumb_dir, "%s/grabit/history", xdg);
	else
		snprintf(g_thumb_dir, sizeof g_thumb_dir, "%s/.cache/grabit/history", home ? home : "");
}

const char *history_thumb_dir(void) {
	static pthread_once_t once = PTHREAD_ONCE_INIT;
	pthread_once(&once, find_thumb_dir);
	return g_thumb_dir;
}

static bool valid_id(const char *id) {
	if (!id || !id[0] || id[0] == '.') return false;
	for (const char *p = id; *p; p++)
		if (!isalnum((unsigned char)*p) && *p != '-' && *p != '_' && *p != '.') return false;
	return strlen(id) < 128;
}

void history_thumb_path(const char *id, const char *ext, char *out, size_t n) {
	int w = valid_id(id) ? snprintf(out, n, "%s/%s%s", history_thumb_dir(), id, ext) : -1;
	if (n && (w < 0 || (size_t)w >= n)) out[0] = '\0';
}

static void drop_thumbs(const char *id) {
	static const char *const EXTS[] = {".png", ".none", ".orig", ".part", ".orig.part"};
	char path[PATH_MAX];
	for (size_t i = 0; i < sizeof EXTS / sizeof *EXTS; i++) {
		history_thumb_path(id, EXTS[i], path, sizeof path);
		if (path[0]) unlink(path);
	}
}

int64_t history_stamp(void) {
	return grabit_file_mtime_ns(history_file());
}

static int lock_open(void) {
	if (paths_mkdir_p(paths_state_dir()) != 0) return -1;
	char path[PATH_MAX];
	snprintf(path, sizeof path, "%s/history.lock", paths_state_dir());
	int fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
	if (fd < 0) {
		log_error("history: cannot open %s: %s", path, strerror(errno));
		return -1;
	}
	while (flock(fd, LOCK_EX) != 0)
		if (errno != EINTR) {
			close(fd);
			return -1;
		}
	return fd;
}

static void lock_close(int fd) {
	if (fd < 0) return;
	flock(fd, LOCK_UN);
	close(fd);
}

bool history_enabled(struct config *cfg, enum history_kind kind) {
	const char *on = config_get(cfg, "history.enabled");
	if (on && strcmp(on, "false") == 0) return false;
	if (kind != HISTORY_OCR) return true;
	const char *ocr = config_get(cfg, "history.ocr");
	return !ocr || strcmp(ocr, "false") != 0;
}

static void put_str(struct json_object *o, const char *key, const char *val) {
	if (val && val[0]) json_object_object_add(o, key, json_object_new_string(val));
}

char *history_entry_json(const struct history_entry *e) {
	struct json_object *o = json_object_new_object();
	if (!o) return NULL;
	put_str(o, "id", e->id);
	json_object_object_add(o, "time", json_object_new_int64(e->time));
	put_str(o, "kind", history_kind_name(e->kind));
	if (e->video) json_object_object_add(o, "video", json_object_new_boolean(1));
	put_str(o, "path", e->path);
	put_str(o, "url", e->url);
	put_str(o, "service", e->service);
	put_str(o, "delete_url", e->delete_url);
	if (e->delete_auth) json_object_object_add(o, "delete_auth", json_object_new_boolean(1));
	if (e->external) json_object_object_add(o, "external", json_object_new_boolean(1));
	put_str(o, "text", e->text);
	put_str(o, "lang", e->lang);
	put_str(o, "target", e->target);
	const char *s = json_object_to_json_string_ext(o, JSON_C_TO_STRING_PLAIN | JSON_C_TO_STRING_NOSLASHESCAPE);
	char *out = s ? strdup(s) : NULL;
	json_object_put(o);
	return out;
}

static char *get_str(struct json_object *o, const char *key) {
	struct json_object *v = NULL;
	if (!json_object_object_get_ex(o, key, &v) || !json_object_is_type(v, json_type_string))
		return NULL;
	return strdup(json_object_get_string(v));
}

static bool get_bool(struct json_object *o, const char *key) {
	struct json_object *v = NULL;
	return json_object_object_get_ex(o, key, &v) && json_object_get_boolean(v);
}

static void entry_free(struct history_entry *e) {
	free(e->id);
	free(e->path);
	free(e->url);
	free(e->service);
	free(e->delete_url);
	free(e->text);
	free(e->lang);
	free(e->target);
	*e = (struct history_entry){0};
}

static bool entry_parse(const char *line, struct history_entry *e) {
	*e = (struct history_entry){0};
	struct json_object *o = json_tokener_parse(line);
	if (!o) return false;
	char *kind = get_str(o, "kind");
	e->kind = kind_from(kind);
	free(kind);
	e->id = get_str(o, "id");
	struct json_object *t = NULL;
	if (json_object_object_get_ex(o, "time", &t)) e->time = json_object_get_int64(t);
	e->video = get_bool(o, "video");
	e->delete_auth = get_bool(o, "delete_auth");
	e->external = get_bool(o, "external");
	e->path = get_str(o, "path");
	e->url = get_str(o, "url");
	e->service = get_str(o, "service");
	e->delete_url = get_str(o, "delete_url");
	e->text = get_str(o, "text");
	e->lang = get_str(o, "lang");
	e->target = get_str(o, "target");
	json_object_put(o);
	if (e->kind && valid_id(e->id)) return true;
	entry_free(e);
	return false;
}

int history_add(struct config *cfg, struct history_entry *e) {
	if (!history_enabled(cfg, e->kind)) return 0;
	static unsigned counter;
	char id[64];
	if (!e->time) e->time = (int64_t)time(NULL);
	snprintf(id, sizeof id, "%lld-%d-%u", (long long)e->time, (int)getpid(), counter++);
	char *saved_id = e->id;
	if (!e->id) e->id = id;
	char *line = history_entry_json(e);
	e->id = saved_id;
	if (!line) return -1;
	int lock = lock_open();
	int rc = -1;
	if (lock >= 0) {
		int fd = open(history_file(), O_RDWR | O_APPEND | O_CREAT | O_CLOEXEC, 0600);
		if (fd >= 0) {
			struct grabit_buf b = {0};
			char last = '\n';
			off_t end = lseek(fd, 0, SEEK_END);
			if (end > 0 && pread(fd, &last, 1, end - 1) == 1 && last != '\n') grabit_buf_putc(&b, '\n');
			grabit_buf_puts(&b, line);
			grabit_buf_putc(&b, '\n');
			size_t done = 0;
			while (b.data && done < b.len) {
				ssize_t w = write(fd, b.data + done, b.len - done);
				if (w < 0 && errno == EINTR) continue;
				if (w <= 0) break;
				done += (size_t)w;
			}
			rc = b.data && done == b.len ? 0 : -1;
			grabit_buf_free(&b);
			close(fd);
		}
		if (rc != 0) log_warn("history: could not write %s: %s", history_file(), strerror(errno));
	}
	lock_close(lock);
	free(line);
	return rc;
}

static void each_line(void (*fn)(char *line, void *ud), void *ud) {
	FILE *f = fopen(history_file(), "re");
	if (!f) return;
	char *line = NULL;
	size_t cap = 0;
	ssize_t n;
	while ((n = getline(&line, &cap, f)) > 0) {
		while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
			line[--n] = '\0';
		if (n > 0) fn(line, ud);
	}
	free(line);
	fclose(f);
}

struct load_ctx {
	unsigned kinds;
	struct history_list *out;
	size_t cap;
	bool oom;
};

static void load_line(char *line, void *ud) {
	struct load_ctx *c = ud;
	struct history_entry e;
	if (c->oom || !entry_parse(line, &e)) return;
	if (!(e.kind & c->kinds)) {
		entry_free(&e);
		return;
	}
	if (c->out->n == c->cap) {
		size_t cap = c->cap ? c->cap * 2 : 64;
		struct history_entry *v = realloc(c->out->v, cap * sizeof *v);
		if (!v) {
			c->oom = true;
			entry_free(&e);
			return;
		}
		c->out->v = v;
		c->cap = cap;
	}
	c->out->v[c->out->n++] = e;
}

int history_load(unsigned kinds, struct history_list *out) {
	*out = (struct history_list){0};
	struct load_ctx c = {.kinds = kinds, .out = out};
	each_line(load_line, &c);
	if (c.oom) {
		history_list_free(out);
		return -1;
	}
	for (size_t i = 0; i < out->n / 2; i++) {
		struct history_entry t = out->v[i];
		out->v[i] = out->v[out->n - 1 - i];
		out->v[out->n - 1 - i] = t;
	}
	return 0;
}

void history_list_free(struct history_list *l) {
	for (size_t i = 0; i < l->n; i++)
		entry_free(&l->v[i]);
	free(l->v);
	*l = (struct history_list){0};
}

const struct history_entry *history_find(const struct history_list *l, const char *ref) {
	if (!ref || !ref[0]) return NULL;
	for (size_t i = 0; i < l->n; i++)
		if (strcmp(l->v[i].id, ref) == 0) return &l->v[i];
	bool digits = true;
	for (const char *p = ref; *p && digits; p++)
		digits = isdigit((unsigned char)*p);
	if (digits) {
		long n = strtol(ref, NULL, 10);
		return n >= 1 && (size_t)n <= l->n ? &l->v[n - 1] : NULL;
	}
	for (size_t i = 0; i < l->n; i++)
		if (strcmp(l->v[i].id, ref) == 0) return &l->v[i];
	return NULL;
}

struct filter_ctx {
	const char *id;
	unsigned kinds;
	struct grabit_buf keep;
	struct grabit_buf *taken;
	const char *replace;
	struct grabit_buf gone;
	size_t dropped;
	size_t at;
};

static void filter_line(char *line, void *ud) {
	struct filter_ctx *c = ud;
	struct history_entry e;
	bool drop = false;
	size_t at = c->at++;
	if (entry_parse(line, &e)) {
		drop = c->id ? strcmp(e.id, c->id) == 0 : (e.kind & c->kinds) != 0;
		if (drop && !c->replace && !c->taken) {
			grabit_buf_puts(&c->gone, e.id);
			grabit_buf_putc(&c->gone, '\n');
		}
		entry_free(&e);
	}
	if (drop && c->replace) {
		c->dropped++;
		grabit_buf_puts(&c->keep, c->replace);
		grabit_buf_putc(&c->keep, '\n');
		return;
	}
	if (drop) {
		c->dropped++;
		if (c->taken) {
			char pos[32];
			snprintf(pos, sizeof pos, "%zu\t", at);
			grabit_buf_puts(c->taken, pos);
			grabit_buf_puts(c->taken, line);
			grabit_buf_putc(c->taken, '\n');
		}
		return;
	}
	grabit_buf_puts(&c->keep, line);
	grabit_buf_putc(&c->keep, '\n');
}

static int rewrite(struct filter_ctx *c) {
	int lock = lock_open();
	if (lock < 0) return -1;
	each_line(filter_line, c);
	int rc = 0;
	if (c->dropped) {
		rc = paths_atomic_write(history_file(), c->keep.data ? c->keep.data : "", c->keep.len);
		if (rc != 0) log_error("history: could not rewrite %s", history_file());
	}
	lock_close(lock);
	char *save = NULL;
	for (char *id = rc == 0 && c->gone.data ? strtok_r(c->gone.data, "\n", &save) : NULL; id;
		 id = strtok_r(NULL, "\n", &save))
		drop_thumbs(id);
	grabit_buf_free(&c->gone);
	grabit_buf_free(&c->keep);
	return rc;
}

int history_take(const char *id, unsigned kinds, char **taken) {
	struct grabit_buf out = {0};
	struct filter_ctx c = {.id = id, .kinds = kinds, .taken = taken ? &out : NULL};
	int rc = rewrite(&c);
	if (taken) *taken = rc == 0 && c.dropped ? out.data : NULL;
	if (!taken || rc != 0 || !c.dropped) grabit_buf_free(&out);
	return rc != 0 ? -1 : (int)c.dropped;
}

int history_update(const struct history_entry *e) {
	char *line = history_entry_json(e);
	if (!line) return -1;
	struct filter_ctx c = {.id = e->id, .replace = line};
	int rc = rewrite(&c);
	free(line);
	return rc != 0 ? -1 : c.dropped ? 0
									: 1;
}

int history_remove(const char *id) {
	int n = history_take(id, 0, NULL);
	return n < 0 ? -1 : n ? 0
						  : 1;
}

int history_clear(unsigned kinds) {
	return history_take(NULL, kinds, NULL);
}

struct lines {
	char **v;
	size_t n;
	size_t cap;
};

static bool push_line(struct lines *l, size_t at, const char *line) {
	if (l->n == l->cap) {
		size_t cap = l->cap ? l->cap * 2 : 64;
		char **v = realloc(l->v, cap * sizeof *v);
		if (!v) return false;
		l->v = v;
		l->cap = cap;
	}
	char *copy = strdup(line);
	if (!copy) return false;
	if (at > l->n) at = l->n;
	memmove(&l->v[at + 1], &l->v[at], (l->n - at) * sizeof *l->v);
	l->v[at] = copy;
	l->n++;
	return true;
}

static void collect(char *line, void *ud) {
	struct lines *l = ud;
	push_line(l, l->n, line);
}

int history_restore(const char *taken) {
	if (!taken || !taken[0]) return 0;
	int lock = lock_open();
	if (lock < 0) return -1;
	struct lines l = {0};
	each_line(collect, &l);
	char *copy = strdup(taken), *save = NULL;
	for (char *t = copy ? strtok_r(copy, "\n", &save) : NULL; t; t = strtok_r(NULL, "\n", &save)) {
		char *tab = strchr(t, '\t');
		if (!tab) continue;
		push_line(&l, strtoul(t, NULL, 10), tab + 1);
	}
	free(copy);
	struct grabit_buf out = {0};
	for (size_t i = 0; i < l.n; i++) {
		grabit_buf_puts(&out, l.v[i]);
		grabit_buf_putc(&out, '\n');
		free(l.v[i]);
	}
	free(l.v);
	int rc = paths_atomic_write(history_file(), out.data ? out.data : "", out.len);
	grabit_buf_free(&out);
	lock_close(lock);
	return rc;
}

static char *absolute(const char *path) {
	char buf[PATH_MAX];
	return strdup(path && realpath(path, buf) ? buf : path ? path
														   : "");
}

void history_record_upload(struct config *cfg, const char *service, const char *kept_path,
						   bool external, bool video, const struct upload_result *r) {
	if (!history_enabled(cfg, HISTORY_UPLOAD) || !r->url) return;
	struct history_entry e = {
		.kind = HISTORY_UPLOAD,
		.video = video,
		.external = external && kept_path,
		.url = r->url,
		.service = (char *)service,
	};
	char *path = kept_path ? absolute(kept_path) : NULL;
	char *del = NULL;
	if (upload_delete_target(service, cfg, r, &del, &e.delete_auth) == 0) e.delete_url = del;
	e.path = path;
	history_add(cfg, &e);
	free(path);
	free(del);
}

void history_record_file(struct config *cfg, const char *path, bool video) {
	if (!path || !history_enabled(cfg, HISTORY_FILE)) return;
	char *abs = absolute(path);
	struct history_entry e = {.kind = HISTORY_FILE, .video = video, .path = abs};
	history_add(cfg, &e);
	free(abs);
}

void history_record_ocr(struct config *cfg, const char *text, const char *lang,
						const char *target) {
	if (!text || !text[0] || !history_enabled(cfg, HISTORY_OCR)) return;
	struct history_entry e = {
		.kind = HISTORY_OCR,
		.text = (char *)text,
		.lang = (char *)lang,
		.target = (char *)target,
	};
	history_add(cfg, &e);
}

bool history_can_delete_host(const struct history_entry *e) {
	return e->kind == HISTORY_UPLOAD && e->delete_url && e->delete_url[0];
}

bool history_has_file(const struct history_entry *e) {
	return e->kind != HISTORY_OCR && e->path && access(e->path, F_OK) == 0;
}

bool history_can_delete_file(const struct history_entry *e) {
	return !e->external && history_has_file(e);
}

bool history_host_keeps(const struct history_entry *e) {
	return e->kind == HISTORY_UPLOAD && !history_can_delete_host(e);
}

void history_delete_summary(const struct history_entry *e, unsigned what, char *out, size_t n) {
	const char *parts[3];
	char host[160], file[PATH_MAX + 16];
	size_t k = 0;
	if ((what & HISTORY_DELETE_HOST) && history_can_delete_host(e)) {
		snprintf(host, sizeof host, "delete it from %s", e->service ? e->service : "the host");
		parts[k++] = host;
	}
	if ((what & HISTORY_DELETE_FILE) && history_can_delete_file(e)) {
		const char *home = getenv("HOME");
		size_t hl = home ? strlen(home) : 0;
		bool under = hl > 1 && strncmp(e->path, home, hl) == 0 && e->path[hl] == '/';
		snprintf(file, sizeof file, "delete %s%s", under ? "~" : "", e->path + (under ? hl : 0));
		parts[k++] = file;
	}
	parts[k] = k ? "forget it" : "forget this entry";
	k++;
	struct grabit_buf b = {0};
	for (size_t i = 0; i < k; i++) {
		if (i) grabit_buf_puts(&b, i + 1 == k ? " and " : ", ");
		grabit_buf_puts(&b, parts[i]);
	}
	snprintf(out, n, "%s", b.data ? b.data : "");
	grabit_buf_free(&b);
}
