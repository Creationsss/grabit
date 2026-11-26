// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _GNU_SOURCE
#include "ui/settings_upload.h"

#include "clipboard/clipboard.h"
#include "log.h"
#include "mime.h"
#include "paths.h"
#include "ui/settings_grid.h"
#include "ui/window.h"
#include "upload/upload.h"
#include "util/util.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define QUEUE_KEY "grabit -f <file>"
#define SERVICE_KEY "grabit -f <file> --<service>"
#define ORIGINAL_NAME "x-zipline-original-name"
#define FILENAME "x-zipline-filename"

static const char *const EXPIRY[] = {"never", "5min", "10min", "15min", "30min", "1h", "2h",
									 "3h", "4h", "5h", "6h", "8h", "12h", "1d", "3d", "5d", "7d",
									 "1w", "1.5w", "2w", "3w", "30d", "45.625d", "60d", "90d",
									 "120d", "0.5 year", "1y", NULL};
static const char *const EXPIRY_LABELS[] = {
	"Never", "5 minutes", "10 minutes", "15 minutes", "30 minutes", "1 hour", "2 hours",
	"3 hours", "4 hours", "5 hours", "6 hours", "8 hours", "12 hours", "1 day", "3 days",
	"5 days", "7 days", "1 week", "1.5 weeks", "2 weeks", "3 weeks", "1 month (30 days)",
	"1.5 months (~45 days)", "2 months (60 days)", "3 months (90 days)", "4 months (120 days)",
	"6 months (0.5 year)", "1 year", NULL};
static const char *const FORMATS[] = {"random", "date", "uuid", "name", "gfycat", NULL};
static const char *const FORMAT_LABELS[] = {"Random", "Date", "UUID", "Use file name",
											"Gfycat-style name", NULL};
static const char *const COMPRESS[] = {"jpg", "png", "webp", "jxl", NULL};
static const char *const COMPRESS_LABELS[] = {".jpg", ".png", ".webp", ".jxl", NULL};

static const struct set_dep ONLY_ZIPLINE = {NULL, SERVICE_KEY, "zipline",
											"Only used for zipline uploads"};
static const struct set_dep ONLY_NEST = {NULL, SERVICE_KEY, "nest", "Only used for nest uploads"};

struct once_def {
	const char *cfg;
	const char *label;
	const char *hint;
	const char *section;
	const char *example;
	const char *const *choices;
	const char *const *labels;
	const char *empty;
};

#define ZL(h) "services.zipline.headers.x-zipline-" h

static const struct once_def ONCE[] = {
	{ZL("deletes-at"), "Deletes at", "the files delete themselves after this long", "Zipline", NULL, EXPIRY, EXPIRY_LABELS, "Server default"},
	{ZL("format"), "Name format",
	 "the link name to use; Override file name wins for a single file", "Zipline",
	 NULL, FORMATS, FORMAT_LABELS, "Use file name (grabit default)"},
	{ZL("image-compression-type"), "Compression format",
	 "the format images are compressed to, only when a compression percent is set", "Zipline", NULL, COMPRESS, COMPRESS_LABELS, "Server default"},
	{ZL("image-compression-percent"), "Compression",
	 "0-100, the compression level for images only; empty turns it off", "Zipline",
	 "optional", NULL, NULL, NULL},
	{ZL("max-views"), "Max views",
	 "the files are deleted after this many views; empty allows any number", "Zipline", "optional", NULL, NULL, NULL},
	{ZL("folder"), "Add to a folder", "id of a zipline folder for the files", "Zipline", "optional", NULL, NULL, NULL},
	{ZL("domain"), "Override domain", "return links on this domain instead", "Zipline", "files.example.com", NULL, NULL, NULL},
	{ZL("filename"), "Override file name",
	 "use this name instead of Name format; ignored when uploading more than one file", "Zipline", "optional", NULL, NULL, NULL},
	{ZL("password"), "Password", "viewers have to enter it to see the files", "Zipline", "optional", NULL, NULL, NULL},
	{ZL("original-name"), "Add original name",
	 "downloads keep the file's own name; the link still follows Name format", "Zipline", NULL, NULL, NULL, NULL},
	{"services.zipline.chunked", "Chunked upload", "split each file into chunks, for big files",
	 "Zipline", NULL, NULL, NULL, NULL},
	{"services.nest.folder", "Folder", "optional folder uuid", "Nest", NULL, NULL, NULL,
	 NULL},
	{"history.enabled", "Add to history", "show the uploads on the History tab", "After uploading", NULL, NULL, NULL, NULL},
	{"notifications", "Notification", "a desktop notification for each finished upload",
	 "After uploading", NULL, NULL, NULL, NULL},
};

