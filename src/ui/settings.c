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
#include <sys/stat.h>

static void update_banner(struct settings_state *st) {
	st->banner[0] = '\0';
	if (st->cfg.unparsed)
		snprintf(st->banner, sizeof st->banner,
				 "%s does not parse; fix it before changing settings here",
				 paths_config_file());
}

static int64_t file_stamp(const char *path) {
	struct stat sb;
	if (!path || stat(path, &sb) != 0) return 0;
	return (int64_t)sb.st_mtim.tv_sec * 1000000000 + sb.st_mtim.tv_nsec;
}

static bool stamps_changed(const struct settings_state *st) {
	return file_stamp(paths_config_file()) != st->cfg_stamp ||
		   file_stamp(paths_state_file()) != st->state_stamp;
}

static int reload(struct settings_state *st) {
	struct config fresh;
	int64_t cfg = file_stamp(paths_config_file()), state = file_stamp(paths_state_file());
	if (config_load_full(&fresh) != 0) return -1;
	config_free(&st->cfg);
	st->cfg = fresh;
	st->cfg_stamp = cfg;
	st->state_stamp = state;
	grabit_ui_theme_init(&st->cfg);
	update_banner(st);
	return 0;
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

bool set_add_sxcu(struct settings_state *st, const char *path_or_uri) {
	struct set_row *r = set_sxcu_row(st);
	if (!r) return false;
	char raw[UI_TEXT_MAX];
	uri_to_path(path_or_uri, raw, sizeof raw);
	char *path = paths_expand_tilde(raw);
	char err[sizeof r->error];
	log_capture_errors(err, sizeof err);
	bool replaced = false;
	int rc = path ? sxcu_dir_add(path, true, &replaced) : -1;
	log_capture_errors(NULL, 0);
	free(path);
	if (rc != 0) {
		if (!r->error[0])
			snprintf(r->error, sizeof r->error, "%s", err[0] ? err : "could not add this uploader");
		ui_window_damage(st->win);
		return false;
	}
	set_refresh_uploaders(st);
	set_toast(st, replaced ? "Uploader replaced" : "Uploader added");
	return true;
}

void set_apply(struct settings_state *st, struct set_row *r, const char *value) {
	if (value && !value[0]) value = NULL;
	if (r->sxcu_add) {
		r->error[0] = '\0';
		if (value) set_add_sxcu(st, value);
		return;
	}
	if (stamps_changed(st)) reload(st);
	if (st->cfg.unparsed) {
		snprintf(r->error, sizeof r->error, "%s", st->banner);
		ui_window_damage(st->win);
		return;
	}
	const char *cur = set_value(st, r);
	const char *same = cur ? cur : r->info.def;
	if (value ? same && strcmp(value, same) == 0 : !cur) {
		r->error[0] = '\0';
		return;
	}
	char err[sizeof r->error];
	log_capture_errors(err, sizeof err);
	int rc = config_write_key(r->key, value);
	log_capture_errors(NULL, 0);
	if (rc != 0) {
		snprintf(r->error, sizeof r->error, "%s", err[0] ? err : "could not save this setting");
	} else {
		r->error[0] = '\0';
		reload(st);
		toast_change(st, r, value == NULL);
	}
	ui_window_damage(st->win);
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

int grabit_settings_main(const char *what) {
	struct settings_state st = {
		.side_hover = SET_NONE,
		.hover_row = SET_NONE,
		.press_row = SET_NONE,
		.tip_row = SET_NONE,
		.last_click_row = SET_NONE,
		.area = AREA_SIDEBAR,
	};
	if (reload(&st) != 0) return 1;

	struct grabit_wl_state wls;
	if (grabit_wl_init(&wls) != 0) {
		config_free(&st.cfg);
		return 1;
	}
	st.wls = &wls;

	if (settings_build(&st) != 0) {
		log_error("settings: out of memory");
		settings_free_rows(&st);
		grabit_wl_finish(&wls);
		config_free(&st.cfg);
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
		.on_timer = set_on_timer,
		.on_drag = set_on_drag,
		.on_drop = set_on_drop,
	};
	st.win = &win;
	set_arm_timer(&st);

	int rc = 1;
	if (ui_window_open(&win, &wls, "grabit settings", 980, 680) == 0) {
		rc = ui_window_run(&win) == 0 ? 0 : 1;
		set_commit_edit(&st);
	}
	ui_window_finish(&win);
	wl_display_roundtrip(wls.display);

	set_icons_free(&st);
	settings_free_rows(&st);
	grabit_wl_finish(&wls);
	config_free(&st.cfg);
	return rc;
}

int cmd_settings(int argc, char **argv) {
	if (argc > 0 && (strcmp(argv[0], "--help") == 0 || strcmp(argv[0], "-h") == 0)) {
		puts("Usage: grabit settings [<page>|<search>]");
		puts("");
		puts("Opens the settings window. Every change is validated and written to");
		puts("config.toml right away, the same as `grabit set`.");
		puts("");
		puts("<page> opens that page (general, capture, region, editor, recording,");
		puts("uploads, ocr, cards, appearance, keybinds); anything else is searched.");
		return 0;
	}
	if (argc > 1) {
		log_error("usage: grabit settings [<page>|<search>]");
		return 2;
	}
	return grabit_settings_main(argc ? argv[0] : NULL);
}
