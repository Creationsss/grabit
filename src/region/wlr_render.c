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

void gren_anno_cache_ensure(struct ro_output *o) {
	const struct annotation_list *annos = o->st->out_annos;
	int32_t S = o->scale;
	int32_t skip = region_anno_dragging(o->st) ? o->st->sel_anno : -1;

	bool samples_backdrop = false;
	for (size_t i = 0; i < annos->n; i++) {
		if (tool_samples_backdrop(annos->items[i].tool)) {
			samples_backdrop = true;
			break;
		}
	}
	struct rect sel = {0};
	if (samples_backdrop) {
		sel.x = o->st->sel_x;
		sel.y = o->st->sel_y;
		sel.w = o->st->sel_w;
		sel.h = o->st->sel_h;
	}

	if (o->anno_cache &&
		(cairo_image_surface_get_width(o->anno_cache) != o->pixel_width ||
		 cairo_image_surface_get_height(o->anno_cache) != o->pixel_height)) {
		cairo_surface_destroy(o->anno_cache);
		o->anno_cache = NULL;
	}
	if (o->anno_cache && o->anno_cache_gen == annos->gen &&
		o->anno_cache_skip == skip &&
		memcmp(&o->anno_cache_sel, &sel, sizeof sel) == 0)
		return;
	if (!o->anno_cache) {
		o->anno_cache = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
												   o->pixel_width, o->pixel_height);
		if (cairo_surface_status(o->anno_cache) != CAIRO_STATUS_SUCCESS) {
			cairo_surface_destroy(o->anno_cache);
			o->anno_cache = NULL;
			return;
		}
	}
	cairo_t *cc = cairo_create(o->anno_cache);
	cairo_set_operator(cc, CAIRO_OPERATOR_CLEAR);
	cairo_paint(cc);
	cairo_set_operator(cc, CAIRO_OPERATOR_OVER);
	cairo_translate(cc, -o->go->x * S, -o->go->y * S);
	cairo_scale(cc, S, S);
	for (size_t i = 0; i < annos->n; i++)
		if ((int32_t)i != skip)
			annotation_paint_backdrop(cc, &annos->items[i], 1.0, o->cairo_dst);
	cairo_destroy(cc);
	cairo_surface_flush(o->anno_cache);
	o->anno_cache_gen = annos->gen;
	o->anno_cache_sel = sel;
	o->anno_cache_skip = skip;
}

void gren_anno_cache_paint(cairo_t *cr, cairo_surface_t *cache) {
	cairo_save(cr);
	cairo_identity_matrix(cr);
	cairo_set_source_surface(cr, cache, 0, 0);
	cairo_paint(cr);
	cairo_restore(cr);
}

