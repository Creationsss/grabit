// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#include "capture/freeze.h"

#include "capture/capture.h"
#include "capture/pixels.h"
#include "capture/region_plan.h"
#include "capture/save.h"
#include "log.h"
#include "region/region.h"
#include "wl/wl.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>

struct edit_out {
	const struct freeze_dest *dest;
	const struct region_snap *snap;
	char **path;
};

static int edit_in_window(struct grabit_wl_state *s, struct config *cfg,
						  const struct png_slice *slices, size_t n_slices,
						  int32_t dst_w, int32_t dst_h, struct rect *r,
						  struct annotation_list *annos, uint32_t *inout_color,
						  int32_t *inout_width, int32_t *inout_tool,
						  bool *out_choices_dirty, struct grabit_save_opts *opts,
						  const struct edit_out *eo) {
	cairo_surface_t *base = grabit_composite_surface(dst_w, dst_h, slices, n_slices);
	if (!base) return -1;
	cairo_surface_flush(base);
	struct image img = {
		.width = cairo_image_surface_get_width(base),
		.height = cairo_image_surface_get_height(base),
		.stride = cairo_image_surface_get_stride(base),
		.format = WL_SHM_FORMAT_ARGB8888,
		.bytes = cairo_image_surface_get_data(base),
	};
	struct region_window_req req = {&img, *r, "grabit editor"};
	struct rect sel = *r;
	int rc = region_edit_window(s, cfg, &req, &sel, annos, inout_color, inout_width,
								inout_tool, out_choices_dirty);
	if (rc == 0) {
		if (!rect_equal(sel, *r)) opts->corner_radius = 0;
		*eo->path = eo->dest->build(eo->dest->ud, eo->snap);
		if (!*eo->path) {
			log_error("freeze: could not build the output path");
			rc = -1;
		} else {
			rc = grabit_save_cropped_annotated(base, r, &sel, annos, opts, *eo->path);
		}
	} else if (rc == REGION_SELECT_CANCELLED) {
		log_debug("editor window closed without saving");
		rc = GRABIT_CAPTURE_CANCELLED;
	}
	cairo_surface_destroy(base);
	return rc;
}

