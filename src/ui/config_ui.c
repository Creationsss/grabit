// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "ui/config_ui.h"

#include "config.h"
#include "config_internal.h"
#include "log.h"
#include "paths.h"
#include "ui/config_ui_internal.h"
#include "ui/window.h"
#include "util.h"
#include "wl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int tab_for(const char *key) {
	if (!strchr(key, '.')) return 0;
	if (!strncmp(key, "recording.", 10)) return 1;
	if (!strncmp(key, "edit.", 5)) return 2;
	if (!strncmp(key, "jpeg.", 5) || !strncmp(key, "webp.", 5) ||
		!strncmp(key, "capture.", 8) || !strncmp(key, "region.", 7))
		return 3;
	if (!strncmp(key, "ocr.", 4) || !strncmp(key, "translate.", 10) ||
		!strncmp(key, "text_card.", 10))
		return 4;
	if (!strncmp(key, "preview.", 8) || !strncmp(key, "sound.", 6)) return 5;
	return 0;
}

static char *initial_value(const struct cfg_key_desc *d, const char *cur) {
	const char *s;
	if (cur)
		s = cur;
	else if (d->def)
		s = d->def;
	else if (d->kind == CFG_BOOL)
		s = "false";
	else if (d->kind == CFG_ENUM)
		s = d->allow_empty ? "" : d->vals[0];
	else
		s = "";
	char *out = strdup(s);
	return out ? out : strdup("");
}

static int build_tabs(struct cfg_ui *u) {
	for (size_t i = 0; i < u->n_keys; i++)
		u->tab_n[tab_for(u->keys[i].key)]++;
	for (int t = 0; t < NTAB; t++) {
		u->tab_keys[t] = calloc(u->tab_n[t] ? u->tab_n[t] : 1, sizeof **u->tab_keys);
		if (!u->tab_keys[t]) return -1;
		u->tab_n[t] = 0;
	}
	for (size_t i = 0; i < u->n_keys; i++) {
		int t = tab_for(u->keys[i].key);
		u->tab_keys[t][u->tab_n[t]++] = (int)i;
	}
	return 0;
}

static void backup_config(void) {
	const char *src = paths_config_file();
	char *buf = NULL;
	size_t len = 0;
	if (!src || grabit_read_file(src, 1u << 20, &buf, &len) != 0) return;
	char rt[512], path[4096];
	if (grabit_runtime_dir(rt, sizeof rt) == 0)
		snprintf(path, sizeof path, "%s/grabit-config.bak.toml", rt);
	else
		snprintf(path, sizeof path, "/tmp/grabit-config.bak.toml");
	if (paths_atomic_write(path, buf, len) == 0) {
		log_info("config: changes apply live; previous config backed up to %s", path);
		log_info("  revert with: cp %s %s", path, src);
	}
	free(buf);
}

int grabit_config_ui(void) {
	struct grabit_wl_state s;
	if (grabit_wl_init(&s) != 0) return 1;

	struct cfg_ui u = {0};
	u.editing = -1;
	u.pick_fd = -1;
	u.dd_open = -1;
	if (config_load(&u.cfg) != 0) {
		grabit_wl_finish(&s);
		return 1;
	}
	u.keys = cfg_key_descs(&u.n_keys);
	u.val = calloc(u.n_keys, sizeof *u.val);
	if (!u.val || build_tabs(&u) != 0) {
		log_error("config: out of memory");
		goto cleanup;
	}
	for (size_t i = 0; i < u.n_keys; i++)
		u.val[i] = initial_value(&u.keys[i], config_get(&u.cfg, u.keys[i].key));

	u.monitors = calloc(s.n_outputs ? s.n_outputs : 1, sizeof *u.monitors);
	if (u.monitors) {
		for (size_t k = 0; k < s.n_outputs; k++) {
			struct grabit_output *mo = s.outputs[k];
			if (mo && !mo->dead && mo->name) u.monitors[u.n_monitors++] = mo->name;
		}
	}

	backup_config();

	int max_tab = 1;
	for (int t = 0; t < NTAB; t++)
		if (u.tab_n[t] > max_tab) max_tab = u.tab_n[t];
	struct grabit_output *o = grabit_wl_primary_output(&s);
	int avail = o && o->logical_height > 0 ? (int)(o->logical_height * 0.78) : 720;
	int fit = (avail - TABBAR_H - FOOTER_H) / ROW_H;
	u.n_visible = max_tab < fit ? max_tab : fit;
	if (u.n_visible < 3) u.n_visible = 3;
	int panel_h = TABBAR_H + u.n_visible * ROW_H + FOOTER_H;
	u.panel_h = panel_h;

	struct ui_window_opts opts = {
		.wls = &s,
		.width = PANEL_W,
		.height = panel_h,
		.name = "grabit-config",
		.title = "grabit config",
		.user = &u,
		.on_draw = cfg_ui_draw,
		.on_pointer = cfg_ui_pointer,
		.on_key = cfg_ui_key,
	};
	u.win = ui_window_create(&opts);
	if (!u.win) {
		log_error("config: could not open window");
		goto cleanup;
	}
	ui_window_run(u.win);

cleanup:
	if (u.pick_fd >= 0) close(u.pick_fd);
	if (u.win) ui_window_destroy(u.win);
	for (size_t i = 0; i < u.n_keys; i++)
		free(u.val[i]);
	free(u.val);
	free(u.monitors);
	for (int t = 0; t < NTAB; t++)
		free(u.tab_keys[t]);
	config_free(&u.cfg);
	grabit_wl_finish(&s);
	return u.win ? 0 : 1;
}
