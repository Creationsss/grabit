// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "ui/settings_input.h"

#include "clipboard/clipboard.h"
#include "ui/window.h"
#include "util/util.h"
#include "wl/xkb.h"

#include <linux/input-event-codes.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

void set_on_drag(struct ui_window *w, bool over) {
	struct settings_state *st = w->user;
	st->drag_over = over;
	ui_window_damage(w);
}

static bool ends_with(const char *s, size_t n, const char *ext) {
	size_t e = strlen(ext);
	return n >= e && strncasecmp(s + n - e, ext, e) == 0;
}

static void show_row(struct settings_state *st, const struct set_row *r) {
	if (!r) return;
	for (size_t i = 0; i < st->n_pages; i++)
		for (size_t j = 0; j < st->pages[i].n; j++)
			if (st->pages[i].rows[j] == r) {
				set_select_page(st, i);
				st->area = AREA_BODY;
				st->focus = j;
				set_reveal_focus(st);
				return;
			}
}

static char *plugin_folder(const char *line) {
	char *path = set_resolve_path(line);
	char manifest[UI_TEXT_MAX + 16];
	if (path && snprintf(manifest, sizeof manifest, "%s/manifest.toml", path) > 0 &&
		access(manifest, R_OK) == 0)
		return path;
	free(path);
	return NULL;
}

enum { DROP_UPLOADER = 1,
	   DROP_PLUGIN = 2,
	   DROP_ICON = 4 };

static unsigned drop_one(struct settings_state *st, const char *line, size_t len, unsigned seen) {
	if (ends_with(line, len, ".sxcu")) {
		if (!(seen & DROP_UPLOADER)) show_row(st, set_sxcu_row(st));
		set_add_sxcu(st, line);
		return DROP_UPLOADER;
	}
	if (ui_icon_supported(line)) {
		struct set_row *icon = set_find_row(st, "tray.icon");
		char *path = icon && !(seen & DROP_ICON) ? set_resolve_path(line) : NULL;
		if (!path) return 0;
		show_row(st, icon);
		set_apply(st, icon, path);
		free(path);
		return DROP_ICON;
	}
	if (seen & DROP_PLUGIN) return 0;
	char *plugin = plugin_folder(line);
	if (!plugin) return 0;
	show_row(st, set_submit_row(st, set_install_plugin));
	set_install_plugin(st, plugin);
	free(plugin);
	return DROP_PLUGIN;
}

void set_on_drop(struct ui_window *w, const char *uri_list) {
	struct settings_state *st = w->user;
	st->drag_over = false;
	if (!set_leave_edit(st)) set_cancel_edit(st);
	set_close_popup(st);
	set_capture_end(st, false);
	struct set_row *target = set_drop_row(st, false);
	if (target) {
		target->custom->drop(st, target, uri_list);
		show_row(st, target);
		ui_window_damage(w);
		return;
	}
	unsigned seen = 0;
	for (const char *p = uri_list; *p;) {
		const char *e = strchr(p, '\n');
		size_t n = e ? (size_t)(e - p) : strlen(p);
		char line[UI_TEXT_MAX];
		size_t len = n < sizeof line ? n : sizeof line - 1;
		memcpy(line, p, len);
		line[len] = '\0';
		len = grabit_rstrip(line, len);
		if (len && line[0] != '#') seen |= drop_one(st, line, len, seen);
		if (!e) break;
		p = e + 1;
	}
	if (!seen) set_toast(st, "%s", set_drop_hint(st)->refused);
	ui_window_damage(w);
}
