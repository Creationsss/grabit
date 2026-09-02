// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "region/annotate.h"

#include "cairo_util.h"

#include "region/annotate_internal.h"
#include "region/region.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <cairo/cairo.h>

void ganno_set_color(cairo_t *cr, uint32_t color) {
	grabit_cairo_set_source_argb(cr, color, 1.0);
}

static void ganno_set_contrast_ink(cairo_t *cr, uint32_t color) {
	double lum = (0.299 * ((color >> 16) & 0xff) + 0.587 * ((color >> 8) & 0xff) +
				  0.114 * (color & 0xff)) /
				 255.0;
	double v = lum > 0.6 ? 0.0 : 1.0;
	cairo_set_source_rgba(cr, v, v, v, 1);
}

static void apply_stroke_style(cairo_t *cr, enum stroke_style style, double w) {
	double d[2];
	int n = grabit_stroke_dash(style, w, d);
	cairo_set_dash(cr, d, n, 0.0);
	if (n > 0) cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
}

static int32_t spot_strength(const struct annotation *a) {
	if (!a || !tool_is_layer(a->tool)) return 0;
	struct rect r = annotation_norm_rect(a);
	return (r.w >= 2 && r.h >= 2) ? annotation_width(a) : 0;
}

#define SMOOTH_WINDOW 3

static void polyline_path(cairo_t *cr, const int32_t *p, size_t n) {
	cairo_move_to(cr, p[0], p[1]);
	for (size_t i = 1; i < n; i++)
		cairo_line_to(cr, p[i * 2], p[i * 2 + 1]);
}

static void curve_push(cairo_t *cr, double *wx, double *wy, size_t *nw, double x, double y) {
	if (*nw < 4) {
		wx[*nw] = x;
		wy[*nw] = y;
		(*nw)++;
	} else {
		for (size_t j = 0; j < 3; j++) {
			wx[j] = wx[j + 1];
			wy[j] = wy[j + 1];
		}
		wx[3] = x;
		wy[3] = y;
	}
	if (*nw == 4)
		cairo_curve_to(cr, wx[1] + (wx[2] - wx[0]) / 6.0, wy[1] + (wy[2] - wy[0]) / 6.0,
					   wx[2] - (wx[3] - wx[1]) / 6.0, wy[2] - (wy[3] - wy[1]) / 6.0,
					   wx[2], wy[2]);
}

static void pen_arrow_head(cairo_t *cr, const struct annotation *a, double w,
						   double tip_x, double tip_y) {
	size_t n = a->n_points;
	if (n < 2) return;
	size_t last = n - 1;
	if (a->smooth && n >= 4) {
		if (last < (size_t)SMOOTH_WINDOW) return;
		last -= (size_t)SMOOTH_WINDOW;
	}
	double want = grabit_cairo_arrow_head_len(w, ANNO_ARROW_MIN_HEAD);
	double bx = 0.0, by = 0.0, blen = 0.0;
	for (size_t i = last + 1; i-- > 0;) {
		double dx = tip_x - a->points[i * 2], dy = tip_y - a->points[i * 2 + 1];
		double len = sqrt(dx * dx + dy * dy);
		if (len > blen) {
			bx = a->points[i * 2];
			by = a->points[i * 2 + 1];
			blen = len;
		}
		if (len >= want) break;
	}
	if (blen < 0.001) return;
	grabit_cairo_arrow_head(cr, bx, by, tip_x, tip_y, w, ANNO_ARROW_MIN_HEAD);
}

