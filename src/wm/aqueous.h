// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_WM_AQUEOUS_H
#define GRABIT_WM_AQUEOUS_H

#include <stdbool.h>
#include <stddef.h>

struct rect;

bool grabit_aqueous_present(void);
int grabit_aqueous_active_window(char **class_out, char **title_out);
int grabit_aqueous_active_window_rect(struct rect *out);
int grabit_aqueous_window_border(const struct rect *win);
int grabit_aqueous_windows(struct rect **out, size_t *n_out);
char *grabit_aqueous_focused_output(void);

#endif
