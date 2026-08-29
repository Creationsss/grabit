// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_CAIRO_UTIL_H
#define GRABIT_CAIRO_UTIL_H

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include <cairo/cairo.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static inline void grabit_cairo_set_source_argb(cairo_t *cr, uint32_t color, double alpha) {
	double r = ((color >> 16) & 0xff) / 255.0;
	double g = ((color >> 8) & 0xff) / 255.0;
	double b = (color & 0xff) / 255.0;
	cairo_set_source_rgba(cr, r, g, b, alpha);
}

static inline cairo_surface_t *grabit_cairo_image(void *data, cairo_format_t fmt,
												  int32_t w, int32_t h, int32_t stride) {
	cairo_surface_t *s = cairo_image_surface_create_for_data(data, fmt, w, h, stride);
	if (cairo_surface_status(s) != CAIRO_STATUS_SUCCESS) {
		cairo_surface_destroy(s);
		return NULL;
	}
	return s;
}

static inline cairo_surface_t *grabit_cairo_image_argb(void *data, int32_t w, int32_t h,
													   int32_t stride) {
	return grabit_cairo_image(data, CAIRO_FORMAT_ARGB32, w, h, stride);
}

static inline void grabit_cairo_rounded_rect(cairo_t *cr, double x, double y,
											 double w, double h, double r) {
	cairo_new_sub_path(cr);
	cairo_arc(cr, x + r, y + r, r, M_PI, 1.5 * M_PI);
	cairo_arc(cr, x + w - r, y + r, r, 1.5 * M_PI, 2.0 * M_PI);
	cairo_arc(cr, x + w - r, y + h - r, r, 0.0, 0.5 * M_PI);
	cairo_arc(cr, x + r, y + h - r, r, 0.5 * M_PI, M_PI);
	cairo_close_path(cr);
}

static inline double grabit_cairo_clamp_r(double w, double h, double r) {
	double max = (w < h ? w : h) / 2.0;
	return r > max ? max : r;
}

cairo_surface_t *grabit_cairo_promote_argb32(cairo_surface_t *src);

void grabit_cairo_rect_r(cairo_t *cr, double x, double y, double w, double h, double r);
void grabit_cairo_arrow(cairo_t *cr, double x0, double y0, double x1, double y1,
						double width, double min_head);
double grabit_cairo_arrow_extent(double width, double min_head, double len);

static inline void grabit_cairo_rect_r_inset(cairo_t *cr, double x, double y,
											 double w, double h, double r, double s) {
	grabit_cairo_rect_r(cr, x + 0.5 * s, y + 0.5 * s, w - s, h - s, r - 0.5 * s);
}

static inline void grabit_cairo_punch_corners(cairo_t *cr, double w, double h, double r) {
	if (r <= 0 || w <= 0 || h <= 0) return;
	cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
	cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
	cairo_rectangle(cr, 0, 0, w, h);
	grabit_cairo_rect_r(cr, 0, 0, w, h, r);
	cairo_fill(cr);
}

#endif
