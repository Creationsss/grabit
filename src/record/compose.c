// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#include "record/compose.h"

#include "cairo_util.h"

#include "capture/capture.h"
#include "capture/pixels.h"
#include "log.h"
#include "region/region.h"
#include "wl/wl.h"
#include <math.h>

#include <stdlib.h>
#include <string.h>

#include <cairo/cairo.h>
#include <wayland-client.h>

int rec_layout_build(struct grabit_wl_state *s, struct rect r, struct rec_layout *out) {
	memset(out, 0, sizeof *out);
	if (!s || s->n_outputs == 0) return -1;

	double max_ratio = 1.0;
	size_t n_overlap = 0;
	for (size_t i = 0; i < s->n_outputs; i++) {
		struct grabit_output *o = s->outputs[i];
		int32_t ix, iy, iw, ih;
		if (!grabit_output_rect_intersect(o, &r, &ix, &iy, &iw, &ih)) continue;
		n_overlap++;
		double pr = grabit_output_pixel_ratio(o);
		if (pr > max_ratio) max_ratio = pr;
	}
	if (n_overlap == 0) return -1;

	int32_t dst_w = (int32_t)lround(r.w * max_ratio);
	int32_t dst_h = (int32_t)lround(r.h * max_ratio);
	if (dst_w & 1) dst_w--;
	if (dst_h & 1) dst_h--;
	if (dst_w <= 0 || dst_h <= 0) return -1;

	int32_t stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, dst_w);
	if (stride <= 0) return -1;

	out->slices = calloc(n_overlap, sizeof *out->slices);
	if (!out->slices) return -1;
	out->slice_caches = calloc(n_overlap, sizeof *out->slice_caches);
	if (!out->slice_caches) {
		free(out->slices);
		out->slices = NULL;
		return -1;
	}

	size_t k = 0;
	for (size_t i = 0; i < s->n_outputs; i++) {
		struct grabit_output *o = s->outputs[i];
		int32_t ix, iy, iw, ih;
		if (!grabit_output_rect_intersect(o, &r, &ix, &iy, &iw, &ih)) continue;

		int32_t dx = (int32_t)lround((ix - r.x) * max_ratio);
		int32_t dy = (int32_t)lround((iy - r.y) * max_ratio);
		if (dx + lround(iw * max_ratio) > dst_w) iw = (int32_t)((dst_w - dx) / max_ratio);
		if (dy + lround(ih * max_ratio) > dst_h) ih = (int32_t)((dst_h - dy) / max_ratio);
		if (iw <= 0 || ih <= 0) continue;

		struct rec_slice *sl = &out->slices[k++];
		sl->out = o;
		sl->src_x = ix - o->x;
		sl->src_y = iy - o->y;
		sl->src_w = iw;
		sl->src_h = ih;
		grabit_output_region_pixels(o, iw, ih, &sl->cap_w, &sl->cap_h);
		sl->dst_x = dx;
		sl->dst_y = dy;
		sl->dst_w = (int32_t)lround(iw * max_ratio);
		sl->dst_h = (int32_t)lround(ih * max_ratio);
	}
	if (k == 0) {
		free(out->slices);
		free(out->slice_caches);
		out->slices = NULL;
		out->slice_caches = NULL;
		return -1;
	}
	out->n = k;
	out->dst_w = dst_w;
	out->dst_h = dst_h;
	out->dst_stride = stride;
	return 0;
}

bool rec_layout_is_direct(const struct rec_layout *layout) {
	if (!layout || layout->n != 1) return false;
	const struct rec_slice *sl = &layout->slices[0];
	if (sl->out->transform != WL_OUTPUT_TRANSFORM_NORMAL) return false;
	return sl->dst_x == 0 && sl->dst_y == 0 && sl->dst_w == layout->dst_w && sl->dst_h == layout->dst_h;
}

int rec_layout_capture_direct_into(struct grabit_wl_state *s, const struct rec_layout *layout,
								   bool cursor, void *dst, int32_t dst_stride, int32_t dst_h) {
	if (!s || !layout || !dst) return -1;
	if (layout->n != 1) return -1;
	const struct rec_slice *sl = &layout->slices[0];
	if (sl->out->dead) return -1;
	if (sl->cap_w != layout->dst_w || sl->cap_h != layout->dst_h) return -1;
	return capture_output_region_into(s, sl->out,
									  sl->src_x, sl->src_y, sl->src_w, sl->src_h,
									  cursor, dst, dst_stride, dst_h, NULL,
									  &layout->slice_caches[0]);
}

