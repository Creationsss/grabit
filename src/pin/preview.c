// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "pin/preview.h"

#include "capture/save.h"
#include "log.h"

#include <stdbool.h>
#include <stdlib.h>

#include <cairo/cairo.h>

int pin_preview_render_surface(cairo_surface_t *src, int target_w,
							   const char *out_path) {
	if (!src || !out_path || target_w <= 0) return -1;
	if (cairo_surface_status(src) != CAIRO_STATUS_SUCCESS) return -1;

	int src_w = cairo_image_surface_get_width(src);
	int src_h = cairo_image_surface_get_height(src);
	if (src_w <= 0 || src_h <= 0) return -1;

	double scale = (double)target_w / (double)src_w;
	if (scale > 1.0) scale = 1.0;
	int img_w = (int)(src_w * scale + 0.5);
	int img_h = (int)(src_h * scale + 0.5);
	if (img_w < 1) img_w = 1;
	if (img_h < 1) img_h = 1;

	const int border = 2;
	int dst_w = img_w + 2 * border;
	int dst_h = img_h + 2 * border;

	cairo_surface_t *dst = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, dst_w, dst_h);
	if (cairo_surface_status(dst) != CAIRO_STATUS_SUCCESS) {
		cairo_surface_destroy(dst);
		return -1;
	}
	cairo_t *cr = cairo_create(dst);
	cairo_set_source_rgba(cr, 0.10, 0.11, 0.13, 0.95);
	cairo_paint(cr);

	cairo_save(cr);
	cairo_translate(cr, border, border);
	cairo_scale(cr, scale, scale);
	cairo_set_source_surface(cr, src, 0, 0);
	cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
	cairo_paint(cr);
	cairo_restore(cr);
	cairo_destroy(cr);

	int rc = grabit_save_png_surface(dst, out_path, 1);
	cairo_surface_destroy(dst);
	return rc;
}

int pin_preview_render_png(const char *src_image_path, int target_w,
						   const char *out_path) {
	if (!src_image_path) return -1;
	cairo_surface_t *src = cairo_image_surface_create_from_png(src_image_path);
	if (cairo_surface_status(src) != CAIRO_STATUS_SUCCESS) {
		log_error("preview: load %s: %s", src_image_path,
				  cairo_status_to_string(cairo_surface_status(src)));
		cairo_surface_destroy(src);
		return -1;
	}
	int rc = pin_preview_render_surface(src, target_w, out_path);
	cairo_surface_destroy(src);
	return rc;
}