static void stroke_path(cairo_t *cr, const int32_t *p, size_t n, bool smooth, double lw,
						double *end_x, double *end_y) {
	*end_x = p[(n - 1) * 2];
	*end_y = p[(n - 1) * 2 + 1];
	if (!smooth || n < 4) {
		polyline_path(cr, p, n);
		return;
	}

	double step = lw * 0.75;
	if (step < 2.0) step = 2.0;

	size_t lo = 0;
	size_t hi = n - 1 < (size_t)SMOOTH_WINDOW ? n - 1 : (size_t)SMOOTH_WINDOW;
	double sx = 0, sy = 0;
	for (size_t j = lo; j <= hi; j++) {
		sx += p[j * 2];
		sy += p[j * 2 + 1];
	}

	double wx[4], wy[4];
	size_t nw = 0;
	double lastx = 0, lasty = 0;

	for (size_t i = 0; i < n; i++) {
		double cx = sx / (double)(hi - lo + 1);
		double cy = sy / (double)(hi - lo + 1);

		bool take = nw == 0 || i + 1 == n;
		if (!take) {
			double dx = cx - lastx, dy = cy - lasty;
			take = dx * dx + dy * dy >= step * step;
		}
		if (take) {
			if (nw == 0) {
				cairo_move_to(cr, cx, cy);
				curve_push(cr, wx, wy, &nw, cx, cy);
			}
			curve_push(cr, wx, wy, &nw, cx, cy);
			lastx = cx;
			lasty = cy;
		}

		if (i + 1 + (size_t)SMOOTH_WINDOW < n) {
			hi++;
			sx += p[hi * 2];
			sy += p[hi * 2 + 1];
		}
		if (i + 1 > (size_t)SMOOTH_WINDOW) {
			sx -= p[lo * 2];
			sy -= p[lo * 2 + 1];
			lo++;
		}
	}
	curve_push(cr, wx, wy, &nw, lastx, lasty);
	*end_x = lastx;
	*end_y = lasty;
}

static void spot_hole(cairo_t *cr, const struct annotation *a) {
	if (!spot_strength(a)) return;
	struct rect r = annotation_norm_rect(a);
	cairo_rectangle(cr, r.x, r.y, r.w, r.h);
}

