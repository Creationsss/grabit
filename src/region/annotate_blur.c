// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "region/annotate.h"

#include "region/annotate_internal.h"
#include "region/region.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <cairo/cairo.h>

void ganno_paint_blur(cairo_t *cr, double x, double y, double w, double h,
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
