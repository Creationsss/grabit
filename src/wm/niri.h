// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_WM_NIRI_H
#define GRABIT_WM_NIRI_H

#include <stdbool.h>
#include <stddef.h>

struct rect;
struct snap_window;

bool grabit_niri_present(void);
int grabit_niri_active_window(char **class_out, char **title_out);
int grabit_niri_active_window_rect(struct rect *out);
int grabit_niri_active_window_radius(void);
int grabit_niri_windows(struct snap_window **out, size_t *n_out);
char *grabit_niri_focused_output(void);
int grabit_niri_capture_active_window(bool cursor, const char *png_path);

#endif
