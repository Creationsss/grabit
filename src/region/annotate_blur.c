// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "region/annotate.h"

#include "region/annotate_internal.h"
#include "region/region.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>

#include <cairo/cairo.h>

static cairo_surface_t *ganno_snap_region(cairo_t *cr, cairo_surface_t *backdrop,
										  double x, double y, double w, double h,
										  int32_t *psx0, int32_t *psy0,
										  int32_t *psx1, int32_t *psy1) {
	cairo_surface_t *target = backdrop ? backdrop : cairo_get_target(cr);
	cairo_surface_flush(target);
	int32_t tw = cairo_image_surface_get_width(target);
	int32_t th = cairo_image_surface_get_height(target);
	cairo_format_t fmt = cairo_image_surface_get_format(target);
	if (fmt != CAIRO_FORMAT_ARGB32 && fmt != CAIRO_FORMAT_RGB24) return NULL;

	cairo_surface_t *layer = cairo_get_group_target(cr);
	if (layer == target || cairo_surface_get_type(layer) != CAIRO_SURFACE_TYPE_IMAGE)
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
	if (sx1 <= sx0 || sy1 <= sy0) return NULL;

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
	if (!cairo_image_surface_get_data(snap)) {
		cairo_surface_destroy(snap);
		return NULL;
	}
	*psx0 = sx0;
	*psy0 = sy0;
	*psx1 = sx1;
	*psy1 = sy1;
	return snap;
}

void ganno_paint_pixelate(cairo_t *cr, double x, double y, double w, double h,
						  double scale, cairo_surface_t *backdrop) {
	double cell = 12.0 * scale;
	if (cell < 6.0) cell = 6.0;
	cairo_save(cr);
	cairo_rectangle(cr, x, y, w, h);
	cairo_clip(cr);

	int32_t sx0, sy0, sx1, sy1;
	cairo_surface_t *snap = ganno_snap_region(cr, backdrop, x, y, w, h,
											  &sx0, &sy0, &sx1, &sy1);
	if (!snap) {
		cairo_restore(cr);
		return;
	}
	const uint8_t *snap_data = cairo_image_surface_get_data(snap);
	int snap_stride = cairo_image_surface_get_stride(snap);

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

static void blur_axis(const uint32_t *src, int src_rs, uint32_t *dst, int dst_rs,
					  int w, int h, int r, int horiz) {
	int len = horiz ? w : h;
	int lines = horiz ? h : w;
	if (len < 1) return;
	int src_line = horiz ? src_rs : 1;
	int dst_line = horiz ? dst_rs : 1;
	int src_step = horiz ? 1 : src_rs;
	int dst_step = horiz ? 1 : dst_rs;
	double norm = 1.0 / (2 * r + 1);
	for (int l = 0; l < lines; l++) {
		const uint32_t *sl = src + (size_t)l * src_line;
		uint32_t *dl = dst + (size_t)l * dst_line;
		long sr = 0, sg = 0, sb = 0;
		for (int k = -r; k <= r; k++) {
			int i = k < 0 ? 0 : (k >= len ? len - 1 : k);
			uint32_t p = sl[(size_t)i * src_step];
			sr += (p >> 16) & 0xff;
			sg += (p >> 8) & 0xff;
			sb += p & 0xff;
		}
		for (int i = 0; i < len; i++) {
			uint32_t r8 = (uint32_t)(sr * norm + 0.5);
			uint32_t g8 = (uint32_t)(sg * norm + 0.5);
			uint32_t b8 = (uint32_t)(sb * norm + 0.5);
			dl[(size_t)i * dst_step] = 0xff000000u | (r8 << 16) | (g8 << 8) | b8;
			int rem = i - r;
			if (rem < 0) rem = 0;
			int add = i + r + 1;
			if (add >= len) add = len - 1;
			uint32_t pr = sl[(size_t)rem * src_step];
			uint32_t pa = sl[(size_t)add * src_step];
			sr += (long)((pa >> 16) & 0xff) - (long)((pr >> 16) & 0xff);
			sg += (long)((pa >> 8) & 0xff) - (long)((pr >> 8) & 0xff);
			sb += (long)(pa & 0xff) - (long)(pr & 0xff);
		}
	}
}

static void ganno_box_blur(cairo_surface_t *s, int radius, int passes) {
	int w = cairo_image_surface_get_width(s);
	int h = cairo_image_surface_get_height(s);
	int stride = cairo_image_surface_get_stride(s);
	uint8_t *data = cairo_image_surface_get_data(s);
	if (!data || w < 1 || h < 1 || radius < 1) return;

	uint32_t *surf = (uint32_t *)data;
	int surf_rs = stride / 4;
	uint32_t *tmp = malloc((size_t)w * (size_t)h * sizeof *tmp);
	if (!tmp) return;
	for (int p = 0; p < passes; p++) {
		blur_axis(surf, surf_rs, tmp, w, w, h, radius, 1);
		blur_axis(tmp, w, surf, surf_rs, w, h, radius, 0);
	}
	free(tmp);
}

void ganno_paint_blur(cairo_t *cr, double x, double y, double w, double h,
					  double scale, cairo_surface_t *backdrop) {
	cairo_save(cr);
	cairo_rectangle(cr, x, y, w, h);
	cairo_clip(cr);

	int32_t sx0, sy0, sx1, sy1;
	cairo_surface_t *snap = ganno_snap_region(cr, backdrop, x, y, w, h,
											  &sx0, &sy0, &sx1, &sy1);
	if (!snap) {
		cairo_restore(cr);
		return;
	}

	int radius = (int)(7.0 * scale + 0.5);
	if (radius < 2) radius = 2;
	ganno_box_blur(snap, radius, 3);
	cairo_surface_mark_dirty(snap);

	cairo_identity_matrix(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
	cairo_set_source_surface(cr, snap, sx0, sy0);
	cairo_paint(cr);

	cairo_surface_destroy(snap);
	cairo_restore(cr);
}