static bool is_flag_row(const struct set_row *r) {
	return r->once && strstr(r->once, ORIGINAL_NAME) != NULL;
}

static struct set_row *scratch_row(struct settings_state *st, const char *key, const char *cfg,
								   const char *label, const char *hint) {
	struct set_row *r = calloc(1, sizeof *r);
	if (!r) return NULL;
	r->key = key;
	r->once = cfg;
	r->scratch = true;
	r->hint = hint;
	snprintf(r->label, sizeof r->label, "%s", label);
	if (config_key_info(cfg, &r->info) != 0) {
		free(r);
		return NULL;
	}
	set_fill_choices(st, r);
	if (r->info.secret) r->aux = AUX_EYE;
	if (set_page_push(&st->all, r) != 0) {
		free(r);
		return NULL;
	}
	return r;
}

static struct set_row *option_row(struct settings_state *st, const struct once_def *d) {
	struct set_row *r = scratch_row(st, NULL, d->cfg, d->label, d->hint);
	if (!r) return NULL;
	grabit_xasprintf(&r->owned_key, "upload once %s", d->cfg);
	r->key = r->owned_key;
	r->section = d->section;
	r->dep = strcmp(d->section, "Zipline") == 0 ? &ONLY_ZIPLINE
			 : strcmp(d->section, "Nest") == 0	? &ONLY_NEST
												: NULL;
	if (d->example) r->info.example = d->example;
	if (d->choices) {
		r->info.kind = CFG_KIND_ENUM;
		r->info.allow_empty = true;
		r->info.open_values = true;
		r->empty_label = d->empty;
		for (size_t k = 0; d->choices[k]; k++)
			set_push_choice(r, d->choices[k]);
		r->choice_labels = d->labels;
	}
	if (is_flag_row(r)) {
		r->info.kind = CFG_KIND_BOOL;
		r->info.def = "false";
	}
	return r;
}

int set_add_upload_rows(struct settings_state *st, struct set_page *p) {
	struct set_upq *q = calloc(1, sizeof *q);
	if (!q) return -1;
	st->upq = q;
	q->out_fd = q->prog_fd = -1;
	q->pop = SET_NONE;
	q->shown = UPQ_SHOWN;
	size_t n = 1 + sizeof ONCE / sizeof *ONCE;
	q->opts = calloc(n, sizeof *q->opts);
	if (!q->opts) return -1;
	struct set_row *row = set_extra_row(st, p, QUEUE_KEY, "Files", NULL);
	if (!row) return -1;
	row->custom = &SET_UPQ_CUSTOM;
	struct set_row *s = scratch_row(st, SERVICE_KEY, "service", "Service",
									"where these files go; the Uploads page sets the default");
	if (!s) return -1;
	s->section = "Upload options";
	s->page = p->title;
	q->opts[q->n_opts++] = s;
	for (size_t i = 0; i < sizeof ONCE / sizeof *ONCE; i++) {
		struct set_row *r = option_row(st, &ONCE[i]);
		if (!r) return -1;
		r->page = p->title;
		q->opts[q->n_opts++] = r;
	}
	return 0;
}

void upq_set_options(struct settings_state *st, bool open) {
	struct set_upq *q = st->upq;
	struct set_page *p = &st->pages[st->upload_page];
	q->options_open = open;
	p->n = 1;
	for (size_t i = 0; open && i < q->n_opts; i++)
		set_page_push(p, q->opts[i]);
	if (st->focus >= p->n) st->focus = 0;
	ui_window_damage(st->win);
}

size_t upq_changes(const struct set_upq *q) {
	size_t n = 0;
	for (size_t i = 0; i < q->n_opts; i++)
		n += q->opts[i]->once_value != NULL;
	return n;
}

const char *set_scratch_value(struct settings_state *st, const struct set_row *r) {
	if (r->once_value) return r->once_value;
	const char *v = config_get(&st->cfg, r->once);
	if (is_flag_row(r)) return v && strcmp(v, "true") == 0 ? "true" : "false";
	return v;
}

