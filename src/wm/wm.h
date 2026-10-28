// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_WM_H
#define GRABIT_WM_H

#include "util/rect.h"

#include <stdbool.h>
#include <stddef.h>

struct grabit_wl_state;
struct grabit_output;

const char *grabit_wm_current_name(void);

int grabit_wm_active_window(char **class_out, char **title_out);
int grabit_wm_active_window_rect(struct rect *out);
struct wm_window {
	struct rect r;
	char *window_class;
	char *title;
};

int grabit_wm_windows(const char *ignore, struct wm_window **out, size_t *n_out);
void grabit_wm_window_clear(struct wm_window *w);
void grabit_wm_windows_free(struct wm_window *w, size_t n);
int grabit_wm_window_radius(const struct rect *win);
int grabit_wm_window_border(const struct rect *win);

struct grabit_output *grabit_wm_active_output(struct grabit_wl_state *s);

int grabit_wm_capture_active_window(bool cursor, const char *png_path);

#endif
