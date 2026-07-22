// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "region/wlr_state.h"

#include "cairo_util.h"
#include "capture/capture.h"
#include "log.h"
#include "region/annotate.h"
#include "region/wlr_input_state.h"
#include "util/util.h"
#include "wl/wl.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <cairo/cairo.h>
#include <wayland-client.h>

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#include "region/render_internal.h"

int gren_output_alloc_buffer(struct ro_output *o) {
	o->scale = o->go->scale > 0 ? o->go->scale : 1;
	o->pixel_width = o->width * o->scale;
	o->pixel_height = o->height * o->scale;

	struct grabit_shm_buf b;
	if (grabit_shm_argb_buf(o->st->wls->shm, "grabit-region",
							o->pixel_width, o->pixel_height, &b) != 0) {
		return -1;
	}
	o->buffer = b.buffer;
	o->buf_data = b.map;
	o->buf_size = b.size;
	o->stride = o->pixel_width * 4;

	o->cairo_dst = grabit_cairo_image_argb(o->buf_data, o->pixel_width,
										   o->pixel_height, o->stride);
	if (!o->cairo_dst) {
		log_error("region: cairo dst surface failed");
		return -1;
	}

	const struct image *frozen = NULL;
	if (o->st->frozen) {
		const struct image *cand = &o->st->frozen[o->idx];
		if (cand->bytes && cand->width > 0 && cand->height > 0) frozen = cand;
	}
	if (frozen) {
		cairo_format_t fmt = grabit_cairo_format_for_shm(frozen->format);
		o->cairo_frozen = grabit_cairo_image(frozen->bytes, fmt,
											 frozen->width, frozen->height,
											 frozen->stride);
		if (o->cairo_frozen) {
			o->cairo_frozen_pat = cairo_pattern_create_for_surface(o->cairo_frozen);
			double psx = frozen->width > 0
							 ? (double)o->pixel_width / (double)frozen->width
							 : 1.0;
			double psy = frozen->height > 0
							 ? (double)o->pixel_height / (double)frozen->height
							 : 1.0;
			cairo_matrix_t m;
			cairo_matrix_init_scale(&m, 1.0 / psx, 1.0 / psy);
			cairo_pattern_set_matrix(o->cairo_frozen_pat, &m);
			cairo_pattern_set_filter(o->cairo_frozen_pat, CAIRO_FILTER_GOOD);
		} else {
			cairo_surface_destroy(o->cairo_frozen);
			o->cairo_frozen = NULL;
		}
	}

	wl_surface_set_buffer_scale(o->surface, o->scale);
	return 0;
}

void region_render_free_buffer(struct ro_output *o) {
	grabit_wl_callback_drop(&o->frame_cb);
	if (o->anno_cache) {
		cairo_surface_destroy(o->anno_cache);
		o->anno_cache = NULL;
	}
	if (o->cairo_frozen_pat) {
		cairo_pattern_destroy(o->cairo_frozen_pat);
		o->cairo_frozen_pat = NULL;
	}
	if (o->cairo_frozen) {
		cairo_surface_destroy(o->cairo_frozen);
		o->cairo_frozen = NULL;
	}
	if (o->cairo_dst) {
		cairo_surface_destroy(o->cairo_dst);
		o->cairo_dst = NULL;
	}
	grabit_shm_release(&o->buffer, &o->buf_data, &o->buf_size);
}