void set_scratch_write(struct settings_state *st, struct set_row *r, const char *value) {
	r->error[0] = '\0';
	if (is_flag_row(r) && value) value = strcmp(value, "true") == 0 ? "true" : "";
	if (value && value[0]) {
		struct config probe = {0};
		char err[sizeof r->error];
		log_capture_errors(err, sizeof err);
		int rc = config_set(&probe, r->once, value);
		log_capture_errors(NULL, 0);
		config_free(&probe);
		if (rc != 0) {
			snprintf(r->error, sizeof r->error, "%s", err[0] ? err : "this value is not valid");
			ui_window_damage(st->win);
			return;
		}
	}
	const char *mine = config_get(&st->cfg, r->once);
	const char *eff = mine ? mine : is_flag_row(r) ? ""
								: r->info.def	   ? r->info.def
												   : "";
	free(r->once_value);
	r->once_value = !value || strcmp(value, eff) == 0 ? NULL : strdup(value);
	if (r->once_value && is_flag_row(r) && !r->once_value[0]) {
		free(r->once_value);
		r->once_value = strdup("false");
	}
	ui_window_damage(st->win);
}

char *set_upload_overlay(const struct set_upq *q, size_t files) {
	struct config ov = {0};
	for (size_t i = 0; i < q->n_opts; i++) {
		const struct set_row *r = q->opts[i];
		if (!r->once_value || strcmp(r->once, "service") == 0) continue;
		if (files > 1 && strstr(r->once, FILENAME)) continue;
		const char *v = is_flag_row(r) && strcmp(r->once_value, "true") != 0 ? "" : r->once_value;
		if (config_set(&ov, r->once, v) != 0) {
			config_free(&ov);
			return NULL;
		}
	}
	if (ov.n == 0) {
		config_free(&ov);
		return strdup("");
	}
	char name[64];
	snprintf(name, sizeof name, "upload-%d.toml", (int)getpid());
	char *path = paths_temp_file(name);
	if (path && config_write_to(&ov, path) != 0) {
		free(path);
		path = NULL;
	}
	config_free(&ov);
	return path;
}

static int add_path(struct settings_state *st, const char *text, char *err, size_t n) {
	struct set_upq *q = st->upq;
	char *path = set_resolve_path(text);
	struct stat sb;
	if (!path || stat(path, &sb) != 0) {
		snprintf(err, n, "%s: no such file", text);
		free(path);
		return -1;
	}
	if (!S_ISREG(sb.st_mode)) {
		snprintf(err, n, "%s is a folder, not a file", grabit_basename(path));
		free(path);
		return -1;
	}
	for (size_t i = 0; i < q->n; i++)
		if (q->files[i].state != UQ_DONE && strcmp(q->files[i].path, path) == 0) {
			free(path);
			return 0;
		}
	if (q->n == q->cap) {
		size_t cap = q->cap ? q->cap * 2 : 8;
		struct upq_file *grown = realloc(q->files, cap * sizeof *grown);
		if (!grown) {
			free(path);
			return -1;
		}
		q->files = grown;
		q->cap = cap;
	}
	struct upq_file *f = &q->files[q->n++];
	*f = (struct upq_file){.path = path, .size = (int64_t)sb.st_size};
	return 1;
}

size_t upq_add_text(struct settings_state *st, const char *text, char *err, size_t n) {
	if (n) err[0] = '\0';
	size_t added = 0;
	char *copy = strdup(text);
	char *save = NULL;
	for (char *line = copy ? strtok_r(copy, "\r\n", &save) : NULL; line;
		 line = strtok_r(NULL, "\r\n", &save)) {
		while (*line == ' ' || *line == '\t')
			line++;
		grabit_rstrip(line, strlen(line));
		if (!line[0] || line[0] == '#') continue;
		if (add_path(st, line, err, n) > 0) added++;
	}
	free(copy);
	ui_window_damage(st->win);
	return added;
}

static void added_toast(struct settings_state *st, size_t added, const char *err,
						const char *where) {
	if (added && err[0])
		set_toast(st, "Added %zu file%s; %s", added, added == 1 ? "" : "s", err);
	else if (added)
		set_toast(st, "Added %zu file%s%s", added, added == 1 ? "" : "s", where);
	else if (err[0])
		set_toast(st, "%s", err);
	else
		set_toast(st, "Those files are already in the list");
}

size_t set_upload_drop(struct settings_state *st, const char *uri_list) {
	if (!st->upq) return 0;
	char err[200];
	size_t n = upq_add_text(st, uri_list, err, sizeof err);
	added_toast(st, n, err, "");
	return n;
}

