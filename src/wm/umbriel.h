// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_WM_UMBRIEL_H
#define GRABIT_WM_UMBRIEL_H

#include <stdbool.h>
#include <stddef.h>

struct rect;

bool grabit_umbriel_present(void);
int grabit_umbriel_active_window(char **class_out, char **title_out);
int grabit_umbriel_active_window_rect(struct rect *out);
int grabit_umbriel_windows(struct rect **out, size_t *n_out);
char *grabit_umbriel_focused_output(void);

#endif
