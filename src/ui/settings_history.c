// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _GNU_SOURCE
#include "ui/settings_internal.h"

#include "clipboard/clipboard.h"
#include "log.h"
#include "ui/window.h"
#include "util/util.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define OCR_PREFIX "grabit history copy --ocr "
#define CONFIRM_KEY "history.confirm_delete"
#define CLEAR_KEY "grabit history clear"
#define CLEAR_OCR_KEY "grabit history clear --ocr"

static const struct history_entry *find(struct settings_state *st, const char *id) {
	const struct history_entry *e = history_find(&st->hist, id);
	return e ? e : history_find(&st->hist_ocr, id);
}

static const struct history_entry *entry_of(struct settings_state *st, const struct set_row *r) {
	return find(st, r->key + strlen(OCR_PREFIX));
}

void set_hist_when(int64_t t, char *out, size_t n) {
	time_t tt = (time_t)t;
	struct tm tm;
	if (!localtime_r(&tt, &tm) || !strftime(out, n, "%b %d %H:%M", &tm)) snprintf(out, n, "?");
}

void set_hist_tilde(const char *path, char *out, size_t n) {
	const char *home = getenv("HOME");
	size_t hl = home ? strlen(home) : 0;
	if (path && hl > 1 && strncmp(path, home, hl) == 0 && path[hl] == '/')
		snprintf(out, n, "~%s", path + hl);
	else
		snprintf(out, n, "%s", path ? path : "");
}

static void first_line(const char *text, char *out, size_t n) {
	const char *s = text ? text : "";
	while (*s == ' ' || *s == '\n' || *s == '\t')
		s++;
	size_t len = strcspn(s, "\n");
	if (len >= n) len = n - 1;
	memcpy(out, s, len);
	out[len] = '\0';
}

static void describe(struct settings_state *st, struct set_row *r, const char *id) {
	const struct history_entry *e = find(st, id);
	if (!e) return;
	char t[32];
	set_hist_when(e->time, t, sizeof t);
	first_line(e->text, r->label, sizeof r->label);
	r->button = "Copy";
	r->alt[0].label = "Forget";
	if (e->target)
		grabit_xasprintf(&r->owned_hint, "%s \xc2\xb7 %s, translated to %s", t, e->lang ? e->lang : "?",
						 e->target);
	else
		grabit_xasprintf(&r->owned_hint, "%s \xc2\xb7 %s", t, e->lang ? e->lang : "?");
}

static void copy_ocr(struct settings_state *st, struct set_row *r);
static void forget_ocr(struct settings_state *st, struct set_row *r);

static const struct set_list OCR_LIST = {
	.action = copy_ocr,
	.alt = {forget_ocr},
	.prefix = OCR_PREFIX,
	.suffix = "",
	.section = "OCR text",
	.button = "Copy",
	.unreadable = "this entry could not be read",
	.describe = describe,
};

static void sync_page(struct settings_state *st, size_t page, const struct set_list *l,
					  const struct history_list *h) {
	size_t n = h->n < SET_HIST_SHOWN ? h->n : SET_HIST_SHOWN;
	char **ids = calloc(n ? n : 1, sizeof *ids);
	if (!ids) return;
	for (size_t i = 0; i < n; i++)
		ids[i] = h->v[i].id;
	set_sync_list(st, page, l, ids, n);
	free(ids);
}

static void count_hint(struct settings_state *st, const char *key, size_t n, const char *tail) {
	struct set_row *r = set_find_row(st, key);
	if (!r) return;
	free(r->owned_hint);
	r->owned_hint = NULL;
	if (n == 0)
		grabit_xasprintf(&r->owned_hint, "nothing recorded yet");
	else if (n == 1)
		grabit_xasprintf(&r->owned_hint, "forgets the one entry%s", tail);
	else
		grabit_xasprintf(&r->owned_hint, "forgets all %zu entries%s", n, tail);
	r->hint = r->owned_hint;
}

