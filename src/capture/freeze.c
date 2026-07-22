// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#include "capture/freeze.h"

#include "capture/capture.h"
#include "capture/save.h"
#include "log.h"
#include "region/region.h"
#include "wl/wl.h"

#include <stdint.h>
#include <stdlib.h>

int grabit_freeze_capture(struct grabit_wl_state *s, struct config *cfg,
						  const char *path,
						  const struct grabit_save_opts *save_opts,
						  struct rect *out_rect, bool annotate, bool cursor,
						  uint32_t *inout_color, int32_t *inout_width,
						  int32_t *inout_tool,
						  bool *out_choices_dirty, const struct rect *forced_region,
						  const struct rect *snap_rects, size_t n_snap_rects) {
	struct image *frozen = calloc(s->n_outputs, sizeof *frozen);
	if (!frozen) return -1;

	int rc = -1;
	size_t captured = 0;
	struct png_slice *slices = NULL;
	struct annotation_list annos = {0};

	struct rect r;
	bool forced_only = forced_region && !annotate;
	if (forced_only) r = *forced_region;

	struct grabit_output **want = calloc(s->n_outputs, sizeof *want);
	size_t *want_idx = calloc(s->n_outputs, sizeof *want_idx);
	struct image *shot = calloc(s->n_outputs, sizeof *shot);
	size_t n_want = 0;
	if (!want || !want_idx || !shot) {
		log_error("freeze: out of memory");
		free(want);
		free(want_idx);
		free(shot);
		goto cleanup;
	}
	for (size_t i = 0; i < s->n_outputs; i++) {
		if (forced_only) {
			int32_t ix, iy, iw, ih;
			if (!grabit_output_rect_intersect(s->outputs[i], &r, &ix, &iy, &iw, &ih))
				continue;
		}
		want_idx[n_want] = i;
		want[n_want++] = s->outputs[i];
	}
	captured = s->n_outputs;

	if (n_want > 0 && capture_outputs_full(s, want, n_want, cursor, shot) != 0) {
		log_error("freeze: capture failed");
		free(want);
		free(want_idx);
		free(shot);
		goto cleanup;
	}
	for (size_t k = 0; k < n_want; k++)
		frozen[want_idx[k]] = shot[k];
	free(want);
	free(shot);

	for (size_t k = 0; k < n_want; k++) {
		size_t i = want_idx[k];
		if (image_apply_transform(&frozen[i], s->outputs[i]->transform) != 0) {
			log_error("freeze: transform of %s failed; aborting capture",
					  s->outputs[i]->name ? s->outputs[i]->name : "?");
			free(want_idx);
			goto cleanup;
		}
	}
	free(want_idx);

	if (!forced_only &&
		region_select(s, cfg, frozen, annotate, &r, annotate ? &annos : NULL,
					  inout_color, inout_width, inout_tool, out_choices_dirty,
					  forced_region, snap_rects, n_snap_rects) != 0) {
		log_info("region selection cancelled");
		rc = GRABIT_CAPTURE_CANCELLED;
		goto cleanup;
	}
	if (r.w <= 0 || r.h <= 0) {
		log_error("empty selection");
		rc = GRABIT_CAPTURE_CANCELLED;
		goto cleanup;
	}

	int32_t max_scale = 1;
	for (size_t i = 0; i < s->n_outputs; i++) {
		struct grabit_output *o = s->outputs[i];
		int32_t ix, iy, iw, ih;
		if (!grabit_output_rect_intersect(o, &r, &ix, &iy, &iw, &ih)) continue;
		if (o->scale > max_scale) max_scale = o->scale;
	}

	int32_t dst_w = r.w * max_scale;
	int32_t dst_h = r.h * max_scale;

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

		sl->dst_x = (ix0 - r.x) * max_scale;
		sl->dst_y = (iy0 - r.y) * max_scale;
		sl->dst_w = iw * max_scale;
		sl->dst_h = ih * max_scale;
	}

	if (n_slices == 0) {
		log_error("selection doesn't intersect any output");
		goto cleanup;
	}

	rc = grabit_save_composite_annotated(dst_w, dst_h, slices, n_slices,
										 &r, max_scale,
										 annos.n > 0 ? &annos : NULL, save_opts, path);

	if (rc == 0 && out_rect) *out_rect = r;

cleanup:
	free(slices);
	for (size_t i = 0; i < captured; i++)
		image_free(&frozen[i]);
	free(frozen);
	annotation_list_free(&annos);
	return rc;
}
