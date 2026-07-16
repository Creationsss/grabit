// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_HYPRLAND_H
#define GRABIT_HYPRLAND_H

#include <stddef.h>
#include <stdint.h>

int grabit_hyprland_active_window(char **class_out, char **title_out);
int grabit_hyprland_cursorpos(int32_t *x_out, int32_t *y_out);

struct rect;
int grabit_hyprland_clients(struct rect **out, size_t *n_out);

#endif