void upq_paste(struct settings_state *st, struct set_row *r) {
	(void)r;
	char *text = clipboard_get_text();
	if (!text || !text[0]) {
		free(text);
		set_toast(st, "The clipboard has no copied files or paths");
		return;
	}
	char err[200];
	size_t n = upq_add_text(st, text, err, sizeof err);
	free(text);
	added_toast(st, n, err, " from the clipboard");
}

void upq_load_preview(struct settings_state *st, struct upq_file *f) {
	if (f->loaded) return;
	f->loaded = true;
	f->mime = mime_for_file(f->path);
	bool media = media_path(f->path) || mime_is_video(f->mime) ||
				 (f->mime && strcmp(f->mime, "image/gif") == 0);
	if (!media && grid_is_image(f->path)) {
		f->thumb = grid_load_fit(f->path, (int)(UPQ_POP_W * 2));
	} else if (media) {
		char *ffmpeg = media_ffmpeg(&st->cfg);
		if (ffmpeg) f->job = media_job_start(ffmpeg, f->path, (int)(UPQ_POP_W * 2));
		free(ffmpeg);
		set_arm_timer(st);
	}
}

static bool previews_pending(const struct set_upq *q) {
	for (size_t i = 0; q && i < q->n; i++)
		if (q->files[i].job) return true;
	return false;
}

static bool poll_previews(struct set_upq *q) {
	bool got = false;
	for (size_t i = 0; q && i < q->n; i++) {
		struct upq_file *f = &q->files[i];
		if (!f->job || !media_job_take(f->job, &f->thumb)) continue;
		media_job_end(f->job);
		f->job = NULL;
		got = true;
	}
	return got;
}

static void free_file(struct upq_file *f) {
	media_job_end(f->job);
	free(f->path);
	free(f->mime);
	if (f->thumb) cairo_surface_destroy(f->thumb);
}

void upq_remove_at(struct settings_state *st, size_t i) {
	struct set_upq *q = st->upq;
	if (i >= q->n || q->files[i].state == UQ_UPLOADING || q->files[i].state == UQ_WAIT) return;
	free_file(&q->files[i]);
	memmove(&q->files[i], &q->files[i + 1], (q->n - i - 1) * sizeof *q->files);
	q->n--;
	q->pop = SET_NONE;
	ui_window_damage(st->win);
}

void upq_clear_files(struct settings_state *st) {
	struct set_upq *q = st->upq;
	for (size_t i = q->n; i-- > 0;)
		upq_remove_at(st, i);
	q->shown = UPQ_SHOWN;
}

int64_t upq_total(const struct set_upq *q) {
	int64_t t = 0;
	for (size_t i = 0; i < q->n; i++)
		t += q->files[i].size;
	return t;
}

bool upq_busy(const struct set_upq *q) {
	return q && q->pid > 0;
}

bool set_upload_busy(const struct settings_state *st) {
	return upq_busy(st->upq) || previews_pending(st->upq);
}

double upq_progress(const struct set_upq *q) {
	if (q->batch_total <= 0) return 0;
	double p = (double)(q->batch_sent + q->cur_sent) / (double)q->batch_total;
	return p > 1 ? 1 : p;
}

bool upq_finalizing(const struct set_upq *q) {
	return upq_busy(q) && q->cur_total > 0 && q->cur_sent >= q->cur_total;
}

static void clear_results(struct set_upq *q) {
	for (size_t i = 0; i < q->n_res; i++)
		free(q->res[i].name);
	free(q->res);
	q->res = NULL;
	q->n_res = 0;
}

void upq_close_results(struct settings_state *st) {
	clear_results(st->upq);
	ui_window_damage(st->win);
}

void upq_copy_all(struct settings_state *st) {
	struct set_upq *q = st->upq;
	struct grabit_buf b = {0};
	for (size_t i = 0; i < q->n_res; i++) {
		if (q->res[i].failed || !q->res[i].url[0]) continue;
		if (b.len) grabit_buf_putc(&b, '\n');
		grabit_buf_puts(&b, q->res[i].url);
	}
	set_copy_text(st, b.data, "links");
	grabit_buf_free(&b);
}

static bool ready_service(struct settings_state *st, const char *service) {
	const char *missing = NULL;
	switch (upload_test_check(&st->cfg, service, &missing)) {
	case UPLOAD_TEST_NO_SERVICE:
		set_toast(st, "Pick a service under Options first");
		return false;
	case UPLOAD_TEST_UNKNOWN:
		set_toast(st, "%s is not a built-in service or an installed uploader", service);
		return false;
	case UPLOAD_TEST_MISSING: {
		struct set_row *m = set_find_row(st, missing);
		set_toast(st, "Set %s on the Uploads page first", m ? m->label : missing);
		return false;
	}
	case UPLOAD_TEST_READY:
		break;
	}
	return true;
}

