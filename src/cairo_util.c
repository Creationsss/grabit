// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "cairo_util.h"

#define ARROW_HEAD_LEN 4.4
#define ARROW_HEAD_W 2.5

struct arrow_head {
	double len, w;
};

cairo_surface_t *grabit_cairo_promote_argb32(cairo_surface_t *src) {
	cairo_surface_t *out = cairo_image_surface_create(
		CAIRO_FORMAT_ARGB32, cairo_image_surface_get_width(src),
		cairo_image_surface_get_height(src));
	if (cairo_surface_status(out) != CAIRO_STATUS_SUCCESS) {
		cairo_surface_destroy(out);
		return NULL;
	}
	cairo_t *cr = cairo_create(out);
	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
	cairo_set_source_surface(cr, src, 0, 0);
	cairo_paint(cr);
	cairo_destroy(cr);
	return out;
}

void grabit_cairo_rect_r(cairo_t *cr, double x, double y, double w, double h, double r) {
	if (r <= 0.0 || w <= 0.0 || h <= 0.0) {
		cairo_rectangle(cr, x, y, w, h);
		return;
	}
	grabit_cairo_rounded_rect(cr, x, y, w, h, grabit_cairo_clamp_r(w, h, r));
}

static struct arrow_head arrow_head_geom(double width, double min_head) {
	double hl = width * ARROW_HEAD_LEN;
	double hw = width * ARROW_HEAD_W;
	if (hl < min_head) {
		hw *= min_head / hl;
		hl = min_head;
	}
	return (struct arrow_head){hl, hw};
}

double grabit_cairo_arrow_extent(double width, double min_head, double len) {
	if (width <= 0.0) return 0.0;
	struct arrow_head h = arrow_head_geom(width, min_head);
	if (len > 0.0 && h.len > len * 0.5) h.w *= len * 0.5 / h.len;
	return h.w + width * 0.5;
}

void grabit_cairo_arrow(cairo_t *cr, double x0, double y0, double x1, double y1,
						double width, double min_head) {
	if (width <= 0.0) return;
	double dx = x1 - x0, dy = y1 - y0;
	double len = sqrt(dx * dx + dy * dy);
	if (len < 1.0) {
		cairo_new_sub_path(cr);
		cairo_arc(cr, x0, y0, width * 0.5, 0, 2.0 * M_PI);
		cairo_fill(cr);
		return;
	}

	double ux = dx / len, uy = dy / len;
	double px = -uy, py = ux;
	double cap = width * 0.5;
	struct arrow_head h = arrow_head_geom(width, min_head);
	if (h.len > len * 0.5) {
		h.w *= len * 0.5 / h.len;
		h.len = len * 0.5;
	}

	double tx = x1 - ux * cap, ty = y1 - uy * cap;
	double bx = tx - ux * h.len, by = ty - uy * h.len;

	cairo_save(cr);
	cairo_set_line_width(cr, width);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
	if (len > width) {
		cairo_move_to(cr, x0 + ux * cap, y0 + uy * cap);
		cairo_line_to(cr, tx, ty);
	}
	cairo_move_to(cr, bx + px * h.w, by + py * h.w);
	cairo_line_to(cr, tx, ty);
	cairo_line_to(cr, bx - px * h.w, by - py * h.w);
	cairo_stroke(cr);
	cairo_restore(cr);
}
