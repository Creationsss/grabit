// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_WM_H
#define GRABIT_WM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct rect;
struct grabit_wl_state;
struct grabit_output;

const char *grabit_wm_current_name(void);

int grabit_wm_active_window(char **class_out, char **title_out);
int grabit_wm_active_window_rect(struct rect *out);
int grabit_wm_windows(struct rect **out, size_t *n_out);

struct grabit_output *grabit_wm_active_output(struct grabit_wl_state *s);

int grabit_wm_capture_active_window(bool cursor, const char *png_path);

#endif
