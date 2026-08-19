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
#include <time.h>

#include <cairo/cairo.h>
#include <wayland-client.h>

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#include "region/render_internal.h"

static uint64_t spotlight_mix(uint64_t h, const struct annotation *a) {
	if (!a || !tool_is_layer(a->tool)) return h;
	int32_t v[5] = {a->x0, a->y0, a->x1, a->y1, a->width};
	for (int k = 0; k < 5; k++)
		h = (h ^ (uint64_t)(uint32_t)v[k]) * 1099511628211ULL;
	return h;
}

static uint64_t spotlight_sig(const struct annotation_list *l,
							  const struct annotation *live) {
	uint64_t h = 1469598103934665603ULL;
	size_t n = l ? l->n : 0;
	for (size_t i = 0; i < n; i++)
		h = spotlight_mix(h, &l->items[i]);
	return spotlight_mix(h, live);
}

void gren_anno_cache_ensure(struct ro_output *o, cairo_surface_t *dst,
							const struct annotation *live) {
	const struct annotation_list *annos = o->st->out_annos;
	int32_t S = o->scale;
	int32_t dragging = region_anno_dragging(o->st) ? 1 : 0;

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
	uint64_t spot = spotlight_sig(annos, live);
	if (o->anno_cache && o->anno_cache_gen == annos->gen &&
		o->anno_cache_skip == dragging && o->anno_cache_spot == spot &&
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
	ganno_paint_spotlights(cc, annos, live);
	for (size_t i = 0; i < annos->n; i++)
		if (!(dragging && annos->items[i].selected))
			annotation_paint_backdrop(cc, &annos->items[i], 1.0, dst);
	cairo_destroy(cc);
	cairo_surface_flush(o->anno_cache);
	o->anno_cache_gen = annos->gen;
	o->anno_cache_sel = sel;
	o->anno_cache_skip = dragging;
	o->anno_cache_spot = spot;
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
							 double pw, bool rounded) {
	double pad = 8.0 * S;
	double tx = cx - ext->width / 2.0;
	if (tx < pad) tx = pad;
	if (tx + ext->width + pad > pw) tx = pw - ext->width - pad;
	double r = rounded ? 6.0 * S : 0.0;
	cairo_set_source_rgba(cr, 0, 0, 0, 0.78);
	grabit_cairo_rounded_rect(cr, tx - pad, ty - ext->height - pad,
							  ext->width + pad * 2, ext->height + pad * 2, r);
	cairo_fill(cr);
	cairo_set_source_rgba(cr, 1, 1, 1, 1);
	cairo_move_to(cr, tx, ty);
	cairo_show_text(cr, hint);
}

void gren_render_bottom_hint(cairo_t *cr, const struct ro_output *o, const char *hint) {
	int32_t S = o->scale;
	cairo_text_extents_t hext;
	hint_text_extents(cr, S, hint, &hext);
	double ty = (double)o->pixel_height - 24.0 * S;
	if (region_editing(o->st)) {
		int32_t tbx, tby, tbw, tbh;
		region_toolbar_rect(o->st, NULL, &tbx, &tby, &tbw, &tbh);
		if (grabit_output_overlaps(o->go, (struct rect){tbx, tby, tbw, tbh})) {
			double tby_px = (double)(tby - o->go->y) * S;
			double tbh_px = (double)tbh * S;
			if (tby_px + tbh_px > (double)o->pixel_height - 60.0 * S)
				ty = tby_px - 8.0 * S;
		}
	}
	render_hint_pill(cr, S, hint, &hext, (double)o->pixel_width / 2.0, ty, (double)o->pixel_width, o->st->rounded_ui);
}

void gren_paint_anno_selection(cairo_t *cr, const struct ro_state *st) {
	if (!st->out_annos) return;
	double sel_dash[2] = {4.0, 4.0};
	cairo_set_source_rgba(cr, 1, 1, 1, 0.9);
	cairo_set_line_width(cr, 1.2);
	cairo_set_dash(cr, sel_dash, 2, 0);
	for (size_t i = 0; i < st->out_annos->n; i++) {
		const struct annotation *sa = &st->out_annos->items[i];
		if (!sa->selected) continue;
		cairo_rectangle(cr, sa->bbox.x - 2, sa->bbox.y - 2,
						sa->bbox.w + 4, sa->bbox.h + 4);
		cairo_stroke(cr);
	}
	cairo_set_dash(cr, NULL, 0, 0);
	const struct annotation *a = region_single_selection(st);
	if (!a) return;
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

static uint64_t current_time_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)(ts.tv_nsec / 1000000L);
}

static void region_step_animation(struct ro_state *st) {
	if (!st->anim_enabled || !st->anim_active) return;

	uint64_t now = current_time_ms();
	if (st->anim_last_time_ms == 0) {
		st->anim_last_time_ms = now;
		return;
	}

	double dt = (double)(now - st->anim_last_time_ms) / 1000.0;
	st->anim_last_time_ms = now;
	if (dt > 0.1) dt = 0.1;
	if (dt <= 0.0) dt = 0.001;

	double speed = st->anim_speed > 0.0 ? st->anim_speed : 18.0;
	double factor = 1.0 - exp(-speed * dt);
	if (factor > 1.0) factor = 1.0;

	st->anim_x += (st->target_x - st->anim_x) * factor;
	st->anim_y += (st->target_y - st->anim_y) * factor;
	st->anim_w += (st->target_w - st->anim_w) * factor;
	st->anim_h += (st->target_h - st->anim_h) * factor;
	st->anim_r += (st->target_r - st->anim_r) * factor;
	st->anim_alpha += (st->target_alpha - st->anim_alpha) * factor;

	double dx = fabs(st->anim_x - st->target_x);
	double dy = fabs(st->anim_y - st->target_y);
	double dw = fabs(st->anim_w - st->target_w);
	double dh = fabs(st->anim_h - st->target_h);
	double dr = fabs(st->anim_r - st->target_r);
	double da = fabs(st->anim_alpha - st->target_alpha);

	if (dx < 0.5 && dy < 0.5 && dw < 0.5 && dh < 0.5 && dr < 0.5 && da < 0.01) {
		st->anim_x = st->target_x;
		st->anim_y = st->target_y;
		st->anim_w = st->target_w;
		st->anim_h = st->target_h;
		st->anim_r = st->target_r;
		st->anim_alpha = st->target_alpha;
		st->anim_active = false;
		st->anim_last_time_ms = 0;
	}
}

static void frame_done(void *data, struct wl_callback *cb, uint32_t time) {
	(void)time;
	struct ro_output *o = data;
	wl_callback_destroy(cb);
	o->frame_cb = NULL;
	if (o->st->cleanup) return;
	if (o->st->anim_enabled && o->st->anim_active) {
		region_step_animation(o->st);
		o->dirty = true;
	}
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
	if (st->anim_enabled && st->anim_active && st->anim_last_time_ms == 0) {
		st->anim_last_time_ms = current_time_ms();
	}
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

	gren_output_configure(o);
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