void set_refresh_history(struct settings_state *st) {
	struct history_list media = {0}, ocr = {0};
	if (history_load(HISTORY_MEDIA, &media) != 0 || history_load(HISTORY_OCR, &ocr) != 0) {
		history_list_free(&media);
		return;
	}
	struct history_list old_media = st->hist, old_ocr = st->hist_ocr;
	st->hist = media;
	st->hist_ocr = ocr;
	st->hist_stamp = history_stamp();
	sync_page(st, st->ocr_history_page, &OCR_LIST, &st->hist_ocr);
	set_hist_grid_sync(st);
	history_list_free(&old_media);
	history_list_free(&old_ocr);
	count_hint(st, CLEAR_KEY, st->hist.n, "; uploads and files stay where they are");
	count_hint(st, CLEAR_OCR_KEY, st->hist_ocr.n,
			   st->hist_ocr.n > SET_HIST_SHOWN ? "; the OCR history page lists the newest 200" : "");
	if (st->win) ui_window_damage(st->win);
}

void set_hist_release(struct settings_state *st) {
	set_hist_grid_free(st);
	history_list_free(&st->hist);
	history_list_free(&st->hist_ocr);
}

void set_copy_text(struct settings_state *st, const char *text, const char *what) {
	if (text && clipboard_set_text(text) == 0)
		set_toast(st, "Copied the %s", what);
	else
		set_toast(st, "Could not copy the %s", what);
}

void set_open_target(struct settings_state *st, const char *target) {
	int rc = target ? grabit_xdg_open(target) : -1;
	if (rc == -2)
		set_toast(st, "xdg-open is not installed");
	else if (rc != 0)
		set_toast(st, "Could not open %s", target ? target : "this entry");
}

static void restore(struct settings_state *st, const char *lines) {
	if (history_restore(lines) == 0)
		set_toast(st, "Restored");
	else
		set_toast(st, "Could not change %s", history_file());
	set_refresh_history(st);
}

static void forget_taken(struct settings_state *st, int n, char *taken, const char *what) {
	if (n < 0) {
		set_toast(st, "Could not change %s", history_file());
	} else {
		set_toast(st, "Forgot %s", what);
		if (taken) {
			set_ask(st, "Undo", false, restore, taken);
			taken = NULL;
		}
	}
	free(taken);
	set_refresh_history(st);
}

static void forget(struct settings_state *st, const char *id) {
	const struct history_entry *e = find(st, id);
	char what[160];
	snprintf(what, sizeof what, "%s", e ? set_hist_name(e) : "the entry");
	char *taken = NULL;
	int n = history_take(id, 0, &taken);
	forget_taken(st, n, taken, what);
}

static void copy_ocr(struct settings_state *st, struct set_row *r) {
	const struct history_entry *e = entry_of(st, r);
	if (e) set_copy_text(st, e->text, "text");
}

static void forget_ocr(struct settings_state *st, struct set_row *r) {
	const struct history_entry *e = entry_of(st, r);
	if (e) forget(st, e->id);
}

static void act_copy_link(struct settings_state *st, const char *id) {
	const struct history_entry *e = find(st, id);
	if (e) set_copy_text(st, e->url, "link");
}

static void act_open_link(struct settings_state *st, const char *id) {
	const struct history_entry *e = find(st, id);
	if (e) set_open_target(st, e->url);
}

static void act_copy_image(struct settings_state *st, const char *id) {
	const struct history_entry *e = find(st, id);
	if (!e) return;
	if (clipboard_set_image_file(e->path) == 0)
		set_toast(st, "Copied the image");
	else
		set_toast(st, "Could not copy the image");
}

static void act_open_file(struct settings_state *st, const char *id) {
	const struct history_entry *e = find(st, id);
	if (e) set_open_target(st, e->path);
}

static void act_show_folder(struct settings_state *st, const char *id) {
	const struct history_entry *e = find(st, id);
	if (!e || !e->path) return;
	char dir[4096];
	snprintf(dir, sizeof dir, "%s", e->path);
	char *slash = strrchr(dir, '/');
	if (slash && slash != dir) *slash = '\0';
	set_open_target(st, dir);
}

static void act_copy_path(struct settings_state *st, const char *id) {
	const struct history_entry *e = find(st, id);
	if (e) set_copy_text(st, e->path, "path");
}