void ganno_paint_spotlights(cairo_t *cr, const struct annotation_list *list,
							const struct annotation *extra) {
	int32_t strongest = spot_strength(extra);
	size_t n = list ? list->n : 0;
	for (size_t i = 0; i < n; i++) {
		int32_t s = spot_strength(&list->items[i]);
		if (s > strongest) strongest = s;
	}
	if (!strongest) return;

	cairo_save(cr);
	cairo_push_group(cr);
	cairo_set_source_rgba(cr, 0, 0, 0, fmin(0.20 + strongest * 0.055, 0.9));
	cairo_paint(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
	for (size_t i = 0; i < n; i++)
		spot_hole(cr, &list->items[i]);
	spot_hole(cr, extra);
	cairo_fill(cr);
	cairo_pop_group_to_source(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
	cairo_paint(cr);
	cairo_restore(cr);
}

void annotation_paint_backdrop(cairo_t *cr, const struct annotation *a, double scale,
							   cairo_surface_t *backdrop) {
	cairo_save(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
	double w = annotation_line_width(a) * scale;

	switch (a->tool) {
	case TOOL_RECT: {
		struct rect nr = annotation_norm_rect(a);
		double x = nr.x, y = nr.y, rw = nr.w, rh = nr.h;
		ganno_set_color(cr, a->color);
		cairo_set_line_width(cr, w);
		apply_stroke_style(cr, a->style, w);
		cairo_rectangle(cr, x, y, rw, rh);
		cairo_stroke(cr);
		break;
	}
	case TOOL_RRECT: {
		struct rect nr = annotation_norm_rect(a);
		double x = nr.x, y = nr.y, rw = nr.w, rh = nr.h;
		double r = (rw < rh ? rw : rh) * 0.22;
		ganno_set_color(cr, a->color);
		cairo_set_line_width(cr, w);
		apply_stroke_style(cr, a->style, w);
		grabit_cairo_rounded_rect(cr, x, y, rw, rh, r);
		cairo_stroke(cr);
		break;
	}
	case TOOL_ELLIPSE: {
		double cx = (a->x0 + a->x1) / 2.0;
		double cy = (a->y0 + a->y1) / 2.0;
		double rx = (a->x0 < a->x1 ? a->x1 - a->x0 : a->x0 - a->x1) / 2.0;
		double ry = (a->y0 < a->y1 ? a->y1 - a->y0 : a->y0 - a->y1) / 2.0;
		if (rx < 1.0 || ry < 1.0) break;
		ganno_set_color(cr, a->color);
		cairo_set_line_width(cr, w);
		apply_stroke_style(cr, a->style, w);
		cairo_save(cr);
		cairo_translate(cr, cx, cy);
		cairo_scale(cr, rx, ry);
		cairo_new_sub_path(cr);
		cairo_arc(cr, 0, 0, 1, 0, 2.0 * M_PI);
		cairo_restore(cr);
		cairo_stroke(cr);
		break;
	}
	case TOOL_ARROW:
		ganno_set_color(cr, a->color);
		grabit_cairo_arrow(cr, a->x0, a->y0, a->x1, a->y1, w, ANNO_ARROW_MIN_HEAD);
		break;
	case TOOL_LINE:
		ganno_set_color(cr, a->color);
		cairo_set_line_width(cr, w);
		cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
		apply_stroke_style(cr, a->style, w);
		cairo_move_to(cr, a->x0, a->y0);
		cairo_line_to(cr, a->x1, a->y1);
		cairo_stroke(cr);
		break;
	case TOOL_PEN:
	case TOOL_MARKER:
	case TOOL_ARROW_PEN:
	case TOOL_ERASER: {
		if (a->n_points < 1) break;
		if (a->tool == TOOL_MARKER)
			grabit_cairo_set_source_argb(cr, a->color, 0.4);
		else if (a->tool == TOOL_ERASER)
			cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
		else
			ganno_set_color(cr, a->color);
		cairo_set_line_width(cr, w);
		cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
		cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
		if (tool_uses_line_style(a->tool)) apply_stroke_style(cr, a->style, w);
		double ex, ey;
		stroke_path(cr, a->points, a->n_points, a->smooth, w, &ex, &ey);
		if (a->n_points == 1) {
			cairo_arc(cr, a->points[0], a->points[1], w / 2.0, 0, 2.0 * M_PI);
			cairo_fill(cr);
		} else {
			cairo_stroke(cr);
		}
		if (a->tool == TOOL_ARROW_PEN) pen_arrow_head(cr, a, w, ex, ey);
		break;
	}
	case TOOL_BLUR:
	case TOOL_PIXELATE: {
		struct rect nr = annotation_norm_rect(a);
		double x = nr.x, y = nr.y, rw = nr.w, rh = nr.h;
		if (rw < 2.0 || rh < 2.0) break;
		int32_t strength = annotation_width(a);
		if (a->tool == TOOL_BLUR)
			ganno_paint_blur(cr, x, y, rw, rh, scale, strength, backdrop);
		else
			ganno_paint_pixelate(cr, x, y, rw, rh, scale, strength, backdrop);
		break;
	}
	case TOOL_SPOTLIGHT:
		break;
	case TOOL_TEXT: {
		if (!a->text || !a->text[0]) break;
		double font_px = annotation_font_size(a) * scale;
		cairo_select_font_face(cr, "sans-serif",
							   CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
		cairo_set_font_size(cr, font_px);
		ganno_set_color(cr, a->color);
		cairo_move_to(cr, a->x0, a->y0);
		cairo_show_text(cr, a->text);
		break;
	}
	case TOOL_CALLOUT: {
		if (!a->text) break;
		double fs = annotation_font_size(a) * scale;
		cairo_select_font_face(cr, "sans-serif",
							   CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
		cairo_set_font_size(cr, fs);
		cairo_text_extents_t ext;
		cairo_text_extents(cr, a->text, &ext);
		if (!a->text[0]) {
			cairo_font_extents_t fe;
			cairo_font_extents(cr, &fe);
			ext.width = fs * 0.5;
			ext.height = fe.ascent;
			ext.y_bearing = -fe.ascent;
		}

		double pad = fs * 0.45;
		double bx = a->x0 - pad;
		double by = a->y0 + ext.y_bearing - pad;
		double bw = ext.width + pad * 2;
		double bh = ext.height + pad * 2;
		double bcx = bx + bw / 2.0, bcy = by + bh / 2.0;

		ganno_set_color(cr, a->color);
		double rad = fmin(bh * 0.32, fs * 0.5);
		double dx = a->x1 - bcx, dy = a->y1 - bcy;
		double hx = bw / 2.0, hy = bh / 2.0;
		if (fabs(dx) > hx || fabs(dy) > hy) {
			double tx = hx / fabs(dx), ty = hy / fabs(dy);
			bool vert = ty <= tx;
			double len = sqrt(dx * dx + dy * dy);
			double half = fmin(fs * 0.42, len * 0.32);
			double inset = fmin(rad, hx * 0.8);
			double b1x, b1y, b2x, b2y;
			if (vert) {
				double lo = bx + rad, hi = bx + bw - rad;
				half = fmin(half, fmax((hi - lo) / 2.0, 1.0));
				double c = fmin(fmax(bcx + dx * ty, lo + half), hi - half);
				double y = dy < 0 ? by + inset : by + bh - inset;
				b1x = c - half, b1y = y, b2x = c + half, b2y = y;
			} else {
				double lo = by + rad, hi = by + bh - rad;
				half = fmin(half, fmax((hi - lo) / 2.0, 1.0));
				double c = fmin(fmax(bcy + dy * tx, lo + half), hi - half);
				double x = dx < 0 ? bx + inset : bx + bw - inset;
				b1x = x, b1y = c - half, b2x = x, b2y = c + half;
			}
			cairo_move_to(cr, b1x, b1y);
			cairo_line_to(cr, a->x1, a->y1);
			cairo_line_to(cr, b2x, b2y);
			cairo_close_path(cr);
			cairo_fill(cr);
		}
		grabit_cairo_rounded_rect(cr, bx, by, bw, bh, rad);
		cairo_fill(cr);

		ganno_set_contrast_ink(cr, a->color);
		cairo_move_to(cr, a->x0, a->y0);
		cairo_show_text(cr, a->text);
		break;
	}
	case TOOL_COUNTER: {
		if (!a->text || !a->text[0]) break;
		double R = annotation_counter_radius(a) * scale;
		ganno_set_color(cr, a->color);
		cairo_new_sub_path(cr);
		cairo_arc(cr, a->x0, a->y0, R, 0, 2.0 * M_PI);
		cairo_fill(cr);

		ganno_set_contrast_ink(cr, a->color);
		cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL,
							   CAIRO_FONT_WEIGHT_BOLD);
		double fs = R * 1.35;
		cairo_set_font_size(cr, fs);
		cairo_text_extents_t ext;
		cairo_text_extents(cr, a->text, &ext);
		double maxw = R * 1.5;
		if (ext.width > maxw && ext.width > 0) {
			cairo_set_font_size(cr, fs * maxw / ext.width);
			cairo_text_extents(cr, a->text, &ext);
		}
		cairo_move_to(cr, a->x0 - ext.x_advance / 2.0,
					  a->y0 - ext.height / 2.0 - ext.y_bearing);
		cairo_show_text(cr, a->text);
		break;
	}
	case TOOL_COUNT:
		break;
	}
	cairo_restore(cr);
}

void annotation_paint(cairo_t *cr, const struct annotation *a, double scale) {
	annotation_paint_backdrop(cr, a, scale, NULL);
}

void annotation_list_paint(cairo_t *cr, const struct annotation_list *list,
						   int32_t origin_x, int32_t origin_y, double scale) {
	if (!list || list->n == 0) return;
	cairo_surface_t *target = cairo_get_target(cr);
	cairo_surface_t *overlay = NULL;
	if (cairo_surface_get_type(target) == CAIRO_SURFACE_TYPE_IMAGE) {
		overlay = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
											 cairo_image_surface_get_width(target),
											 cairo_image_surface_get_height(target));
		if (cairo_surface_status(overlay) != CAIRO_STATUS_SUCCESS) {
			cairo_surface_destroy(overlay);
			overlay = NULL;
		}
	}

	cairo_save(cr);
	cairo_translate(cr, -origin_x * scale, -origin_y * scale);
	cairo_scale(cr, scale, scale);
	if (!overlay) {
		cairo_push_group(cr);
		ganno_paint_spotlights(cr, list, NULL);
		for (size_t i = 0; i < list->n; i++)
			annotation_paint(cr, &list->items[i], 1.0);
		cairo_pop_group_to_source(cr);
		cairo_paint(cr);
		cairo_restore(cr);
		return;
	}
	cairo_restore(cr);

	cairo_t *oc = cairo_create(overlay);
	cairo_translate(oc, -origin_x * scale, -origin_y * scale);
	cairo_scale(oc, scale, scale);
	ganno_paint_spotlights(oc, list, NULL);
	for (size_t i = 0; i < list->n; i++)
		annotation_paint_backdrop(oc, &list->items[i], 1.0, target);
	cairo_destroy(oc);
	cairo_surface_flush(overlay);

	cairo_save(cr);
	cairo_identity_matrix(cr);
	cairo_set_source_surface(cr, overlay, 0, 0);
	cairo_paint(cr);
	cairo_restore(cr);
	cairo_surface_destroy(overlay);
}
