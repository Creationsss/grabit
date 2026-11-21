// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_UI_ICONS_H
#define GRABIT_UI_ICONS_H

#include <stddef.h>

#include <cairo/cairo.h>

struct ui_icon_entry {
	char *name;
	char *path;
	int rank;
};

struct ui_icons {
	struct ui_icon_entry *v;
	size_t n;
	size_t cap;
	size_t *slots;
	size_t n_slots;
};

struct ui_icon_scan;

struct ui_icon_scan *ui_icons_prepare(void);
void ui_icons_run(const struct ui_icon_scan *s, struct ui_icons *ix);
int ui_icons_scan(struct ui_icons *ix);
const char *ui_icons_path(const struct ui_icons *ix, const char *name);
int ui_icons_sorted_names(const struct ui_icons *ix, const char ***out);
void ui_icons_free(struct ui_icons *ix);
cairo_surface_t *ui_icon_render(const char *path, int px);

#endif
