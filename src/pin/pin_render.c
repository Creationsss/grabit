// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "pin/pin_state.h"

#include <stdio.h>
#include <string.h>

#include "cairo_util.h"
#include "util/util.h"
#include "wl/wl.h"

#include <math.h>
#include <stdint.h>

#include <cairo/cairo.h>
#include <wayland-client.h>

#include "fractional-scale-v1-client-protocol.h"
#include "viewporter-client-protocol.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

void pin_render_output_free(struct pin_output *o) {
	grabit_wl_callback_drop(&o->frame_cb);
	grabit_shm_pool_finish(&o->pool);
}

static void draw_close_button(cairo_t *cr, int32_t width) {
	double bw = PIN_CLOSE_BTN_SIZE;
	double bx = (double)width - bw - PIN_CLOSE_BTN_INSET;
	double by = PIN_CLOSE_BTN_INSET;

	cairo_save(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

	cairo_set_source_rgba(cr, 0, 0, 0, 0.78);
	cairo_arc(cr, bx + bw / 2.0, by + bw / 2.0, bw / 2.0, 0, 2.0 * M_PI);
	cairo_fill(cr);

	cairo_set_source_rgba(cr, 1, 1, 1, 1);
	cairo_set_line_width(cr, 2.5);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	double pad = 7.5;
	cairo_move_to(cr, bx + pad, by + pad);
	cairo_line_to(cr, bx + bw - pad, by + bw - pad);
	cairo_move_to(cr, bx + bw - pad, by + pad);
	cairo_line_to(cr, bx + pad, by + bw - pad);
	cairo_stroke(cr);

	cairo_restore(cr);
}

static void draw_caption(cairo_t *cr, struct pin_state *st) {
	double font = 14.0;
	double pad = 8.0;
	cairo_select_font_face(cr, "sans-serif",
						   CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
	cairo_set_font_size(cr, font);
	if (st->caption_fit_width != st->width || !st->caption_fit[0]) {
		cairo_text_extents_t ext;
		cairo_text_extents(cr, st->hover_caption, &ext);
		double max_w = (double)st->width - 2 * pad;
		const char *src = st->hover_caption;
		if (ext.x_advance <= max_w) {
			snprintf(st->caption_fit, sizeof st->caption_fit, "%s", src);
			st->caption_fit_x_advance = ext.x_advance;
		} else {
			size_t len = strlen(src);
			size_t fit = 0;
			for (size_t i = 1; i < len && i < sizeof st->caption_fit - 5; i++) {
				if (((unsigned char)src[i] & 0xC0) == 0x80) continue;
				memcpy(st->caption_fit, src, i);
				memcpy(st->caption_fit + i, "…", 4);
				cairo_text_extents(cr, st->caption_fit, &ext);
				if (ext.x_advance > max_w) break;
				fit = i;
			}
			if (fit == 0) fit = 1;
			memcpy(st->caption_fit, src, fit);
			memcpy(st->caption_fit + fit, "…", 4);
			st->caption_fit[fit + 3] = '\0';
			cairo_text_extents(cr, st->caption_fit, &ext);
			st->caption_fit_x_advance = ext.x_advance;
		}
		st->caption_fit_width = st->width;
	}
	double bar_h = font + 2 * pad;
	double bar_y = (double)st->height - bar_h;
	cairo_set_source_rgba(cr, 0, 0, 0, 0.7);
	cairo_rectangle(cr, 0, bar_y, st->width, bar_h);
	cairo_fill(cr);
	cairo_set_source_rgba(cr, 1, 1, 1, 1);
	double tx = ((double)st->width - st->caption_fit_x_advance) / 2.0;
	if (tx < pad) tx = pad;
	cairo_move_to(cr, tx, bar_y + pad + font * 0.85);
	cairo_show_text(cr, st->caption_fit);
}

static void frame_done(void *data, struct wl_callback *cb, uint32_t time) {
	(void)cb;
	(void)time;
	struct pin_output *o = data;
	grabit_wl_callback_drop(&o->frame_cb);
	if (o->dirty) pin_render_output_redraw(o);
}

static const struct wl_callback_listener frame_listener_g = {
	.done = frame_done,
};

void pin_render_output_redraw(struct pin_output *o) {
	if (!o->configured) return;
	struct pin_state *st = o->st;
	o->dirty = false;

	bool frac = o->frac_scale > 0;
	uint32_t scale_120 = frac ? o->frac_scale : (uint32_t)o->scale * 120;
	double scale = scale_120 / 120.0;
	int32_t pixel_w = (int32_t)((o->width * scale_120 + 60) / 120);
	int32_t pixel_h = (int32_t)((o->height * scale_120 + 60) / 120);
	if (pixel_w <= 0 || pixel_h <= 0) return;

	struct grabit_shm_slot *slot = grabit_shm_pool_next(
		st->wls->shm, "grabit-pin", &o->pool, pixel_w, pixel_h);
	if (!slot) {
		o->dirty = true;
		return;
	}
	cairo_surface_t *dst = grabit_cairo_image_argb(slot->buf.map, pixel_w,
												   pixel_h, pixel_w * 4);
	if (!dst) return;

	cairo_t *cr = cairo_create(dst);

	if (st->image) {
		cairo_save(cr);
		double sx = st->img_w > 0 ? (double)st->width / (double)st->img_w : 1.0;
		double sy = st->img_h > 0 ? (double)st->height / (double)st->img_h : 1.0;
		cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
		cairo_scale(cr, scale, scale);
		cairo_scale(cr, sx, sy);
		cairo_set_source_surface(cr, st->image, 0, 0);
		cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
		cairo_paint(cr);
		cairo_restore(cr);

		cairo_scale(cr, scale, scale);
		cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
		if (st->input_grabbed && st->width > 0)
			draw_close_button(cr, st->width);
		if (st->transient && st->hover_caption && st->hover_active && st->width > 0)
			draw_caption(cr, st);
	}

	cairo_destroy(cr);
	cairo_surface_flush(dst);
	cairo_surface_destroy(dst);

	o->frame_cb = wl_surface_frame(o->surface);
	wl_callback_add_listener(o->frame_cb, &frame_listener_g, o);
	if (frac) {
		wl_surface_set_buffer_scale(o->surface, 1);
		wp_viewport_set_destination(o->viewport, o->width, o->height);
	} else {
		wl_surface_set_buffer_scale(o->surface, o->scale);
	}
	grabit_shm_slot_attach(o->surface, slot);
	wl_surface_damage_buffer(o->surface, 0, 0, pixel_w, pixel_h);
	wl_surface_commit(o->surface);
}

static void output_request_redraw(struct pin_output *o) {
	o->dirty = true;
	if (o->frame_cb) return;
	pin_render_output_redraw(o);
}

static void fractional_preferred_scale(void *data,
									   struct wp_fractional_scale_v1 *f,
									   uint32_t scale) {
	(void)f;
	struct pin_output *o = data;
	if (o->frac_scale == scale) return;
	o->frac_scale = scale;
	output_request_redraw(o);
}

static const struct wp_fractional_scale_v1_listener fractional_listener_g = {
	.preferred_scale = fractional_preferred_scale,
};

void pin_render_create_fractional(struct pin_output *o) {
	struct grabit_wl_state *wls = o->st->wls;
	if (!wls->viewporter || !wls->fractional_scale_manager) return;
	o->viewport = wp_viewporter_get_viewport(wls->viewporter, o->surface);
	o->fractional = wp_fractional_scale_manager_v1_get_fractional_scale(
		wls->fractional_scale_manager, o->surface);
	wp_fractional_scale_v1_add_listener(o->fractional, &fractional_listener_g, o);
}

void pin_render_redraw_all(struct pin_state *st) {
	for (size_t i = 0; i < st->n; i++)
		output_request_redraw(st->outs[i]);
}

void pin_render_move_all(struct pin_state *st) {
	for (size_t i = 0; i < st->n; i++) {
		struct pin_output *o = st->outs[i];
		if (!o->layer || !o->configured) continue;
		zwlr_layer_surface_v1_set_margin(o->layer, st->py - o->go->y, 0, 0,
										 st->px - o->go->x);
		wl_surface_commit(o->surface);
	}
}

static void layer_surface_configure(void *data, struct zwlr_layer_surface_v1 *ls,
									uint32_t serial, uint32_t w, uint32_t h) {
	struct pin_output *o = data;
	zwlr_layer_surface_v1_ack_configure(ls, serial);
	if (w > 0) o->width = (int32_t)w;
	if (h > 0) o->height = (int32_t)h;
	o->scale = o->go->scale > 0 ? o->go->scale : 1;
	o->configured = true;
	pin_input_apply_region(o);
	pin_render_output_redraw(o);
}

static void layer_surface_closed(void *data, struct zwlr_layer_surface_v1 *ls) {
	(void)ls;
	struct pin_output *o = data;
	o->configured = false;
	pin_render_output_free(o);

	struct pin_state *st = o->st;
	for (size_t i = 0; i < st->n; i++) {
		if (st->outs[i]->configured) return;
	}
	st->finished = true;
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener_g = {
	.configure = layer_surface_configure,
	.closed = layer_surface_closed,
};

int pin_render_create_layer(struct pin_output *o) {
	struct pin_state *st = o->st;
	o->layer = zwlr_layer_shell_v1_get_layer_surface(
		st->wls->layer_shell, o->surface, o->go->wl_output,
		ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "grabit-pin");
	if (!o->layer) return -1;
	zwlr_layer_surface_v1_add_listener(o->layer, &layer_surface_listener_g, o);
	zwlr_layer_surface_v1_set_anchor(o->layer, ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
												   ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT);
	zwlr_layer_surface_v1_set_size(o->layer, (uint32_t)st->width,
								   (uint32_t)st->height);
	zwlr_layer_surface_v1_set_margin(o->layer, st->py - o->go->y, 0, 0,
									 st->px - o->go->x);
	zwlr_layer_surface_v1_set_exclusive_zone(o->layer, -1);
	zwlr_layer_surface_v1_set_keyboard_interactivity(
		o->layer, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
	return 0;
}
