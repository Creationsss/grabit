// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "ui/window_internal.h"

#include "cairo_util.h"
#include "util.h"
#include "wl.h"

#include <wayland-client.h>
#include <wayland-cursor.h>

static void frame_done(void *data, struct wl_callback *cb, uint32_t time) {
	(void)time;
	struct ui_window *w = data;
	wl_callback_destroy(cb);
	w->frame_cb = NULL;
	if (w->finished) return;
	if (w->dirty) uiw_redraw(w);
}

static const struct wl_callback_listener frame_listener_g = {.done = frame_done};

void ui_window_redraw(struct ui_window *w) {
	if (!w) return;
	w->dirty = true;
	if (w->frame_cb) return;
	uiw_redraw(w);
}

void uiw_free_buffer(struct ui_window *w) {
	grabit_wl_callback_drop(&w->frame_cb);
	if (w->cairo_surf) {
		cairo_surface_destroy(w->cairo_surf);
		w->cairo_surf = NULL;
	}
	grabit_shm_release(&w->buffer, &w->buf_data, &w->buf_size);
}

int uiw_alloc_buffer(struct ui_window *w) {
	w->scale = w->output && w->output->scale > 0 ? w->output->scale : 1;
	w->pixel_w = w->logical_w * w->scale;
	w->pixel_h = w->logical_h * w->scale;
	if (w->pixel_w <= 0 || w->pixel_h <= 0) return -1;

	struct grabit_shm_buf b;
	if (grabit_shm_argb_buf(w->wls->shm, "grabit-ui", w->pixel_w, w->pixel_h, &b) != 0)
		return -1;
	w->buffer = b.buffer;
	w->buf_data = b.map;
	w->buf_size = b.size;
	w->stride = w->pixel_w * 4;
	w->cairo_surf = grabit_cairo_image_argb(w->buf_data, w->pixel_w, w->pixel_h, w->stride);
	if (!w->cairo_surf) {
		grabit_shm_release(&w->buffer, &w->buf_data, &w->buf_size);
		return -1;
	}
	wl_surface_set_buffer_scale(w->surface, w->scale);
	return 0;
}

void uiw_redraw(struct ui_window *w) {
	if (!w->configured || !w->cairo_surf || w->finished) return;
	w->dirty = false;

	cairo_t *cr = cairo_create(w->cairo_surf);
	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
	cairo_set_source_rgba(cr, 0, 0, 0, 0);
	cairo_paint(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
	cairo_scale(cr, w->scale, w->scale);
	if (w->on_draw) w->on_draw(cr, w->logical_w, w->logical_h, w->user);
	cairo_destroy(cr);
	cairo_surface_flush(w->cairo_surf);

	w->frame_cb = wl_surface_frame(w->surface);
	wl_callback_add_listener(w->frame_cb, &frame_listener_g, w);
	wl_surface_attach(w->surface, w->buffer, 0, 0);
	wl_surface_damage_buffer(w->surface, 0, 0, w->pixel_w, w->pixel_h);
	wl_surface_commit(w->surface);
}

static struct wl_cursor *cursor_for(struct ui_window *w, enum ui_cursor c) {
	switch (c) {
	case UI_CURSOR_HAND:
		return w->cursor_hand ? w->cursor_hand : w->cursor_default;
	case UI_CURSOR_TEXT:
		return w->cursor_text ? w->cursor_text : w->cursor_default;
	default:
		return w->cursor_default;
	}
}

void uiw_apply_cursor(struct ui_window *w) {
	struct wl_cursor *c = w->cursor_active;
	if (!w->pointer || !w->cursor_surface || !c || c->image_count == 0) return;
	struct wl_cursor_image *img = c->images[0];
	struct wl_buffer *buf = wl_cursor_image_get_buffer(img);
	if (!buf) return;
	wl_pointer_set_cursor(w->pointer, w->enter_serial, w->cursor_surface,
						  (int32_t)img->hotspot_x / w->scale, (int32_t)img->hotspot_y / w->scale);
	wl_surface_set_buffer_scale(w->cursor_surface, w->scale);
	wl_surface_attach(w->cursor_surface, buf, 0, 0);
	wl_surface_damage_buffer(w->cursor_surface, 0, 0, (int32_t)img->width, (int32_t)img->height);
	wl_surface_commit(w->cursor_surface);
}

void ui_window_set_cursor(struct ui_window *w, enum ui_cursor c) {
	if (!w) return;
	struct wl_cursor *want = cursor_for(w, c);
	if (want == w->cursor_active) return;
	w->cursor_active = want;
	uiw_apply_cursor(w);
}
