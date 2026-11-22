// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "ui/ui.h"

#include "log.h"
#include "paths.h"
#include "ui/settings_internal.h"
#include "ui/window.h"
#include "ui_theme.h"
#include "upload/sxcu.h"
#include "util/util.h"
#include "wl/wl.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static void update_banner(struct settings_state *st) {
	st->banner[0] = '\0';
	if (st->cfg.unparsed)
		snprintf(st->banner, sizeof st->banner,
				 "%s does not parse; fix it before changing settings here",
				 paths_config_file());
}

static bool stamps_changed(const struct settings_state *st) {
	return config_stamp() != st->cfg_stamp ||
		   grabit_file_mtime_ns(paths_state_file()) != st->state_stamp;
}

static int reload(struct settings_state *st) {
	struct config fresh;
	int64_t cfg = config_stamp(), state = grabit_file_mtime_ns(paths_state_file());
	if (config_load_full(&fresh) != 0) return -1;
	config_free(&st->cfg);
	st->cfg = fresh;
	st->cfg_stamp = cfg;
	st->state_stamp = state;
	grabit_ui_theme_init(&st->cfg);
	update_banner(st);
	set_refresh_cfg(st);
	return 0;
}

static void on_focus_gained(struct ui_window *w) {
	struct settings_state *st = w->user;
	if (stamps_changed(st) && reload(st) == 0) ui_window_damage(w);
}

