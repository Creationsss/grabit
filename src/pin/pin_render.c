// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "pin/pin_state.h"

#include <stdio.h>
#include <string.h>

#include "cairo_util.h"
#include "util/util.h"
#include "wl.h"

#include <math.h>
#include <stdint.h>

#include <cairo/cairo.h>
#include <wayland-client.h>

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

int pin_render_output_alloc(struct pin_output *o) {
	o->pixel_w = o->width * o->scale;
	o->pixel_h = o->height * o->scale;
	if (o->pixel_w <= 0 || o->pixel_h <= 0) return -1;

	struct grabit_shm_buf b;
	if (grabit_shm_argb_buf(o->st->wls->shm, "grabit-pin",
							o->pixel_w, o->pixel_h, &b) != 0) {
		return -1;
	}
	o->buffer = b.buffer;
	o->buf_data = b.map;
	o->buf_size = b.size;
	o->dst = grabit_cairo_image_argb(o->buf_data, o->pixel_w, o->pixel_h,
									 o->pixel_w * 4);
	if (!o->dst) {
		grabit_shm_release(&o->buffer, &o->buf_data, &o->buf_size);
		return -1;
	}
	wl_surface_set_buffer_scale(o->surface, o->scale);
	o->shown = (struct rect){0, 0, 0, 0};
	return 0;
}

void pin_render_output_free(struct pin_output *o) {
	grabit_wl_callback_drop(&o->frame_cb);
	if (o->dst) {
		cairo_surface_destroy(o->dst);
		o->dst = NULL;
	}
	grabit_shm_release(&o->buffer, &o->buf_data, &o->buf_size);
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

	struct rect cur = {0, 0, 0, 0};
	struct rect p = pin_rect(st);
	int32_t ix, iy, iw, ih;
	if (grabit_output_rect_intersect(o->go, &p, &ix, &iy, &iw, &ih)) {
		cur = (struct rect){(ix - o->go->x) * o->scale, (iy - o->go->y) * o->scale,
							iw * o->scale, ih * o->scale};
	}
	if (cur.w == 0 && o->shown.w == 0) return;

	if (!o->buf_data && pin_render_output_alloc(o) != 0) return;

	cairo_t *cr = cairo_create(o->dst);

	cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
	if (o->shown.w > 0) {
		cairo_rectangle(cr, o->shown.x, o->shown.y, o->shown.w, o->shown.h);
		cairo_fill(cr);
	}

	if (cur.w > 0 && st->image) {
		cairo_save(cr);
		cairo_rectangle(cr, cur.x, cur.y, cur.w, cur.h);
		cairo_clip(cr);
		cairo_scale(cr, o->scale, o->scale);
		cairo_translate(cr, st->px - o->go->x, st->py - o->go->y);

		cairo_save(cr);
		double sx = st->img_w > 0 ? (double)st->width / (double)st->img_w : 1.0;
		double sy = st->img_h > 0 ? (double)st->height / (double)st->img_h : 1.0;
		cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
		cairo_scale(cr, sx, sy);
		cairo_set_source_surface(cr, st->image, 0, 0);
		cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
		cairo_paint(cr);
		cairo_restore(cr);

		cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
		if (st->input_grabbed && st->width > 0)
			draw_close_button(cr, st->width);
		if (st->transient && st->hover_caption && st->hover_active && st->width > 0)
			draw_caption(cr, st);

		cairo_restore(cr);
	}

	cairo_destroy(cr);
	cairo_surface_flush(o->dst);

	o->frame_cb = wl_surface_frame(o->surface);
	wl_callback_add_listener(o->frame_cb, &frame_listener_g, o);
	wl_surface_attach(o->surface, o->buffer, 0, 0);
	if (o->shown.w > 0)
		wl_surface_damage_buffer(o->surface, o->shown.x, o->shown.y,
								 o->shown.w, o->shown.h);
	if (cur.w > 0)
		wl_surface_damage_buffer(o->surface, cur.x, cur.y, cur.w, cur.h);
	wl_surface_commit(o->surface);
	o->shown = cur;
}

static void output_request_redraw(struct pin_output *o) {
	o->dirty = true;
	if (o->frame_cb) return;
	pin_render_output_redraw(o);
}

void pin_render_redraw_all(struct pin_state *st) {
	for (size_t i = 0; i < st->n; i++)
		output_request_redraw(&st->outs[i]);
}

static void layer_surface_configure(void *data, struct zwlr_layer_surface_v1 *ls,
									uint32_t serial, uint32_t w, uint32_t h) {
	struct pin_output *o = data;
	zwlr_layer_surface_v1_ack_configure(ls, serial);
	if (w > 0) o->width = (int32_t)w;
	if (h > 0) o->height = (int32_t)h;
	o->scale = o->go->scale > 0 ? o->go->scale : 1;

	int32_t want_pw = o->width * o->scale;
	int32_t want_ph = o->height * o->scale;
	if (o->buf_data && (want_pw != o->pixel_w || want_ph != o->pixel_h))
		pin_render_output_free(o);
	o->configured = true;
	pin_input_apply_region(o);
	pin_render_output_redraw(o);
}

static void layer_surface_closed(void *data, struct zwlr_layer_surface_v1 *ls) {
	(void)ls;
	struct pin_output *o = data;
	o->configured = false;
	pin_render_output_free(o);
	o->shown = (struct rect){0, 0, 0, 0};

	struct pin_state *st = o->st;
	for (size_t i = 0; i < st->n; i++) {
		if (st->outs[i].configured) return;
	}
	st->finished = true;
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener_g = {
	.configure = layer_surface_configure,
	.closed = layer_surface_closed,
};

void pin_render_attach_layer(struct pin_output *o) {
	zwlr_layer_surface_v1_add_listener(o->layer, &layer_surface_listener_g, o);
}