static void act_delete(struct settings_state *st, const char *id);

pid_t set_spawn_self(struct settings_state *st, const char *const *args) {
	char self[1024];
	if (grabit_self_exe(self, sizeof self) != 0) {
		set_toast(st, "Could not find the grabit binary");
		return -2;
	}
	char *argv[8] = {self};
	for (size_t i = 0; args[i] && i + 2 < sizeof argv / sizeof *argv; i++)
		argv[i + 1] = (char *)args[i];
	pid_t pid = fork();
	if (pid == 0) {
		grabit_quiet_stdio();
		execv(self, argv);
		_exit(127);
	}
	return pid;
}

size_t set_hist_actions(const struct history_entry *e, struct set_hist_action *out) {
	size_t n = 0;
	bool file = history_has_file(e);
	if (e->kind == HISTORY_UPLOAD) {
		out[n++] = (struct set_hist_action){"Copy link", act_copy_link};
		out[n++] = (struct set_hist_action){"Open link", act_open_link};
	}
	if (file && !e->video) out[n++] = (struct set_hist_action){"Copy image", act_copy_image};
	if (file) {
		out[n++] = (struct set_hist_action){e->kind == HISTORY_UPLOAD ? "Open file" : "Open",
											act_open_file};
		out[n++] = (struct set_hist_action){"Show in folder", act_show_folder};
	}
	if (e->path && e->kind == HISTORY_FILE)
		out[n++] = (struct set_hist_action){"Copy path", act_copy_path};
	if (history_can_delete_file(e) || history_can_delete_host(e))
		out[n++] = (struct set_hist_action){"Delete\xe2\x80\xa6", act_delete};
	out[n++] = (struct set_hist_action){"Forget", forget};
	return n;
}

void set_hist_open_menu(struct settings_state *st, size_t row, const struct history_entry *e,
						double bx, double by) {
	char *arg = strdup(e->id);
	if (!arg) return;
	set_open_actions(st, row, bx, by, arg);
	struct set_hist_action acts[SET_MAX_ACTIONS];
	size_t n = set_hist_actions(e, acts);
	for (size_t i = 0; i < n; i++)
		set_add_action(st, acts[i].label, acts[i].fn);
}

static void delete_now(struct settings_state *st, const char *id) {
	if (st->hist_pid > 0) {
		set_toast(st, "Wait for the current delete to finish");
		return;
	}
	pid_t pid = set_spawn_self(st, (const char *const[]){"history", "delete", id, "--yes", NULL});
	if (pid == -2) return;
	if (pid < 0) {
		set_toast(st, "Could not start the delete");
		return;
	}
	st->hist_pid = pid;
	const struct history_entry *e = find(st, id);
	snprintf(st->hist_job_name, sizeof st->hist_job_name, "%s", e ? set_hist_name(e) : "the entry");
	set_toast(st, "Deleting %s", st->hist_job_name);
}

static void act_delete(struct settings_state *st, const char *id) {
	const struct history_entry *e = find(st, id);
	if (!e) return;
	if (!history_can_delete_host(e) && !history_can_delete_file(e)) {
		forget(st, e->id);
		return;
	}
	struct set_row *c = set_find_row(st, CONFIRM_KEY);
	if (c && strcmp(set_effective(st, c), "false") == 0) {
		delete_now(st, e->id);
		return;
	}
	char *arg = strdup(e->id);
	if (!arg) return;
	char what[200], title[96], text[256];
	history_delete_summary(e, HISTORY_DELETE_BOTH, what, sizeof what);
	what[0] = (char)(what[0] == 'd' ? 'D' : what[0]);
	snprintf(title, sizeof title, "Delete %s?", set_hist_name(e));
	snprintf(text, sizeof text, "%s.%s", what,
			 history_host_keeps(e) ? " The host has no delete, so the link stays up." : "");
	set_dialog_open(st, title, text, arg);
	set_dialog_button(st, "Delete", true, delete_now);
	set_dialog_button(st, "Cancel", false, NULL);
	set_dialog_check(st, "Don't ask again", CONFIRM_KEY);
}