static int nonblock_pipe(int fds[2]) {
	if (pipe2(fds, O_CLOEXEC) != 0) return -1;
	fcntl(fds[0], F_SETFL, fcntl(fds[0], F_GETFL) | O_NONBLOCK);
	return 0;
}

static pid_t spawn_upload(struct settings_state *st, const char *file, int *out_fd, int *prog_fd) {
	char flag[80];
	snprintf(flag, sizeof flag, "--%s", st->upq->service);
	char *env = NULL;
	const char *old = getenv("GRABIT_CONFIG");
	if (st->upq->overlay)
		grabit_xasprintf(&env, "%s%s%s", old && old[0] ? old : "", old && old[0] ? ":" : "",
						 st->upq->overlay);
	int out[2] = {-1, -1}, prog[2] = {-1, -1};
	pid_t pid = -1;
	if (nonblock_pipe(out) == 0 && nonblock_pipe(prog) == 0 &&
		fcntl(prog[1], F_SETFL, fcntl(prog[1], F_GETFL) | O_NONBLOCK) == 0) {
		struct set_spawn opt = {.env = env, .out_fd = out[1], .prog_fd = prog[1]};
		pid = set_spawn_self(st, &opt, (const char *const[]){"-f", file, flag, NULL});
	}
	free(env);
	for (int i = 0; i < 2; i++)
		if (prog[i] >= 0 && (i == 1 || pid < 0)) close(prog[i]);
	for (int i = 0; i < 2; i++)
		if (out[i] >= 0 && (i == 1 || pid < 0)) close(out[i]);
	if (pid < 0) return -1;
	*out_fd = out[0];
	*prog_fd = prog[0];
	return pid;
}

static void finish_batch(struct settings_state *st) {
	struct set_upq *q = st->upq;
	if (q->overlay) {
		unlink(q->overlay);
		free(q->overlay);
		q->overlay = NULL;
	}
	size_t kept = 0;
	for (size_t i = 0; i < q->n; i++) {
		if (q->files[i].state == UQ_DONE)
			free_file(&q->files[i]);
		else
			q->files[kept++] = q->files[i];
	}
	q->n = kept;
	q->pop = SET_NONE;
	q->batch_total = q->batch_sent = q->cur_sent = q->cur_total = 0;
	if (q->failed == 0)
		set_toast(st, "Uploaded %zu file%s", q->done, q->done == 1 ? "" : "s");
	else if (q->done == 0)
		set_toast(st, "The upload failed; details in grabit.log");
	else
		set_toast(st, "Uploaded %zu, %zu failed; details in grabit.log", q->done, q->failed);
}

static void next(struct settings_state *st) {
	struct set_upq *q = st->upq;
	for (size_t i = 0; i < q->n; i++) {
		if (q->files[i].state != UQ_WAIT) continue;
		int out = -1, prog = -1;
		pid_t pid = spawn_upload(st, q->files[i].path, &out, &prog);
		if (pid < 0) {
			q->files[i].state = UQ_FAILED;
			q->failed++;
			continue;
		}
		q->files[i].state = UQ_UPLOADING;
		q->pid = pid;
		q->out_fd = out;
		q->prog_fd = prog;
		q->cur = i;
		q->out_len = q->prog_len = 0;
		q->cur_sent = 0;
		q->cur_total = q->files[i].size;
		set_arm_timer(st);
		return;
	}
	finish_batch(st);
}

void upq_start(struct settings_state *st) {
	struct set_upq *q = st->upq;
	if (upq_busy(q)) return;
	size_t todo = 0;
	for (size_t i = 0; i < q->n; i++)
		todo += q->files[i].state == UQ_READY || q->files[i].state == UQ_FAILED;
	if (todo == 0) {
		set_toast(st, "Add files first: drop them here, paste them, or type a path");
		return;
	}
	struct set_row *s = set_find_row(st, SERVICE_KEY);
	if (!s || !ready_service(st, set_effective(st, s))) return;
	snprintf(q->service, sizeof q->service, "%s", set_effective(st, s));
	char *overlay = set_upload_overlay(q, todo);
	if (!overlay) {
		set_toast(st, "Could not write the options for this upload");
		return;
	}
	q->overlay = overlay[0] ? overlay : NULL;
	if (!overlay[0]) free(overlay);
	clear_results(q);
	q->done = q->failed = 0;
	q->batch_total = q->batch_sent = 0;
	q->speed = 0;
	q->sample_ms = grabit_now_ms();
	q->sample_bytes = 0;
	q->pop = SET_NONE;
	for (size_t i = 0; i < q->n; i++)
		if (q->files[i].state == UQ_READY || q->files[i].state == UQ_FAILED) {
			q->files[i].state = UQ_WAIT;
			q->batch_total += q->files[i].size;
		}
	if (q->batch_total <= 0) q->batch_total = 1;
	next(st);
	ui_window_damage(st->win);
}