static void hint_text_extents(cairo_t *cr, double S, const char *hint,
							  cairo_text_extents_t *ext) {
	cairo_select_font_face(cr, "sans-serif",
						   CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
	cairo_set_font_size(cr, 12.0 * S);
	cairo_text_extents(cr, hint, ext);
}

static void render_hint_pill(cairo_t *cr, double S, const char *hint,
							 const cairo_text_extents_t *ext, double cx, double ty,
							 double pw) {
	double pad = 8.0 * S;
	double tx = cx - ext->width / 2.0;
	if (tx < pad) tx = pad;
	if (tx + ext->width + pad > pw) tx = pw - ext->width - pad;
	cairo_set_source_rgba(cr, 0, 0, 0, 0.78);
	cairo_rectangle(cr, tx - pad, ty - ext->height - pad,
					ext->width + pad * 2, ext->height + pad * 2);
	cairo_fill(cr);
	cairo_set_source_rgba(cr, 1, 1, 1, 1);
	cairo_move_to(cr, tx, ty);
	cairo_show_text(cr, hint);
}

void gren_render_bottom_hint(cairo_t *cr, const struct ro_output *o, const char *hint) {
	int32_t S = o->scale;
	cairo_text_extents_t hext;
	hint_text_extents(cr, S, hint, &hext);
	double pad = 8.0 * S;
	double ty = (double)o->pixel_height - 24.0 * S;
	if (region_editing(o->st)) {
		int32_t tbx, tby, tbw, tbh;
		region_toolbar_rect(o->st, NULL, &tbx, &tby, &tbw, &tbh);
		if (grabit_output_overlaps(o->go, (struct rect){tbx, tby, tbw, tbh})) {
			double tb_top = (double)(tby - o->go->y) * S;
			double tb_bot = (double)(tby + tbh - o->go->y) * S;
			double pill_top = ty - hext.height - pad;
			if (pill_top < tb_bot + 6.0 * S && ty + pad > tb_top - 6.0 * S)
				ty = tb_top - 6.0 * S - pad;
		}
	}
	render_hint_pill(cr, S, hint, &hext, (double)o->pixel_width / 2.0, ty,
					 (double)o->pixel_width);
}

void gren_paint_anno_selection(cairo_t *cr, const struct ro_state *st) {
	const struct annotation *a = region_anno_selected(st);
	if (!a) return;
	cairo_set_source_rgba(cr, 1, 1, 1, 0.9);
	cairo_set_line_width(cr, 1.2);
	double sel_dash[2] = {4.0, 4.0};
	cairo_set_dash(cr, sel_dash, 2, 0);
	cairo_rectangle(cr, a->bbox.x - 2, a->bbox.y - 2,
					a->bbox.w + 4, a->bbox.h + 4);
	cairo_stroke(cr);
	cairo_set_dash(cr, NULL, 0, 0);
	int mask = annotation_corner_mask(a);
	for (int c = 0; c < 4; c++) {
		if (!(mask & (1 << c))) continue;
		double hx = annotation_corner_x(a, c);
		double hy = annotation_corner_y(a, c);
		cairo_set_source_rgba(cr, 1, 1, 1, 0.95);
		cairo_rectangle(cr, hx - 4, hy - 4, 8, 8);
		cairo_fill(cr);
		cairo_set_source_rgba(cr, 0, 0, 0, 0.85);
		cairo_set_line_width(cr, 1.0);
		cairo_rectangle(cr, hx - 4, hy - 4, 8, 8);
		cairo_stroke(cr);
	}
}

static void frame_done(void *data, struct wl_callback *cb, uint32_t time) {
	(void)time;
	struct ro_output *o = data;
	wl_callback_destroy(cb);
	o->frame_cb = NULL;
	if (o->st->cleanup) return;
	if (o->dirty) gren_output_redraw(o);
}

const struct wl_callback_listener gren_frame_listener_g = {
	.done = frame_done,
};

static void output_request_redraw(struct ro_output *o) {
	o->dirty = true;
	if (o->frame_cb) return;
	gren_output_redraw(o);
}

void region_render_request_redraw_all(struct ro_state *st) {
	for (size_t i = 0; i < st->n_outs; i++)
		output_request_redraw(&st->outs[i]);
}

struct ro_output *region_render_find_by_surface(struct ro_state *st, struct wl_surface *s) {
	for (size_t i = 0; i < st->n_outs; i++) {
		if (st->outs[i].surface == s) return &st->outs[i];
	}
	return NULL;
}

static void layer_surface_configure(void *data, struct zwlr_layer_surface_v1 *ls,
									uint32_t serial, uint32_t w, uint32_t h) {
	struct ro_output *o = data;
	if (o->st->cleanup) {
		zwlr_layer_surface_v1_ack_configure(ls, serial);
		return;
	}
	o->width = (int32_t)w;
	o->height = (int32_t)h;
	zwlr_layer_surface_v1_ack_configure(ls, serial);

	region_render_free_buffer(o);
	if (gren_output_alloc_buffer(o) != 0) {
		o->st->cancelled = true;
		o->st->finished = true;
		return;
	}
	o->configured = true;
	gren_output_redraw(o);
}

static void layer_surface_closed(void *data, struct zwlr_layer_surface_v1 *ls) {
	(void)ls;
	struct ro_output *o = data;
	o->st->cancelled = true;
	o->st->finished = true;
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener_g = {
	.configure = layer_surface_configure,
	.closed = layer_surface_closed,
};

void region_render_attach_layer(struct ro_output *o) {
	zwlr_layer_surface_v1_add_listener(o->layer_surface,
									   &layer_surface_listener_g, o);
}
