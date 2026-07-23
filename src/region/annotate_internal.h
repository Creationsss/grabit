// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_REGION_ANNOTATE_INTERNAL_H
#define GRABIT_REGION_ANNOTATE_INTERNAL_H

#include <stdint.h>

#include <cairo/cairo.h>

void ganno_set_color(cairo_t *cr, uint32_t color);
void ganno_paint_blur(cairo_t *cr, double x, double y, double w, double h,
					  double scale, int32_t strength, cairo_surface_t *backdrop);
void ganno_paint_pixelate(cairo_t *cr, double x, double y, double w, double h,
						  double scale, int32_t strength, cairo_surface_t *backdrop);

#endif
