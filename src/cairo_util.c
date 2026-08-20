// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "cairo_util.h"

void grabit_cairo_rect_r(cairo_t *cr, double x, double y, double w, double h, double r) {
	if (r <= 0.0 || w <= 0.0 || h <= 0.0) {
		cairo_rectangle(cr, x, y, w, h);
		return;
	}
	grabit_cairo_rounded_rect(cr, x, y, w, h, grabit_cairo_clamp_r(w, h, r));
}
