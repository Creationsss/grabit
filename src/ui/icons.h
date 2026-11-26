// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_UI_ICONS_H
#define GRABIT_UI_ICONS_H

#include <stdbool.h>
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
bool ui_icon_render_sizes(const char *path, const int *px, size_t n, cairo_surface_t **out);
bool ui_icon_supported(const char *path);
bool ui_icon_is_file(const char *value);
char *ui_icon_file(const char *value);

#endif
