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

static void apply_stroke_style(cairo_t *cr, enum stroke_style style, double w) {
	double d[2];
	int n = grabit_stroke_dash(style, w, d);
	cairo_set_dash(cr, d, n, 0.0);
	if (n > 0) cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
}

static void paint_arrow(cairo_t *cr, double x0, double y0, double x1, double y1,
						double width) {
	double dx = x1 - x0, dy = y1 - y0;
	double len = sqrt(dx * dx + dy * dy);
	if (len < 1.0) {
		cairo_new_sub_path(cr);
		cairo_arc(cr, x0, y0, width, 0, 2.0 * M_PI);
		cairo_fill(cr);
		return;
	}

	double ux = dx / len, uy = dy / len;
	double px = -uy, py = ux;

	double body = width * 0.5;
	double head_w = width * 2.2;
	double head_len = width * 5.5;
	if (head_len < 14.0) head_len = 14.0;
	if (head_len > len * 0.5) head_len = len * 0.5;

	double bx = x1 - ux * head_len;
	double by = y1 - uy * head_len;

	cairo_move_to(cr, x0 + px * body, y0 + py * body);
	cairo_line_to(cr, bx + px * body, by + py * body);
	cairo_line_to(cr, bx + px * head_w, by + py * head_w);
	cairo_line_to(cr, x1, y1);
	cairo_line_to(cr, bx - px * head_w, by - py * head_w);
	cairo_line_to(cr, bx - px * body, by - py * body);
	cairo_line_to(cr, x0 - px * body, y0 - py * body);
	cairo_close_path(cr);
	cairo_fill(cr);
}

void annotation_paint_backdrop(cairo_t *cr, const struct annotation *a, double scale,
							   cairo_surface_t *backdrop) {
	cairo_save(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
	double w = annotation_width(a) * scale;

	switch (a->tool) {
	case TOOL_RECT: {
		double x = a->x0 < a->x1 ? a->x0 : a->x1;
		double y = a->y0 < a->y1 ? a->y0 : a->y1;
		double rw = a->x0 < a->x1 ? a->x1 - a->x0 : a->x0 - a->x1;
		double rh = a->y0 < a->y1 ? a->y1 - a->y0 : a->y0 - a->y1;
		ganno_set_color(cr, a->color);
		cairo_set_line_width(cr, w);
		apply_stroke_style(cr, a->style, w);
		cairo_rectangle(cr, x, y, rw, rh);
		cairo_stroke(cr);
		break;
	}
	case TOOL_RRECT: {
		double x = a->x0 < a->x1 ? a->x0 : a->x1;
		double y = a->y0 < a->y1 ? a->y0 : a->y1;
		double rw = a->x0 < a->x1 ? a->x1 - a->x0 : a->x0 - a->x1;
		double rh = a->y0 < a->y1 ? a->y1 - a->y0 : a->y0 - a->y1;
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
		paint_arrow(cr, a->x0, a->y0, a->x1, a->y1, w);
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
	case TOOL_ERASER: {
		if (a->n_points < 1) break;
		double lw = a->tool == TOOL_MARKER ? w * 2.5 : w;
		if (a->tool == TOOL_MARKER)
			grabit_cairo_set_source_argb(cr, a->color, 0.4);
		else if (a->tool == TOOL_ERASER)
			cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
		else
			ganno_set_color(cr, a->color);
		cairo_set_line_width(cr, lw);
		cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
		cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
		if (tool_uses_line_style(a->tool)) apply_stroke_style(cr, a->style, lw);
		cairo_move_to(cr, a->points[0], a->points[1]);
		for (size_t i = 1; i < a->n_points; i++) {
			cairo_line_to(cr, a->points[i * 2], a->points[i * 2 + 1]);
		}
		if (a->n_points == 1) {
			cairo_arc(cr, a->points[0], a->points[1], lw / 2.0, 0, 2.0 * M_PI);
			cairo_fill(cr);
		} else {
			cairo_stroke(cr);
		}
		break;
	}
	case TOOL_BLUR:
	case TOOL_PIXELATE: {
		double x = a->x0 < a->x1 ? a->x0 : a->x1;
		double y = a->y0 < a->y1 ? a->y0 : a->y1;
		double rw = a->x0 < a->x1 ? a->x1 - a->x0 : a->x0 - a->x1;
		double rh = a->y0 < a->y1 ? a->y1 - a->y0 : a->y0 - a->y1;
		if (rw < 2.0 || rh < 2.0) break;
		int32_t strength = annotation_width(a);
		if (a->tool == TOOL_BLUR)
			ganno_paint_blur(cr, x, y, rw, rh, scale, strength, backdrop);
		else
			ganno_paint_pixelate(cr, x, y, rw, rh, scale, strength, backdrop);
		break;
	}
	case TOOL_TEXT: {
		if (!a->text || !a->text[0]) break;
		double font_px = (a->font_size > 0 ? a->font_size : ANNO_DEFAULT_FONT) * scale;
		cairo_select_font_face(cr, "sans-serif",
							   CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
		cairo_set_font_size(cr, font_px);
		ganno_set_color(cr, a->color);
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

		double lum = (0.299 * ((a->color >> 16) & 0xff) +
					  0.587 * ((a->color >> 8) & 0xff) +
					  0.114 * (a->color & 0xff)) /
					 255.0;
		if (lum > 0.6)
			cairo_set_source_rgba(cr, 0, 0, 0, 1);
		else
			cairo_set_source_rgba(cr, 1, 1, 1, 1);
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
