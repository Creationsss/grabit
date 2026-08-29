// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#include "capture/edit_file.h"

#include "cairo_util.h"
#include "capture/capture.h"
#include "capture/freeze.h"
#include "capture/save.h"
#include "log.h"
#include "region/annotate.h"
#include "region/region.h"
#include "util/util.h"
#include "wl/wl.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <cairo/cairo.h>

static void free_frozen(struct image *frozen, size_t n) {
	if (!frozen) return;
	for (size_t i = 0; i < n; i++)
		image_free(&frozen[i]);
	free(frozen);
}

static int fill_output_canvas(struct image *dst, const struct grabit_output *o,
							  cairo_surface_t *img, int32_t img_x, int32_t img_y) {
	int32_t w, h;
	grabit_output_region_pixels(o, o->logical_width, o->logical_height, &w, &h);
	if (w <= 0 || h <= 0 || w > GRABIT_MAX_PIXEL_SIDE || h > GRABIT_MAX_PIXEL_SIDE)
		return -1;

	int32_t stride = cairo_format_stride_for_width(CAIRO_FORMAT_RGB24, w);
	if (stride <= 0) return -1;
	size_t size = (size_t)stride * (size_t)h;
	void *bytes = calloc(1, size);
	if (!bytes) {
		log_error("edit: out of memory");
		return -1;
	}

	cairo_surface_t *canvas = grabit_cairo_image(bytes, CAIRO_FORMAT_RGB24, w, h, stride);
	if (!canvas) {
		free(bytes);
		return -1;
	}

	double ratio = grabit_output_pixel_ratio(o);
	cairo_t *cr = cairo_create(canvas);
	cairo_set_source_rgb(cr, 0.09, 0.09, 0.11);
	cairo_paint(cr);
	cairo_set_source_surface(cr, img, (img_x - o->x) * ratio, (img_y - o->y) * ratio);
	cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
	cairo_paint(cr);
	cairo_destroy(cr);
	cairo_surface_flush(canvas);
	cairo_surface_destroy(canvas);

	dst->width = w;
	dst->height = h;
	dst->stride = stride;
	dst->format = WL_SHM_FORMAT_XRGB8888;
	dst->bytes = bytes;
	dst->size = size;
	return 0;
}

int gapp_edit_image_file(struct config *cfg, const char *src_path,
						 const struct grabit_save_opts *opts, const char *out_path,
						 uint32_t *inout_color, int32_t *inout_width,
						 int32_t *inout_tool, bool *out_choices_dirty,
						 struct rect *out_rect) {
	if (!cfg || !src_path || !opts || !out_path) return -1;

	cairo_surface_t *img = grabit_load_image_surface(src_path, "edit");
	if (!img) return -1;

	int rc = -1;
	bool wl_up = false;
	size_t n_frozen = 0;
	struct image *frozen = NULL;
	struct annotation_list annos = {0};
	cairo_surface_t *out = NULL;
	struct grabit_wl_state s;

	int32_t img_w = cairo_image_surface_get_width(img);
	int32_t img_h = cairo_image_surface_get_height(img);
	if (img_w <= 0 || img_h <= 0) goto cleanup;

	if (grabit_wl_init(&s) != 0) goto cleanup;
	wl_up = true;
	if (s.n_outputs == 0) {
		log_error("edit: no outputs");
		goto cleanup;
	}

	const struct grabit_output *host = s.outputs[0];
	for (size_t i = 1; i < s.n_outputs; i++) {
		const struct grabit_output *o = s.outputs[i];
		if ((int64_t)o->logical_width * o->logical_height >
			(int64_t)host->logical_width * host->logical_height)
			host = o;
	}
	double host_ratio = grabit_output_pixel_ratio(host);

	int32_t logical_w = (int32_t)lround(img_w / host_ratio);
	int32_t logical_h = (int32_t)lround(img_h / host_ratio);
	int32_t img_x = host->x + (host->logical_width - logical_w) / 2;
	int32_t img_y = host->y + (host->logical_height - logical_h) / 2;

	if (logical_w > host->logical_width || logical_h > host->logical_height)
		log_warn("edit: %dx%d image is larger than %s (%dx%d); "
				 "the parts past the edge cannot be reached",
				 img_w, img_h, host->name ? host->name : "the output",
				 host->logical_width, host->logical_height);

	frozen = calloc(s.n_outputs, sizeof *frozen);
	if (!frozen) {
		log_error("edit: out of memory");
		goto cleanup;
	}
	n_frozen = s.n_outputs;
	for (size_t i = 0; i < s.n_outputs; i++) {
		if (s.outputs[i] != host) continue;
		if (fill_output_canvas(&frozen[i], host, img, img_x, img_y) != 0) goto cleanup;
	}

	struct rect preset = {img_x, img_y, logical_w, logical_h};
	struct rect got = {0};
	int sel = region_select(&s, cfg, frozen, true, &got, &annos, inout_color,
							inout_width, inout_tool, out_choices_dirty, NULL,
							&preset, NULL, 0);

	free_frozen(frozen, n_frozen);
	frozen = NULL;
	grabit_wl_finish(&s);
	wl_up = false;

	if (sel != 0) {
		rc = sel == REGION_SELECT_CANCELLED ? GRABIT_CAPTURE_CANCELLED : -1;
		goto cleanup;
	}

	out = grabit_cairo_promote_argb32(img);
	if (!out) {
		log_error("edit: surface %dx%d failed", img_w, img_h);
		goto cleanup;
	}

	if (annos.n > 0) {
		cairo_t *cr = cairo_create(out);
		annotation_list_paint(cr, &annos, img_x, img_y, host_ratio);
		cairo_destroy(cr);
	}

	rc = grabit_save_surface(out, opts, out_path);
	if (rc == 0 && out_rect) *out_rect = preset;

cleanup:
	cairo_surface_destroy(out);
	free_frozen(frozen, n_frozen);
	if (wl_up) grabit_wl_finish(&s);
	annotation_list_free(&annos);
	cairo_surface_destroy(img);
	return rc;
}