static void drain(int fd, char *buf, size_t cap, size_t *len) {
	while (fd >= 0 && *len + 1 < cap) {
		ssize_t got = read(fd, buf + *len, cap - 1 - *len);
		if (got <= 0) break;
		*len += (size_t)got;
	}
	buf[*len] = '\0';
}

static bool read_progress(struct set_upq *q) {
	bool got = false;
	char chunk[4096];
	ssize_t n;
	while (q->prog_fd >= 0 && (n = read(q->prog_fd, chunk, sizeof chunk)) > 0) {
		for (ssize_t k = 0; k < n; k++) {
			if (chunk[k] != '\n') {
				if (q->prog_len + 1 < sizeof q->prog) q->prog[q->prog_len++] = chunk[k];
				continue;
			}
			q->prog[q->prog_len] = '\0';
			long long sent = 0, total = 0;
			if (sscanf(q->prog, "%lld %lld", &sent, &total) == 2 && total > 0) {
				q->cur_sent = sent;
				q->cur_total = total;
				got = true;
			}
			q->prog_len = 0;
		}
	}
	if (!got) return false;
	int64_t now = grabit_now_ms(), bytes = q->batch_sent + q->cur_sent;
	if (now - q->sample_ms >= 250) {
		double inst = (double)(bytes - q->sample_bytes) * 1000.0 / (double)(now - q->sample_ms);
		q->speed = q->speed > 0 ? q->speed * 0.6 + inst * 0.4 : inst;
		q->sample_ms = now;
		q->sample_bytes = bytes;
	}
	return true;
}

static void take_url(struct upq_result *res, const char *out) {
	for (const char *p = out; *p;) {
		size_t len = strcspn(p, "\n");
		if (strncmp(p, "http", 4) == 0) {
			snprintf(res->url, sizeof res->url, "%.*s", (int)len, p);
			return;
		}
		p += len;
		if (*p) p++;
	}
}

static void add_result(struct set_upq *q, const struct upq_file *f, bool ok) {
	struct upq_result *grown = realloc(q->res, (q->n_res + 1) * sizeof *grown);
	if (!grown) return;
	q->res = grown;
	struct upq_result *res = &q->res[q->n_res++];
	*res = (struct upq_result){.name = strdup(grabit_basename(f->path)), .failed = !ok};
	if (ok) take_url(res, q->out);
}

static void close_fd(int *fd) {
	if (*fd >= 0) close(*fd);
	*fd = -1;
}

bool set_upload_poll(struct settings_state *st) {
	struct set_upq *q = st->upq;
	bool got = poll_previews(q);
	if (!upq_busy(q)) return got;
	drain(q->out_fd, q->out, sizeof q->out, &q->out_len);
	bool moved = read_progress(q);
	int rc = set_reap(&q->pid);
	if (rc < 0) return moved || got;
	drain(q->out_fd, q->out, sizeof q->out, &q->out_len);
	bool ok = rc == 1;
	close_fd(&q->out_fd);
	close_fd(&q->prog_fd);
	if (q->cur < q->n) {
		struct upq_file *f = &q->files[q->cur];
		f->state = ok ? UQ_DONE : UQ_FAILED;
		add_result(q, f, ok);
		q->batch_sent += f->size;
		q->cur_sent = 0;
		if (ok)
			q->done++;
		else
			q->failed++;
	}
	next(st);
	return true;
}

void set_upload_free(struct settings_state *st) {
	struct set_upq *q = st->upq;
	if (!q) return;
	close_fd(&q->out_fd);
	close_fd(&q->prog_fd);
	if (q->overlay) unlink(q->overlay);
	free(q->overlay);
	for (size_t i = 0; i < q->n; i++)
		free_file(&q->files[i]);
	free(q->files);
	clear_results(q);
	free(q->opts);
	free(q);
	st->upq = NULL;
}