int grabit_freeze_capture(struct grabit_wl_state *s, struct config *cfg,
						  const struct freeze_dest *dest, char **out_path,
						  const struct grabit_save_opts *save_opts,
						  struct rect *out_rect, bool annotate, bool edit_window,
						  bool cursor,
						  uint32_t *inout_color, int32_t *inout_width,
						  int32_t *inout_tool,
						  bool *out_choices_dirty,
						  const struct rect *forced_region,
						  const struct rect *snap_rects, size_t n_snap_rects) {
	*out_path = NULL;
	struct image *frozen = calloc(s->n_outputs, sizeof *frozen);
	if (!frozen) return -1;

	int rc = -1;
	size_t captured = 0;
	struct png_slice *slices = NULL;
	struct annotation_list annos = {0};
	struct region_snap snap = {0};

	struct rect r;
	bool forced_only = forced_region && !annotate;
	if (forced_only) r = *forced_region;

	struct grabit_output **want = calloc(s->n_outputs, sizeof *want);
	size_t *want_idx = calloc(s->n_outputs, sizeof *want_idx);
	struct image *shot = calloc(s->n_outputs, sizeof *shot);
	size_t n_want = 0;
	if (!want || !want_idx || !shot) {
		log_error("freeze: out of memory");
		goto cleanup;
	}
	for (size_t i = 0; i < s->n_outputs; i++) {
		if (forced_only && !grabit_output_overlaps(s->outputs[i], r)) continue;
		want_idx[n_want] = i;
		want[n_want++] = s->outputs[i];
	}
	captured = s->n_outputs;

	if (n_want > 0 && capture_outputs_full(s, want, n_want, cursor, shot) != 0)
		goto cleanup;
	for (size_t k = 0; k < n_want; k++)
		frozen[want_idx[k]] = shot[k];

	for (size_t k = 0; k < n_want; k++) {
		size_t i = want_idx[k];
		if (image_apply_output_transform(&frozen[i], s->outputs[i]) != 0) {
			log_error("freeze: transform of %s failed; aborting capture",
					  s->outputs[i]->name ? s->outputs[i]->name : "?");
			goto cleanup;
		}
	}

	if (!forced_only || !grabit_save_hdr_possible(save_opts)) {
		for (size_t i = 0; i < captured; i++)
			pixels_to_8bit(&frozen[i]);
	}

	bool in_window = annotate && edit_window;
	if (in_window && forced_region) {
		r = *forced_region;
	} else if (!forced_only) {
		bool overlay_edit = annotate && !in_window;
		int sel = region_select(s, cfg, frozen, overlay_edit, &r,
								overlay_edit ? &annos : NULL,
								overlay_edit ? inout_color : NULL,
								overlay_edit ? inout_width : NULL,
								overlay_edit ? inout_tool : NULL,
								overlay_edit ? out_choices_dirty : NULL, &snap,
								overlay_edit ? forced_region : NULL, snap_rects,
								n_snap_rects);
		if (sel != 0) {
			if (sel == REGION_SELECT_CANCELLED) {
				log_debug("region selection cancelled");
				rc = GRABIT_CAPTURE_CANCELLED;
			}
			goto cleanup;
		}
	}
	if (r.w <= 0 || r.h <= 0) {
		log_debug("empty selection; treating as cancelled");
		rc = GRABIT_CAPTURE_CANCELLED;
		goto cleanup;
	}

	double max_ratio = 1.0;
	for (size_t i = 0; i < s->n_outputs; i++) {
		struct grabit_output *o = s->outputs[i];
		if (!grabit_output_overlaps(o, r)) continue;
		if (o->logical_width > 0) {
			double sxr = (double)frozen[i].width / (double)o->logical_width;
			if (sxr > max_ratio) max_ratio = sxr;
		}
		if (o->logical_height > 0) {
			double syr = (double)frozen[i].height / (double)o->logical_height;
			if (syr > max_ratio) max_ratio = syr;
		}
	}

	int32_t dst_w = (int32_t)lround(r.w * max_ratio);
	int32_t dst_h = (int32_t)lround(r.h * max_ratio);

	slices = calloc(s->n_outputs, sizeof *slices);
	if (!slices) {
		log_error("out of memory");
		goto cleanup;
	}
	size_t n_slices = 0;

	for (size_t i = 0; i < s->n_outputs; i++) {
		struct grabit_output *o = s->outputs[i];
		int32_t ix0, iy0, iw, ih;
		if (!grabit_output_rect_intersect(o, &r, &ix0, &iy0, &iw, &ih)) continue;

		double sxr = o->logical_width > 0
						 ? (double)frozen[i].width / (double)o->logical_width
						 : 1.0;
		double syr = o->logical_height > 0
						 ? (double)frozen[i].height / (double)o->logical_height
						 : 1.0;

		struct png_slice *sl = &slices[n_slices++];
		sl->src = &frozen[i];
		sl->src_x = (int32_t)((ix0 - o->x) * sxr + 0.5);
		sl->src_y = (int32_t)((iy0 - o->y) * syr + 0.5);
		sl->src_w = (int32_t)(iw * sxr + 0.5);
		sl->src_h = (int32_t)(ih * syr + 0.5);
		if (sl->src_x + sl->src_w > frozen[i].width)
			sl->src_w = frozen[i].width - sl->src_x;
		if (sl->src_y + sl->src_h > frozen[i].height)
			sl->src_h = frozen[i].height - sl->src_y;

		sl->dst_x = (int32_t)lround((ix0 - r.x) * max_ratio);
		sl->dst_y = (int32_t)lround((iy0 - r.y) * max_ratio);
		sl->dst_w = (int32_t)lround(iw * max_ratio);
		sl->dst_h = (int32_t)lround(ih * max_ratio);
	}

	if (n_slices == 0) {
		log_error("selection doesn't intersect any output");
		goto cleanup;
	}

	struct grabit_save_opts eff_opts = *save_opts;
	if (forced_region && !forced_only && !rect_equal(r, *forced_region))
		eff_opts.corner_radius = 0;
	if (snap.snapped && n_snap_rects == 0)
		eff_opts.corner_radius = region_window_outer_radius(cfg, &r);
	if (in_window) {
		struct edit_out eo = {dest, &snap, out_path};
		rc = edit_in_window(s, cfg, slices, n_slices, dst_w, dst_h, &r, &annos, inout_color,
							inout_width, inout_tool, out_choices_dirty, &eff_opts, &eo);
	} else {
		*out_path = dest->build(dest->ud, &snap);
		if (!*out_path) {
			log_error("freeze: could not build the output path");
			goto cleanup;
		}
		rc = grabit_save_composite_annotated(dst_w, dst_h, slices, n_slices, &r,
											 max_ratio, annos.n > 0 ? &annos : NULL,
											 &eff_opts, *out_path);
	}
	if (rc == 0 && out_rect) *out_rect = r;

cleanup:
	free(want);
	free(want_idx);
	free(shot);
	free(slices);
	for (size_t i = 0; i < captured; i++)
		image_free(&frozen[i]);
	free(frozen);
	annotation_list_free(&annos);
	region_snap_free(&snap);
	return rc;
}
