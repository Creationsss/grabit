// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#include "capture/save.h"

#include "cairo_util.h"
#include "capture/capture.h"
#include "log.h"
#include "pin/preview.h"
#include "region/annotate.h"
#include "region/region.h"
#include "util/util.h"

#include <string.h>

const char *grabit_format_extension(enum grabit_image_format f) {
	switch (f) {
	case GRABIT_FMT_JPEG:
		return ".jpg";
	case GRABIT_FMT_WEBP:
		return ".webp";
	case GRABIT_FMT_PNG:
	default:
		return ".png";
	}
}

int grabit_format_from_name(const char *name, enum grabit_image_format *out) {
	if (!name || !out) return -1;
	if (strcmp(name, "png") == 0) {
		*out = GRABIT_FMT_PNG;
		return 0;
	}
	if (strcmp(name, "jpeg") == 0 || strcmp(name, "jpg") == 0) {
		*out = GRABIT_FMT_JPEG;
		return 0;
	}
	if (strcmp(name, "webp") == 0) {
		*out = GRABIT_FMT_WEBP;
		return 0;
	}
	return -1;
}

static cairo_surface_t *build_composite_surface(int32_t dst_w, int32_t dst_h,
												const struct png_slice *slices, size_t n) {
	cairo_surface_t *dst = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, dst_w, dst_h);
	if (cairo_surface_status(dst) != CAIRO_STATUS_SUCCESS) {
		log_error("cairo composite dst: %s",
				  cairo_status_to_string(cairo_surface_status(dst)));
		cairo_surface_destroy(dst);
		return NULL;
	}

	cairo_t *cr = cairo_create(dst);
	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
	cairo_set_source_rgba(cr, 0, 0, 0, 1);
	cairo_paint(cr);

	for (size_t i = 0; i < n; i++) {
		const struct png_slice *s = &slices[i];
		if (!s->src || !s->src->bytes) continue;
		if (s->src_w <= 0 || s->src_h <= 0) continue;
		if (s->dst_w <= 0 || s->dst_h <= 0) continue;

		cairo_format_t fmt = grabit_cairo_format_for_shm(s->src->format);
		cairo_surface_t *src = grabit_cairo_image(s->src->bytes, fmt, s->src->width,
												  s->src->height, s->src->stride);
		if (!src) {
			log_warn("composite slice %zu: cairo image failed", i);
			continue;
		}

		cairo_save(cr);
		cairo_rectangle(cr, s->dst_x, s->dst_y, s->dst_w, s->dst_h);
		cairo_clip(cr);
		double sx = (double)s->dst_w / (double)s->src_w;
		double sy = (double)s->dst_h / (double)s->src_h;
		cairo_translate(cr, s->dst_x, s->dst_y);
		cairo_scale(cr, sx, sy);
		cairo_translate(cr, -s->src_x, -s->src_y);
		cairo_set_source_surface(cr, src, 0, 0);
		cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
		cairo_paint(cr);
		cairo_restore(cr);

		cairo_surface_destroy(src);
	}

	cairo_destroy(cr);
	return dst;
}

int grabit_surface_pixels(cairo_surface_t *surface, const char *tag,
						  int *w, int *h, int *stride, const unsigned char **data) {
	if (!surface || cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
		log_error("%s: bad input surface", tag);
		return -1;
	}
	cairo_surface_flush(surface);
	*w = cairo_image_surface_get_width(surface);
	*h = cairo_image_surface_get_height(surface);
	*stride = cairo_image_surface_get_stride(surface);
	*data = cairo_image_surface_get_data(surface);
	if (*w <= 0 || *h <= 0 || *stride <= 0 || !*data) {
		log_error("%s: empty surface", tag);
		return -1;
	}
	return 0;
}

static void punch_rounded_corners(cairo_surface_t *dst,
								  const struct grabit_save_opts *opts) {
	int radius = opts->corner_radius;
	if (radius <= 0) return;
	if (opts->format == GRABIT_FMT_JPEG) {
		static bool warned;
		if (!warned) {
			warned = true;
			log_warn("jpeg cannot store transparency; saving %s with square corners",
					 grabit_format_extension(opts->format));
		}
		return;
	}
	cairo_t *cr = cairo_create(dst);
	grabit_cairo_punch_corners(cr, cairo_image_surface_get_width(dst),
							   cairo_image_surface_get_height(dst), radius);
	cairo_destroy(cr);
	cairo_surface_flush(dst);
}

int grabit_save_surface(cairo_surface_t *dst,
						const struct grabit_save_opts *opts, const char *path) {
	if (!dst || !opts || !path) return -1;

	punch_rounded_corners(dst, opts);

	int rc;
	switch (opts->format) {
	case GRABIT_FMT_JPEG:
		rc = grabit_save_jpeg_surface(dst, path, opts->jpeg_quality);
		break;
	case GRABIT_FMT_WEBP:
		rc = grabit_save_webp_surface(dst, path, opts->webp_quality, opts->webp_lossless);
		break;
	case GRABIT_FMT_PNG:
	default:
		rc = grabit_save_png_surface(dst, path, opts->png_level);
		break;
	}

	if (rc == 0 && opts->preview_path && opts->preview_width > 0)
		(void)pin_preview_render_surface(dst, opts->preview_width, opts->preview_path);

	return rc;
}

int grabit_save_composite_annotated(int32_t dst_w, int32_t dst_h,
									const struct png_slice *slices, size_t n,
									const struct rect *region, double scale,
									const struct annotation_list *annos,
									const struct grabit_save_opts *opts,
									const char *path) {
	if (!path || !opts || dst_w <= 0 || dst_h <= 0 || n == 0 || !slices) return -1;
	if (dst_w > GRABIT_MAX_PIXEL_SIDE || dst_h > GRABIT_MAX_PIXEL_SIDE) {
		log_error("save: %dx%d exceeds %d-px side cap",
				  dst_w, dst_h, GRABIT_MAX_PIXEL_SIDE);
		return -1;
	}

	cairo_surface_t *dst = build_composite_surface(dst_w, dst_h, slices, n);
	if (!dst) return -1;

	punch_rounded_corners(dst, opts);

	if (annos && region && scale > 0) {
		cairo_t *cr = cairo_create(dst);
		annotation_list_paint(cr, annos, region->x, region->y, scale);
		cairo_destroy(cr);
	}

	struct grabit_save_opts flat = *opts;
	flat.corner_radius = 0;
	int rc = grabit_save_surface(dst, &flat, path);
	cairo_surface_destroy(dst);
	return rc;
}