static int ensure_slice_scratch(struct rec_layout *layout, int32_t w, int32_t h) {
	if (layout->slice_scratch_w >= w && layout->slice_scratch_h >= h) return 0;
	int32_t new_w = layout->slice_scratch_w > w ? layout->slice_scratch_w : w;
	int32_t new_h = layout->slice_scratch_h > h ? layout->slice_scratch_h : h;
	size_t new_size = (size_t)new_w * 4 * (size_t)new_h;
	if (new_size > layout->slice_scratch_size) {
		void *p = realloc(layout->slice_scratch, new_size);
		if (!p) return -1;
		layout->slice_scratch = p;
		layout->slice_scratch_size = new_size;
	}
	layout->slice_scratch_w = new_w;
	layout->slice_scratch_h = new_h;
	return 0;
}

static void clear_slice(cairo_t *cr, const struct rec_slice *sl) {
	cairo_save(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
	cairo_set_source_rgba(cr, 0, 0, 0, 1);
	cairo_rectangle(cr, sl->dst_x, sl->dst_y, sl->dst_w, sl->dst_h);
	cairo_fill(cr);
	cairo_restore(cr);
}

static bool draw_slice(struct grabit_wl_state *s, struct rec_layout *layout,
					   size_t i, bool cursor, cairo_t *cr) {
	const struct rec_slice *sl = &layout->slices[i];
	if (ensure_slice_scratch(layout, sl->cap_w, sl->cap_h) != 0) return false;
	int32_t scratch_stride = sl->cap_w * 4;
	uint32_t fmt_raw;
	if (capture_output_region_into(s, sl->out, sl->src_x, sl->src_y, sl->src_w, sl->src_h,
								   cursor, layout->slice_scratch, scratch_stride,
								   sl->cap_h, &fmt_raw, &layout->slice_caches[i]) != 0)
		return false;

	cairo_format_t fmt = grabit_cairo_format_for_shm(fmt_raw);
	cairo_surface_t *src = grabit_cairo_image(layout->slice_scratch, fmt,
											  sl->cap_w, sl->cap_h, scratch_stride);
	if (!src) return false;

	int32_t visible_w = grabit_wl_transform_swaps(sl->out->transform) ? sl->cap_h : sl->cap_w;
	int32_t visible_h = grabit_wl_transform_swaps(sl->out->transform) ? sl->cap_w : sl->cap_h;
	bool needs_scale = visible_w != sl->dst_w || visible_h != sl->dst_h;
	double sx = visible_w > 0 ? (double)sl->dst_w / (double)visible_w : 1.0;
	double sy = visible_h > 0 ? (double)sl->dst_h / (double)visible_h : 1.0;

	cairo_save(cr);
	cairo_rectangle(cr, sl->dst_x, sl->dst_y, sl->dst_w, sl->dst_h);
	cairo_clip(cr);
	cairo_translate(cr, sl->dst_x, sl->dst_y);
	if (needs_scale) cairo_scale(cr, sx, sy);
	grabit_wl_transform_apply_inverse(cr, sl->out->transform, sl->cap_w, sl->cap_h);
	cairo_set_source_surface(cr, src, 0, 0);
	cairo_pattern_set_filter(cairo_get_source(cr),
							 needs_scale ? CAIRO_FILTER_GOOD : CAIRO_FILTER_NEAREST);
	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
	cairo_paint(cr);
	cairo_restore(cr);
	cairo_surface_destroy(src);
	return true;
}

int rec_layout_capture_compose(struct grabit_wl_state *s, struct rec_layout *layout,
							   bool cursor, void *dst_buf) {
	if (!s || !layout || !dst_buf) return -1;

	cairo_surface_t *dst = grabit_cairo_image_argb(dst_buf, layout->dst_w,
												   layout->dst_h, layout->dst_stride);
	if (!dst) return -1;
	cairo_t *cr = cairo_create(dst);

	size_t alive = 0, captured = 0;
	for (size_t i = 0; i < layout->n; i++) {
		const struct rec_slice *sl = &layout->slices[i];
		if (sl->out->dead) continue;
		alive++;
		if (draw_slice(s, layout, i, cursor, cr))
			captured++;
		else
			clear_slice(cr, sl);
	}

	cairo_destroy(cr);
	cairo_surface_flush(dst);
	cairo_surface_destroy(dst);
	if (alive == 0) return -1;
	if (captured == 0) return -1;
	return 0;
}

void rec_round_corners_buf(void *buf, int32_t w, int32_t h, int32_t stride,
						   int radius) {
	if (!buf || radius <= 0 || w <= 0 || h <= 0) return;
	cairo_surface_t *s = grabit_cairo_image_argb(buf, w, h, stride);
	if (!s) return;
	cairo_t *cr = cairo_create(s);
	grabit_cairo_punch_corners(cr, w, h, radius);
	cairo_destroy(cr);
	cairo_surface_flush(s);
	cairo_surface_destroy(s);
}

void rec_layout_free(struct rec_layout *layout) {
	if (!layout) return;
	if (layout->slice_caches) {
		for (size_t i = 0; i < layout->n; i++)
			pixels_pool_destroy(&layout->slice_caches[i]);
		free(layout->slice_caches);
	}
	free(layout->slices);
	free(layout->slice_scratch);
	memset(layout, 0, sizeof *layout);
}
