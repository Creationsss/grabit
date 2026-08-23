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
	"rounded_rect",
	"ellipse",
	"arrow",
	"blur",
	"pixelate",
	"spotlight",
	"text",
	"counter",
	"callout",
	"eraser",
	NULL,
};

const char *const grabit_line_style_names[] = {
	"solid",
	"dashed",
	"dotted",
	NULL,
};

int grabit_stroke_dash(enum stroke_style style, double w, double out_dashes[2]) {
	if (w < 1.0) w = 1.0;
	switch (style) {
	case STROKE_DASHED:
		out_dashes[0] = w * 3.0;
		out_dashes[1] = w * 2.5;
		return 2;
	case STROKE_DOTTED:
		out_dashes[0] = w * 0.6;
		out_dashes[1] = w * 1.8;
		return 2;
	default:
		return 0;
	}
}

static int annotation_list_grow(struct annotation_list *list) {
	if (list->n < list->cap) return 0;
	size_t cap = list->cap ? list->cap * 2 : 8;
	struct annotation *p = realloc(list->items, cap * sizeof *p);
	if (!p) return -1;
	list->items = p;
	list->cap = cap;
	return 0;
}

int annotation_list_push(struct annotation_list *list, const struct annotation *a) {
	if (annotation_list_grow(list) != 0) return -1;
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

bool annotation_list_pop_take(struct annotation_list *list, struct annotation *out) {
	if (list->n == 0) return false;
	*out = list->items[--list->n];
	list->gen++;
	return true;
}

int annotation_list_insert(struct annotation_list *list, size_t idx,
						   const struct annotation *a) {
	if (idx > list->n) idx = list->n;
	if (annotation_list_grow(list) != 0) return -1;
	memmove(&list->items[idx + 1], &list->items[idx],
			(list->n - idx) * sizeof *list->items);
	list->items[idx] = *a;
	list->n++;
	list->gen++;
	return 0;
}

bool annotation_list_remove_at(struct annotation_list *list, size_t idx,
							   struct annotation *out) {
	if (idx >= list->n) return false;
	*out = list->items[idx];
	memmove(&list->items[idx], &list->items[idx + 1],
			(list->n - idx - 1) * sizeof *list->items);
	list->n--;
	list->gen++;
	return true;
}

int32_t annotation_counter_radius(const struct annotation *a) {
	return annotation_font_size(a);
}

int32_t annotation_width(const struct annotation *a) {
	return a->width > 0 ? a->width : ANNO_DEFAULT_WIDTH;
}

int32_t annotation_font_size(const struct annotation *a) {
	return a->font_size > 0 ? a->font_size : ANNO_DEFAULT_FONT;
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

struct rect annotation_text_box(const struct annotation *a) {
	int32_t fs = annotation_font_size(a);
	int32_t len = a->text ? (int32_t)strlen(a->text) : 0;
	int32_t pad = a->tool == TOOL_CALLOUT ? fs / 2 : 0;
	return (struct rect){a->x0 - pad, a->y0 - fs - pad,
						 len * fs * 3 / 5 + pad * 2, fs + fs / 4 + pad * 2};
}

double annotation_line_width(const struct annotation *a) {
	return annotation_width(a) * (a->tool == TOOL_MARKER ? 2.5 : 1.0);
}

static int32_t annotation_paint_extent(const struct annotation *a) {
	if (tool_samples_backdrop(a->tool) || tool_is_layer(a->tool)) return 0;
	if (a->tool == TOOL_ARROW)
		return (int32_t)ceil(
			grabit_cairo_arrow_extent(annotation_width(a), ANNO_ARROW_MIN_HEAD));
	return (int32_t)(annotation_line_width(a) / 2.0);
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
	} else if (tool_types_text(a->tool)) {
		struct rect t = annotation_text_box(a);
		minx = t.x, maxx = t.x + t.w, miny = t.y, maxy = t.y + t.h;
		if (a->tool == TOOL_CALLOUT) {
			minx = i32min(minx, a->x1), maxx = i32max(maxx, a->x1);
			miny = i32min(miny, a->y1), maxy = i32max(maxy, a->y1);
		}
	} else if (a->tool == TOOL_COUNTER) {
		int32_t r = annotation_counter_radius(a);
		minx = a->x0 - r;
		maxx = a->x0 + r;
		miny = a->y0 - r;
		maxy = a->y0 + r;
	} else {
		minx = i32min(a->x0, a->x1);
		maxx = i32max(a->x0, a->x1);
		miny = i32min(a->y0, a->y1);
		maxy = i32max(a->y0, a->y1);
	}
	int32_t pad = annotation_paint_extent(a) + 2;
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
	if (a->tool == TOOL_LINE || a->tool == TOOL_ARROW ||
		a->tool == TOOL_CALLOUT)
		return 0x9;
	if (tool_is_rect_region(a->tool))
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
	case TOOL_CALLOUT: {
		struct rect b = annotation_text_box(a);
		if (rect_contains(b, x, y)) return true;
		return seg_dist2(x, y, b.x + b.w / 2.0, b.y + b.h / 2.0, a->x1, a->y1) <= tol2;
	}
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
