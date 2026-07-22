// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "region/annotate.h"

#include "cairo_util.h"
#include "region/region.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <cairo/cairo.h>

const char *const grabit_tool_names[] = {
	"pen",
	"marker",
	"line",
	"rect",
	"ellipse",
	"arrow",
	"blur",
	"text",
	"eraser",
	NULL,
};

static void set_color(cairo_t *cr, uint32_t color) {
	grabit_cairo_set_source_argb(cr, color, 1.0);
}

static void paint_arrow(cairo_t *cr, double x0, double y0, double x1, double y1,
						double width) {
	double dx = x1 - x0, dy = y1 - y0;
	double len = sqrt(dx * dx + dy * dy);
	if (len < 1.0) {
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

static void paint_blur(cairo_t *cr, double x, double y, double w, double h,
					   double scale, cairo_surface_t *backdrop) {
	double cell = 12.0 * scale;
	if (cell < 6.0) cell = 6.0;
	cairo_save(cr);
	cairo_rectangle(cr, x, y, w, h);
	cairo_clip(cr);

	cairo_surface_t *target = backdrop ? backdrop : cairo_get_target(cr);
	cairo_surface_flush(target);
	int32_t tw = cairo_image_surface_get_width(target);
	int32_t th = cairo_image_surface_get_height(target);
	cairo_format_t fmt = cairo_image_surface_get_format(target);
	if (fmt != CAIRO_FORMAT_ARGB32 && fmt != CAIRO_FORMAT_RGB24) {
		cairo_restore(cr);
		return;
	}

	cairo_surface_t *layer = cairo_get_group_target(cr);
	if (layer == target ||
		cairo_surface_get_type(layer) != CAIRO_SURFACE_TYPE_IMAGE)
		layer = NULL;

	double bx0 = x, by0 = y, bx1 = x + w, by1 = y + h;
	cairo_user_to_device(cr, &bx0, &by0);
	cairo_user_to_device(cr, &bx1, &by1);
	int32_t sx0 = (int32_t)floor(bx0 < bx1 ? bx0 : bx1);
	int32_t sy0 = (int32_t)floor(by0 < by1 ? by0 : by1);
	int32_t sx1 = (int32_t)ceil(bx0 > bx1 ? bx0 : bx1);
	int32_t sy1 = (int32_t)ceil(by0 > by1 ? by0 : by1);
	if (sx0 < 0) sx0 = 0;
	if (sy0 < 0) sy0 = 0;
	if (sx1 > tw) sx1 = tw;
	if (sy1 > th) sy1 = th;
	if (sx1 <= sx0 || sy1 <= sy0) {
		cairo_restore(cr);
		return;
	}

	cairo_surface_t *snap = cairo_image_surface_create(fmt, sx1 - sx0, sy1 - sy0);
	cairo_t *cr2 = cairo_create(snap);
	cairo_set_operator(cr2, CAIRO_OPERATOR_SOURCE);
	cairo_set_source_surface(cr2, target, -sx0, -sy0);
	cairo_paint(cr2);
	if (layer) {
		cairo_surface_flush(layer);
		cairo_set_operator(cr2, CAIRO_OPERATOR_OVER);
		cairo_set_source_surface(cr2, layer, -sx0, -sy0);
		cairo_paint(cr2);
	}
	cairo_destroy(cr2);
	cairo_surface_flush(snap);

	const uint8_t *snap_data = cairo_image_surface_get_data(snap);
	int snap_stride = cairo_image_surface_get_stride(snap);
	if (!snap_data) {
		cairo_surface_destroy(snap);
		cairo_restore(cr);
		return;
	}

	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
	for (double cy = y; cy < y + h; cy += cell) {
		for (double cx = x; cx < x + w; cx += cell) {
			double cw = (cx + cell > x + w) ? (x + w - cx) : cell;
			double ch = (cy + cell > y + h) ? (y + h - cy) : cell;

			double dx0 = cx, dy0 = cy;
			double dx1 = cx + cw, dy1 = cy + ch;
			cairo_user_to_device(cr, &dx0, &dy0);
			cairo_user_to_device(cr, &dx1, &dy1);
			int32_t px0 = (int32_t)dx0;
			int32_t py0 = (int32_t)dy0;
			int32_t px1 = (int32_t)dx1;
			int32_t py1 = (int32_t)dy1;
			if (px0 < sx0) px0 = sx0;
			if (py0 < sy0) py0 = sy0;
			if (px1 > sx1) px1 = sx1;
			if (py1 > sy1) py1 = sy1;
			if (px1 <= px0 || py1 <= py0) continue;

			uint64_t sr = 0, sg = 0, sb = 0;
			uint64_t count = 0;
			for (int32_t py = py0; py < py1; py++) {
				const uint32_t *row = (const uint32_t *)(snap_data +
														 (size_t)(py - sy0) * snap_stride);
				for (int32_t px = px0; px < px1; px++) {
					uint32_t p = row[px - sx0];
					sr += (p >> 16) & 0xff;
					sg += (p >> 8) & 0xff;
					sb += p & 0xff;
					count++;
				}
			}
			if (count == 0) continue;
			double rb = (double)sr / count / 255.0;
			double gb = (double)sg / count / 255.0;
			double bb = (double)sb / count / 255.0;
			cairo_set_source_rgba(cr, rb, gb, bb, 1.0);
			cairo_rectangle(cr, cx, cy, cw, ch);
			cairo_fill(cr);
		}
	}

	cairo_surface_destroy(snap);
	cairo_restore(cr);
}

void annotation_paint_backdrop(cairo_t *cr, const struct annotation *a, double scale,
							   cairo_surface_t *backdrop) {
	cairo_save(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
	double w = (a->width > 0 ? a->width : 3) * scale;

	switch (a->tool) {
	case TOOL_RECT: {
		double x = a->x0 < a->x1 ? a->x0 : a->x1;
		double y = a->y0 < a->y1 ? a->y0 : a->y1;
		double rw = a->x0 < a->x1 ? a->x1 - a->x0 : a->x0 - a->x1;
		double rh = a->y0 < a->y1 ? a->y1 - a->y0 : a->y0 - a->y1;
		set_color(cr, a->color);
		cairo_set_line_width(cr, w);
		cairo_rectangle(cr, x, y, rw, rh);
		cairo_stroke(cr);
		break;
	}
	case TOOL_ELLIPSE: {
		double cx = (a->x0 + a->x1) / 2.0;
		double cy = (a->y0 + a->y1) / 2.0;
		double rx = (a->x0 < a->x1 ? a->x1 - a->x0 : a->x0 - a->x1) / 2.0;
		double ry = (a->y0 < a->y1 ? a->y1 - a->y0 : a->y0 - a->y1) / 2.0;
		if (rx < 1.0 || ry < 1.0) break;
		set_color(cr, a->color);
		cairo_set_line_width(cr, w);
		cairo_save(cr);
		cairo_translate(cr, cx, cy);
		cairo_scale(cr, rx, ry);
		cairo_arc(cr, 0, 0, 1, 0, 2.0 * M_PI);
		cairo_restore(cr);
		cairo_stroke(cr);
		break;
	}
	case TOOL_ARROW:
		set_color(cr, a->color);
		paint_arrow(cr, a->x0, a->y0, a->x1, a->y1, w);
		break;
	case TOOL_LINE:
		set_color(cr, a->color);
		cairo_set_line_width(cr, w);
		cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
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
			set_color(cr, a->color);
		cairo_set_line_width(cr, lw);
		cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
		cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
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
	case TOOL_BLUR: {
		double x = a->x0 < a->x1 ? a->x0 : a->x1;
		double y = a->y0 < a->y1 ? a->y0 : a->y1;
		double rw = a->x0 < a->x1 ? a->x1 - a->x0 : a->x0 - a->x1;
		double rh = a->y0 < a->y1 ? a->y1 - a->y0 : a->y0 - a->y1;
		if (rw < 2.0 || rh < 2.0) break;
		paint_blur(cr, x, y, rw, rh, scale, backdrop);
		break;
	}
	case TOOL_TEXT: {
		if (!a->text || !a->text[0]) break;
		double font_px = (a->font_size > 0 ? a->font_size : ANNO_DEFAULT_FONT) * scale;
		cairo_select_font_face(cr, "sans-serif",
							   CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
		cairo_set_font_size(cr, font_px);
		set_color(cr, a->color);
		cairo_move_to(cr, a->x0, a->y0);
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

int annotation_list_push(struct annotation_list *list, const struct annotation *a) {
	if (list->n == list->cap) {
		size_t cap = list->cap ? list->cap * 2 : 8;
		struct annotation *p = realloc(list->items, cap * sizeof *p);
		if (!p) return -1;
		list->items = p;
		list->cap = cap;
	}
	list->items[list->n] = *a;
	annotation_update_bbox(&list->items[list->n]);
	list->n++;
	list->gen++;
	return 0;
}

void annotation_list_pop(struct annotation_list *list) {
	if (list->n == 0) return;
	struct annotation *a = &list->items[--list->n];
	annotation_free(a);
	list->gen++;
}

void annotation_free(struct annotation *a) {
	if (!a) return;
	free(a->points);
	free(a->text);
	memset(a, 0, sizeof *a);
}

void annotation_list_free(struct annotation_list *list) {
	if (!list) return;
	for (size_t i = 0; i < list->n; i++)
		annotation_free(&list->items[i]);
	free(list->items);
	memset(list, 0, sizeof *list);
}

void annotation_update_bbox(struct annotation *a) {
	int32_t minx, miny, maxx, maxy;
	if (tool_uses_points(a->tool) && a->n_points > 0) {
		minx = maxx = a->points[0];
		miny = maxy = a->points[1];
		for (size_t i = 1; i < a->n_points; i++) {
			minx = i32min(minx, a->points[i * 2]);
			maxx = i32max(maxx, a->points[i * 2]);
			miny = i32min(miny, a->points[i * 2 + 1]);
			maxy = i32max(maxy, a->points[i * 2 + 1]);
		}
	} else if (a->tool == TOOL_TEXT) {
		int32_t fs = a->font_size > 0 ? a->font_size : ANNO_DEFAULT_FONT;
		int32_t len = a->text ? (int32_t)strlen(a->text) : 0;
		minx = a->x0;
		maxx = a->x0 + len * fs * 3 / 5;
		miny = a->y0 - fs;
		maxy = a->y0 + fs / 4;
	} else {
		minx = i32min(a->x0, a->x1);
		maxx = i32max(a->x0, a->x1);
		miny = i32min(a->y0, a->y1);
		maxy = i32max(a->y0, a->y1);
	}
	int32_t pad = a->width / 2 + 2;
	a->bbox.x = minx - pad;
	a->bbox.y = miny - pad;
	a->bbox.w = maxx - minx + pad * 2;
	a->bbox.h = maxy - miny + pad * 2;
}

static double seg_dist2(double px, double py, double x0, double y0,
						double x1, double y1) {
	double dx = x1 - x0, dy = y1 - y0;
	double len2 = dx * dx + dy * dy;
	double t = len2 > 0.0 ? ((px - x0) * dx + (py - y0) * dy) / len2 : 0.0;
	if (t < 0.0) t = 0.0;
	if (t > 1.0) t = 1.0;
	double cx = x0 + t * dx, cy = y0 + t * dy;
	return (px - cx) * (px - cx) + (py - cy) * (py - cy);
}

int annotation_corner_mask(const struct annotation *a) {
	if (a->tool == TOOL_LINE || a->tool == TOOL_ARROW) return 0x9;
	if (a->tool == TOOL_RECT || a->tool == TOOL_ELLIPSE || a->tool == TOOL_BLUR)
		return 0xF;
	return 0;
}

bool annotation_hit(const struct annotation *a, int32_t x, int32_t y) {
	int32_t tol = a->width + 6;
	double tol2 = (double)tol * (double)tol;
	struct rect ebb = {a->bbox.x - tol, a->bbox.y - tol,
					   a->bbox.w + tol * 2, a->bbox.h + tol * 2};
	if (!rect_contains(ebb, x, y)) return false;
	switch (a->tool) {
	case TOOL_LINE:
	case TOOL_ARROW:
		return seg_dist2(x, y, a->x0, a->y0, a->x1, a->y1) <= tol2;
	case TOOL_PEN:
	case TOOL_MARKER:
	case TOOL_ERASER:
		if (a->n_points == 0) return false;
		if (a->n_points == 1)
			return seg_dist2(x, y, a->points[0], a->points[1],
							 a->points[0], a->points[1]) <= tol2;
		for (size_t i = 0; i + 1 < a->n_points; i++) {
			if (seg_dist2(x, y, a->points[i * 2], a->points[i * 2 + 1],
						  a->points[i * 2 + 2], a->points[i * 2 + 3]) <= tol2)
				return true;
		}
		return false;
	default:
		return true;
	}
}

void annotation_translate(struct annotation *a, int32_t dx, int32_t dy) {
	a->x0 += dx;
	a->y0 += dy;
	a->x1 += dx;
	a->y1 += dy;
	for (size_t i = 0; i < a->n_points; i++) {
		a->points[i * 2] += dx;
		a->points[i * 2 + 1] += dy;
	}
	a->bbox.x += dx;
	a->bbox.y += dy;
}