const char *set_hist_name(const struct history_entry *e) {
	if (e->kind == HISTORY_OCR) {
		static char text[48];
		first_line(e->text, text, sizeof text);
		return text;
	}
	const char *name = e->kind == HISTORY_UPLOAD && e->url ? strrchr(e->url, '/') : NULL;
	if (name && name[1]) return name + 1;
	if (e->path) return grabit_basename(e->path);
	return e->url ? e->url : "?";
}

const char *set_hist_row_text(struct settings_state *st, const struct set_row *r) {
	if (strncmp(r->key, OCR_PREFIX, strlen(OCR_PREFIX)) != 0) return NULL;
	const struct history_entry *e = history_find(&st->hist_ocr, r->key + strlen(OCR_PREFIX));
	return e && e->text ? e->text : r->label;
}

void set_history_check(struct settings_state *st) {
	if (history_stamp() == st->hist_stamp) return;
	set_refresh_history(st);
	log_debug("settings: history changed, %zu entries", st->hist.n);
}

int set_reap(pid_t *pid) {
	if (*pid <= 0) return -1;
	int status = 0;
	pid_t w = waitpid(*pid, &status, WNOHANG);
	if (w == 0 || (w < 0 && errno == EINTR)) return -1;
	bool ok = (w < 0 && errno == ECHILD) ||
			  (w == *pid && WIFEXITED(status) && WEXITSTATUS(status) == 0);
	*pid = 0;
	return ok;
}

bool set_history_poll(struct settings_state *st) {
	int del = set_reap(&st->hist_pid);
	if (del == 1)
		set_toast(st, "Deleted %s", st->hist_job_name);
	else if (del == 0)
		set_toast(st, "Could not delete %s; details in grabit.log", st->hist_job_name);
	if (del < 0) return false;
	set_refresh_history(st);
	return true;
}

static void clear(struct settings_state *st, const char *arg) {
	bool ocr = strcmp(arg, "ocr") == 0;
	char *taken = NULL;
	int n = history_take(NULL, ocr ? HISTORY_OCR : HISTORY_MEDIA, &taken);
	char what[96];
	snprintf(what, sizeof what, "%d %s entr%s", n, ocr ? "OCR history" : "history", n == 1 ? "y" : "ies");
	forget_taken(st, n, taken, what);
}

static void clear_ask(struct settings_state *st, bool ocr) {
	if ((ocr ? st->hist_ocr.n : st->hist.n) == 0) {
		set_toast(st, "Nothing to clear");
		return;
	}
	char *arg = strdup(ocr ? "ocr" : "media");
	if (!arg) return;
	set_dialog_open(st, ocr ? "Clear the OCR history?" : "Clear the history?",
					ocr ? "This forgets every OCR text." : "This forgets every entry. Uploads and saved files stay where they are.", arg);
	set_dialog_button(st, "Clear", true, clear);
	set_dialog_button(st, "Cancel", false, NULL);
}

static void clear_media(struct settings_state *st, struct set_row *r) {
	(void)r;
	clear_ask(st, false);
}

static void clear_ocr(struct settings_state *st, struct set_row *r) {
	(void)r;
	clear_ask(st, true);
}

int set_add_history_clear_rows(struct settings_state *st, struct set_page *p) {
	struct set_row *r = set_extra_row(st, p, CLEAR_KEY, "Clear history", "nothing recorded yet");
	if (!r) return -1;
	r->button = "Clear";
	r->action = clear_media;
	struct set_row *o = set_extra_row(st, p, CLEAR_OCR_KEY, "Clear OCR history", "nothing recorded yet");
	if (!o) return -1;
	o->button = "Clear";
	o->action = clear_ocr;
	return 0;
}

int set_add_history_rows(struct settings_state *st, struct set_page *p) {
	struct set_row *g = set_extra_row(st, p, "grabit history", "Screenshots and recordings", NULL);
	if (!g) return -1;
	g->grid = true;
	g->custom = &SET_GRID_CUSTOM;
	g->action = set_hist_grid_key;
	return 0;
}
