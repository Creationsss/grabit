// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _GNU_SOURCE
#include "ui/settings_internal.h"

#include "capture/backend.h"
#include "paths.h"
#include "region/keybinds.h"
#include "region/region.h"
#include "upload/sxcu.h"
#include "util/util.h"
#include "wl/wl.h"

#include <ctype.h>
#include <fcntl.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

bool set_row_has_icons(const struct set_row *r) {
	return r->info.kind == CFG_KIND_ENUM && r->info.values_src == CFG_VALUES_ICONS;
}

static void *scan_thread(void *data) {
	struct settings_state *st = data;
	ui_icons_run(st->icon_scan, &st->icons);
	atomic_store_explicit(&st->icons_done, true, memory_order_release);
	return NULL;
}

void set_icons_start(struct settings_state *st) {
	if (st->icons_state != ICONS_IDLE) return;
	st->icon_scan = ui_icons_prepare();
	if (!st->icon_scan) return;
	atomic_store(&st->icons_done, false);
	if (pthread_create(&st->icons_thread, NULL, scan_thread, st) == 0)
		st->icons_state = ICONS_SCANNING;
}

static void icons_join(struct settings_state *st) {
	if (st->icons_state != ICONS_SCANNING) return;
	pthread_join(st->icons_thread, NULL);
	st->icons_state = ICONS_READY;
	free(st->icon_scan);
	st->icon_scan = NULL;
}

bool set_icons_poll(struct settings_state *st) {
	if (st->icons_state == ICONS_SCANNING &&
		atomic_load_explicit(&st->icons_done, memory_order_acquire))
		icons_join(st);
	return st->icons_state == ICONS_READY;
}

static void icons_wait(struct settings_state *st) {
	if (st->icons_state == ICONS_READY) return;
	if (st->icons_state == ICONS_SCANNING) {
		icons_join(st);
		return;
	}
	ui_icons_scan(&st->icons);
	st->icons_state = ICONS_READY;
}

static bool already_listed(const struct set_row *r, size_t n, const char *name) {
	for (size_t i = 0; i < n; i++)
		if (strcmp(r->choices[i], name) == 0) return true;
	return false;
}

bool set_icons_ensure(struct settings_state *st, struct set_row *r, bool block) {
	if (!set_row_has_icons(r) || r->icons_loaded) return true;
	if (block)
		icons_wait(st);
	else if (!set_icons_poll(st))
		return false;
	r->icons_loaded = true;
	size_t pre = r->n_choices;
	for (size_t i = 0; r->info.values[i]; i++) {
		const char *v = r->info.values[i];
		if (!ui_icons_path(&st->icons, v) || already_listed(r, pre, v)) continue;
		if (set_push_choice(r, v) != 0) return true;
	}
	const char **names = NULL;
	if (ui_icons_sorted_names(&st->icons, &names) != 0) return true;
	for (size_t i = 0; i < st->icons.n; i++) {
		if (cfg_in_list(names[i], r->info.values) || already_listed(r, pre, names[i])) continue;
		if (set_push_choice(r, names[i]) != 0) break;
	}
	free(names);
	return true;
}

cairo_surface_t *set_icon(struct settings_state *st, const char *name, int px) {
	if (st->icons_state != ICONS_READY || !name || !name[0]) return NULL;
	for (size_t i = 0; i < SET_ICON_CACHE; i++) {
		struct set_icon_slot *c = &st->icon_cache[i];
		if (c->name && c->px == px && strcmp(c->name, name) == 0) return c->surface;
	}
	struct set_icon_slot *c = &st->icon_cache[st->icon_cache_next];
	st->icon_cache_next = (st->icon_cache_next + 1) % SET_ICON_CACHE;
	if (c->surface) cairo_surface_destroy(c->surface);
	c->name = name;
	c->px = px;
	char *file = ui_icon_file(name);
	c->surface = ui_icon_render(file ? file : ui_icons_path(&st->icons, name), px);
	free(file);
	return c->surface;
}

void set_icons_free(struct settings_state *st) {
	icons_join(st);
	free(st->icon_scan);
	st->icon_scan = NULL;
	for (size_t i = 0; i < SET_ICON_CACHE; i++)
		if (st->icon_cache[i].surface) cairo_surface_destroy(st->icon_cache[i].surface);
	memset(st->icon_cache, 0, sizeof st->icon_cache);
	ui_icons_free(&st->icons);
	st->icons_state = ICONS_IDLE;
	free(st->popup_map);
	st->popup_map = NULL;
	st->popup_map_n = st->popup_map_cap = 0;
}