static int hex_val(char c) {
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

static void uri_to_path(const char *in, char *out, size_t cap) {
	if (strncmp(in, "file://", 7) != 0) {
		snprintf(out, cap, "%s", in);
		return;
	}
	in += 7;
	const char *slash = *in == '/' ? NULL : strchr(in, '/');
	if (slash) in = slash;
	size_t n = 0;
	for (; *in && n + 1 < cap; in++) {
		int hi, lo;
		if (in[0] == '%' && (hi = hex_val(in[1])) >= 0 && (lo = hex_val(in[2])) >= 0) {
			out[n++] = (char)(hi * 16 + lo);
			in += 2;
		} else {
			out[n++] = *in;
		}
	}
	out[n] = '\0';
}

void set_toast(struct settings_state *st, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(st->toast, sizeof st->toast, fmt, ap);
	va_end(ap);
	st->toast_until = grabit_now_ms() + SET_TOAST_MS;
	st->toast_label = NULL;
	st->toast_fn = NULL;
	free(st->toast_arg);
	st->toast_arg = NULL;
	set_arm_timer(st);
	ui_window_damage(st->win);
}

static void toast_change(struct settings_state *st, const struct set_row *r, bool reset) {
	if (reset) {
		char def[96];
		set_default_text(r, def, sizeof def);
		if (r->info.secret)
			set_toast(st, "%s cleared", r->label);
		else
			set_toast(st, "%s reset to %s", r->label, def);
		return;
	}
	const char *v = set_effective(st, r);
	switch (r->info.kind) {
	case CFG_KIND_BOOL:
		set_toast(st, "%s turned %s", r->label, strcmp(v, "true") == 0 ? "on" : "off");
		return;
	case CFG_KIND_ENUM:
		set_toast(st, "%s set to %s", r->label, set_dd_label(r, set_dd_index_of(st, r)));
		return;
	case CFG_KIND_INT:
	case CFG_KIND_TEXT:
	case CFG_KIND_KEYBIND:
		if (r->info.secret)
			set_toast(st, "%s saved", r->label);
		else
			set_toast(st, "%s set to %.60s", r->label, v);
		return;
	}
}

static void free_change(struct set_change *c) {
	free(c->before);
	free(c->after);
	free(c->sxcu);
	*c = (struct set_change){0};
}

void set_history_free(struct settings_state *st) {
	for (size_t i = 0; i < st->history_n; i++)
		free_change(&st->history[i]);
	st->history_n = st->history_pos = 0;
}

static struct set_change *push_change(struct settings_state *st) {
	while (st->history_n > st->history_pos)
		free_change(&st->history[--st->history_n]);
	if (st->history_n == SET_HISTORY) {
		free_change(&st->history[0]);
		memmove(st->history, st->history + 1, (SET_HISTORY - 1) * sizeof *st->history);
		st->history_n--;
	}
	struct set_change *c = &st->history[st->history_n++];
	st->history_pos = st->history_n;
	return c;
}

static void record_change(struct settings_state *st, struct set_row *r, char *before,
						  const char *after) {
	*push_change(st) = (struct set_change){.row = r, .before = before, .after = after ? strdup(after) : NULL};
}

void set_ask(struct settings_state *st, const char *label, bool danger, set_toast_action fn,
			 char *arg) {
	free(st->toast_arg);
	st->toast_label = label;
	st->toast_danger = danger;
	st->toast_fn = fn;
	st->toast_arg = arg;
	st->toast_until = grabit_now_ms() + SET_UNDO_TOAST_MS;
	set_arm_timer(st);
}

static void undo_action(struct settings_state *st, const char *arg) {
	(void)arg;
	set_undo(st);
}

static void undo_toast(struct settings_state *st) {
	set_ask(st, "Undo", false, undo_action, NULL);
}

static bool row_failed(struct settings_state *st, struct set_row *r, const char *err,
					   const char *fallback) {
	if (!r->error[0]) snprintf(r->error, sizeof r->error, "%s", err[0] ? err : fallback);
	ui_window_damage(st->win);
	return false;
}

static bool write_value(struct settings_state *st, struct set_row *r, const char *value) {
	if (stamps_changed(st)) reload(st);
	if (st->cfg.unparsed) {
		snprintf(r->error, sizeof r->error, "%s", st->banner);
		ui_window_damage(st->win);
		return false;
	}
	const char *cur = set_value(st, r);
	const char *same = cur ? cur : r->info.def;
	if (value ? same && strcmp(value, same) == 0 : !cur) {
		r->error[0] = '\0';
		return false;
	}
	char err[sizeof r->error];
	log_capture_errors(err, sizeof err);
	int rc = config_write_key(r->key, value);
	log_capture_errors(NULL, 0);
	if (rc != 0) return row_failed(st, r, err, "could not save this setting");
	r->error[0] = '\0';
	reload(st);
	toast_change(st, r, value == NULL);
	ui_window_damage(st->win);
	return true;
}

void set_apply(struct settings_state *st, struct set_row *r, const char *value) {
	if (value && !value[0]) value = NULL;
	if (r->submit) {
		r->error[0] = '\0';
		if (value) r->submit(st, value);
		return;
	}
	if (stamps_changed(st)) reload(st);
	const char *cur = set_value(st, r);
	char *before = cur ? strdup(cur) : NULL;
	if (!write_value(st, r, value)) {
		free(before);
		return;
	}
	record_change(st, r, before, value);
	undo_toast(st);
}

static bool drop_unknown(struct settings_state *st, const char *key) {
	char err[192];
	log_capture_errors(err, sizeof err);
	int rc = config_remove_key(key);
	log_capture_errors(NULL, 0);
	if (rc != 0) set_toast(st, "%s", err[0] ? err : "could not remove this key");
	return rc == 0;
}

static void remove_unknown(struct settings_state *st, const char *key) {
	if (!drop_unknown(st, key)) return;
	set_toast(st, "Removed %s from config.toml", key);
	reload(st);
}

void set_remove_unknown_ask(struct settings_state *st, struct set_row *r) {
	char *key = strdup(r->key);
	if (!key) return;
	set_toast(st, "Remove %s from config.toml?", key);
	set_ask(st, "Remove", true, remove_unknown, key);
}

void set_rename_unknown(struct settings_state *st, struct set_row *r) {
	const char *hint = config_suggest_key(r->key);
	struct set_row *dst = hint ? set_find_row(st, hint) : NULL;
	const char *v = config_get(&st->cfg, r->key);
	if (!dst) return;
	char *key = strdup(r->key), *value = strdup(v ? v : ""), *to = strdup(hint);
	if (key && value && to) {
		set_apply(st, dst, value);
		if (dst->error[0]) {
			set_toast(st, "Could not rename %s: %s", key, dst->error);
			dst->error[0] = '\0';
		} else if (drop_unknown(st, key)) {
			set_toast(st, "Renamed %s to %s", key, to);
			reload(st);
		}
	}
	free(key);
	free(value);
	free(to);
}

static const char *sxcu_what(const struct set_change *c) {
	if (!c->after) return "Uploader removed";
	return c->before ? "Uploader replaced" : "Uploader added";
}

static bool restore_sxcu(struct settings_state *st, struct set_change *c, const char *content) {
	char err[sizeof c->row->error];
	c->row->error[0] = '\0';
	log_capture_errors(err, sizeof err);
	int rc = content ? sxcu_dir_write(c->sxcu, content, strlen(content))
					 : sxcu_dir_remove(c->sxcu);
	log_capture_errors(NULL, 0);
	set_refresh_uploaders(st);
	if (rc != 0) return row_failed(st, c->row, err, "could not restore the uploader");
	set_toast(st, "%s", sxcu_what(c));
	return true;
}

char *set_resolve_path(const char *path_or_uri) {
	char raw[UI_TEXT_MAX];
	uri_to_path(path_or_uri, raw, sizeof raw);
	return paths_expand_tilde(raw);
}

bool set_add_sxcu(struct settings_state *st, const char *path_or_uri) {
	struct set_row *r = set_sxcu_row(st);
	if (!r) return false;
	char *path = set_resolve_path(path_or_uri);
	char err[sizeof r->error];
	char *name = NULL, *before = NULL, *after = NULL;
	size_t len = 0, old_len;
	log_capture_errors(err, sizeof err);
	int rc = -1;
	if (path && sxcu_dir_name(path, &name) == 0 &&
		grabit_read_file(path, SXCU_FILE_MAX, &after, &len) == 0) {
		sxcu_dir_read(name, &before, &old_len);
		rc = sxcu_dir_write(name, after, len);
	}
	log_capture_errors(NULL, 0);
	free(path);
	if (rc != 0) {
		free(name);
		free(before);
		free(after);
		return row_failed(st, r, err, "could not add this uploader");
	}
	struct set_change *c = push_change(st);
	*c = (struct set_change){.row = r, .before = before, .after = after, .sxcu = name};
	set_refresh_uploaders(st);
	set_toast(st, "%s", sxcu_what(c));
	undo_toast(st);
	return true;
}

static void remove_uploader(struct settings_state *st, const char *name) {
	struct set_row *add = set_sxcu_row(st);
	char *copy = strdup(name), *before = NULL;
	size_t len = 0;
	char err[sizeof add->error];
	log_capture_errors(err, sizeof err);
	int rc = copy && sxcu_dir_read(name, &before, &len) == 0 ? sxcu_dir_remove(name) : -1;
	log_capture_errors(NULL, 0);
	if (rc != 0 || !add) {
		free(copy);
		free(before);
		set_toast(st, "%s", err[0] ? err : "could not remove this uploader");
		return;
	}
	*push_change(st) = (struct set_change){.row = add, .before = before, .sxcu = copy};
	set_refresh_uploaders(st);
	set_toast(st, "Uploader %s removed", name);
	undo_toast(st);
}

void set_remove_uploader_ask(struct settings_state *st, struct set_row *r) {
	char *name = strndup(r->key, strlen(r->key) - strlen(".sxcu"));
	if (!name) return;
	const char *svc = config_get(&st->cfg, "service");
	bool in_use = svc && strcmp(svc, name) == 0;
	set_toast(st, "Remove uploader %s?%s", name, in_use ? " It is the current upload service" : "");
	set_ask(st, "Remove", true, remove_uploader, name);
}

static void replay(struct settings_state *st, bool undo) {
	if (undo ? st->history_pos == 0 : st->history_pos == st->history_n) {
		set_toast(st, undo ? "Nothing to undo" : "Nothing to redo");
		return;
	}
	size_t steps = 0;
	bool ok = false;
	for (;;) {
		size_t at = undo ? st->history_pos - 1 : st->history_pos;
		struct set_change *c = &st->history[at];
		bool more = undo ? c->chain && at > 0 : at + 1 < st->history_n && st->history[at + 1].chain;
		const char *content = undo ? c->before : c->after;
		ok = c->sxcu ? restore_sxcu(st, c, content) : write_value(st, c->row, content);
		if (!ok && c->row->error[0]) break;
		st->history_pos = undo ? at : at + 1;
		steps++;
		if (!more) break;
	}
	if (steps > 1) {
		set_toast(st, "%s: reset of %zu settings", undo ? "Undone" : "Redone", steps);
		return;
	}
	if (!ok) return;
	char msg[sizeof st->toast + 16];
	snprintf(msg, sizeof msg, "%s: %s", undo ? "Undone" : "Redone", st->toast);
	set_toast(st, "%s", msg);
}

void set_undo(struct settings_state *st) {
	replay(st, true);
}

void set_redo(struct settings_state *st) {
	replay(st, false);
}

static bool page_resettable(struct settings_state *st, const struct set_row *r) {
	return !r->info.secret && !r->submit && set_can_reset(st, r);
}

size_t set_page_resettable(struct settings_state *st, const struct set_page *p) {
	size_t n = 0;
	for (size_t i = 0; i < p->n; i++)
		n += page_resettable(st, p->rows[i]);
	return n;
}

static void reset_page(struct settings_state *st, const char *title) {
	struct set_page *p = NULL;
	for (size_t i = 0; i < st->n_pages && !p; i++)
		if (strcmp(st->pages[i].title, title) == 0) p = &st->pages[i];
	if (!p) return;
	if (stamps_changed(st)) reload(st);
	size_t done = 0;
	for (size_t i = 0; i < p->n; i++) {
		struct set_row *r = p->rows[i];
		if (!page_resettable(st, r)) continue;
		const char *cur = set_value(st, r);
		char *before = cur ? strdup(cur) : NULL;
		if (!write_value(st, r, NULL)) {
			free(before);
			if (r->error[0]) break;
			continue;
		}
		record_change(st, r, before, NULL);
		st->history[st->history_n - 1].chain = done > 0;
		done++;
	}
	if (done == 1) {
		undo_toast(st);
	} else if (done > 1) {
		set_toast(st, "Reset %zu settings on %s", done, p->title);
		undo_toast(st);
	}
}

void set_reset_page_ask(struct settings_state *st) {
	if (st->page >= st->n_pages) return;
	struct set_page *p = &st->pages[st->page];
	size_t n = set_page_resettable(st, p);
	if (!n) return;
	set_toast(st, "Reset %zu setting%s on %s?", n, n == 1 ? "" : "s", p->title);
	set_ask(st, "Reset", true, reset_page, strdup(p->title));
}

void set_toast_run(struct settings_state *st) {
	set_toast_action fn = st->toast_fn;
	char *arg = st->toast_arg;
	st->toast_fn = NULL;
	st->toast_arg = NULL;
	if (fn) fn(st, arg);
	free(arg);
}

void set_commit_edit(struct settings_state *st) {
	if (!st->editing) return;
	struct set_page *p = set_current_page(st);
	if (st->focus >= p->n) {
		st->editing = false;
		return;
	}
	struct set_row *r = p->rows[st->focus];
	char val[UI_TEXT_MAX];
	const char *s = st->edit.buf;
	while (*s == ' ')
		s++;
	snprintf(val, sizeof val, "%s", s);
	grabit_rstrip(val, strlen(val));
	set_apply(st, r, val);
	if (!r->error[0]) st->editing = false;
	ui_window_damage(st->win);
}

static void open_at(struct settings_state *st, const char *what) {
	if (!what || !*what) return;
	size_t n = strlen(what), hits = 0, hit = 0;
	for (size_t i = 0; i < st->n_pages; i++) {
		if (strcasecmp(st->pages[i].title, what) == 0) {
			st->page = i;
			return;
		}
		if (strncasecmp(st->pages[i].title, what, n) == 0) {
			hits++;
			hit = i;
		}
	}
	if (hits == 1) {
		st->page = hit;
		return;
	}
	ui_text_set(&st->search, what);
	settings_filter(st);
	st->area = AREA_SEARCH;
}

#define SIZE_KEY "gui.settings_size"

static void saved_size(struct config *cfg, int32_t *w, int32_t *h) {
	const char *v = config_get(cfg, SIZE_KEY);
	int sw = 0, sh = 0;
	char end = 0;
	if (!v || sscanf(v, "%dx%d%c", &sw, &sh, &end) != 2 || sw < 1 || sh < 1) return;
	*w = sw;
	*h = sh;
}

static void save_size(struct config *cfg, const struct ui_window *win) {
	if (ui_window_snapped(win) || win->width < 1 || win->height < 1) return;
	char v[32];
	snprintf(v, sizeof v, "%dx%d", win->width, win->height);
	const char *cur = config_get(cfg, SIZE_KEY);
	if (cur && strcmp(cur, v) == 0) return;
	const char *keys[] = {SIZE_KEY}, *vals[] = {v};
	(void)config_state_put(cfg, keys, vals, 1);
}

int settings_state_open(struct settings_state *st, struct grabit_wl_state *wls) {
	*st = (struct settings_state){
		.side_hover = SET_NONE,
		.hover_row = SET_NONE,
		.press_row = SET_NONE,
		.tip_row = SET_NONE,
		.last_click_row = SET_NONE,
		.area = AREA_SIDEBAR,
		.wls = wls,
	};
	if (reload(st) != 0) return -1;
	if (settings_build(st) == 0) return 0;
	log_error("settings: out of memory");
	settings_state_close(st);
	return -1;
}

void settings_state_close(struct settings_state *st) {
	set_icons_free(st);
	set_history_free(st);
	free(st->toast_arg);
	free(st->dialog.arg);
	set_jobs_free(st);
	set_tests_free(st);
	settings_free_rows(st);
	config_free(&st->cfg);
}

int grabit_settings_main(const char *what) {
	struct grabit_wl_state wls;
	if (grabit_wl_init(&wls) != 0) return 1;
	struct settings_state st;
	if (settings_state_open(&st, &wls) != 0) {
		grabit_wl_finish(&wls);
		return 1;
	}

	open_at(&st, what);
	set_icons_start(&st);

	struct ui_window win = {
		.user = &st,
		.on_draw = set_draw,
		.on_button = set_on_button,
		.on_motion = set_on_motion,
		.on_leave = set_on_leave,
		.on_scroll = set_on_scroll,
		.on_key = set_on_key,
		.on_focus_lost = set_on_focus_lost,
		.on_focus_gained = on_focus_gained,
		.on_timer = set_on_timer,
		.on_drag = set_on_drag,
		.on_drop = set_on_drop,
	};
	st.win = &win;
	set_arm_timer(&st);

	int rc = 1;
	int32_t w = 980, h = 680;
	saved_size(&st.cfg, &w, &h);
	if (ui_window_open(&win, &wls, "grabit settings", w, h) == 0) {
		rc = ui_window_run(&win) == 0 ? 0 : 1;
		set_commit_edit(&st);
		save_size(&st.cfg, &win);
	}
	ui_window_finish(&win);
	wl_display_roundtrip(wls.display);

	settings_state_close(&st);
	grabit_wl_finish(&wls);
	return rc;
}

int cmd_settings(int argc, char **argv) {
	if (argc > 0 && (strcmp(argv[0], "--help") == 0 || strcmp(argv[0], "-h") == 0)) {
		puts("Usage: grabit settings [<page>|<search>]");
		puts("");
		puts("Opens the settings ui.");
		puts("");
		puts("<page> opens that page (general, capture, region, editor, recording,");
		puts("uploads, plugins, ocr, cards, appearance, keybinds); anything else is");
		puts("searched.");
		return 0;
	}
	if (argc > 1) {
		log_error("usage: grabit settings [<page>|<search>]");
		return 2;
	}
	return grabit_settings_main(argc ? argv[0] : NULL);
}
