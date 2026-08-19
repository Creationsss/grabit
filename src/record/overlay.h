// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_RECORD_OVERLAY_H
#define GRABIT_RECORD_OVERLAY_H

#include <stdbool.h>
#include <stdint.h>

struct grabit_wl_state;
struct rect;
struct overlay_state;

struct overlay_state *overlay_start(struct grabit_wl_state *s, struct rect r,
									bool show_dimensions, int32_t corner_radius,
									bool rounded_ui);
void overlay_stop(struct overlay_state *st);

#endif
