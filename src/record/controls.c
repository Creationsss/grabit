// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "record/controls.h"
#include "record/controls_internal.h"

#include "cursor.h"
#include "hyprland.h"
#include "log.h"
#include "util/util.h"
#include "wl/wl.h"

#include <stdlib.h>

#include <wayland-client.h>
#include <wayland-cursor.h>

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

void ctl_apply_input_region(struct ctl_output *o) {
	struct rec_controls *c = o->st;
	struct wl_region *reg = wl_compositor_create_region(c->wls->compositor);
	if (!reg) return;
	if (c->dragging) {
		wl_region_add(reg, 0, 0, o->width, o->height);
	} else {
		struct rect b = ctl_bar_rect(c);
		int32_t ix, iy, iw, ih;
		if (grabit_output_rect_intersect(o->go, &b, &ix, &iy, &iw, &ih))
			wl_region_add(reg, ix - o->go->x, iy - o->go->y, iw, ih);
	}
	wl_surface_set_input_region(o->surface, reg);
	wl_region_destroy(reg);
}

static void layer_configure(void *data, struct zwlr_layer_surface_v1 *ls,
							uint32_t serial, uint32_t w, uint32_t h) {
	struct ctl_output *o = data;
	zwlr_layer_surface_v1_ack_configure(ls, serial);
	o->width = (int32_t)w;
	o->height = (int32_t)h;
	o->scale = o->go->scale > 0 ? o->go->scale : 1;
	o->pixel_w = o->width * o->scale;
	o->pixel_h = o->height * o->scale;

	grabit_shm_release(&o->buffer, &o->buf_data, &o->buf_size);
	struct grabit_shm_buf b;
	if (grabit_shm_argb_buf(o->st->wls->shm, "grabit-rec-controls",
							o->pixel_w, o->pixel_h, &b) != 0)
		return;
	o->buffer = b.buffer;
	o->buf_data = b.map;
	o->buf_size = b.size;
	wl_surface_set_buffer_scale(o->surface, o->scale);
	o->configured = true;
	o->mapped = false;
	o->shown = (struct rect){0, 0, 0, 0};
	ctl_apply_input_region(o);
	ctl_output_redraw(o);
}

static void layer_closed(void *data, struct zwlr_layer_surface_v1 *ls) {
	(void)data;
	(void)ls;
}

static const struct zwlr_layer_surface_v1_listener layer_listener_g = {
	.configure = layer_configure,
	.closed = layer_closed,
};

static bool try_output(const struct grabit_output *o, struct rect r,
					   int32_t w, int32_t h, int32_t *bx, int32_t *by) {
	int32_t x = o->x + (o->logical_width - w) / 2;
	int32_t ys[2] = {o->y + CB_EDGE_GAP,
					 o->y + o->logical_height - CB_EDGE_GAP - h};
	for (int i = 0; i < 2; i++) {
		if (!rects_overlap((struct rect){x, ys[i], w, h}, r)) {
			*bx = x;
			*by = ys[i];
			return true;
		}
	}
	return false;
}

static bool place_bar(struct grabit_wl_state *s, struct rect r,
					  int32_t w, int32_t h, int32_t *bx, int32_t *by) {
	const struct grabit_output *cur = NULL;
	int32_t cpx = 0, cpy = 0;
	if (grabit_hyprland_cursorpos(&cpx, &cpy) == 0)
		cur = grabit_wl_output_at(s, cpx, cpy);
	if (!cur) cur = grabit_wl_output_at(s, r.x + r.w / 2, r.y + r.h / 2);
	if (!cur) cur = grabit_wl_primary_output(s);
	if (!cur) return false;

	if (try_output(cur, r, w, h, bx, by)) return true;
	for (size_t i = 0; i < s->n_outputs; i++) {
		const struct grabit_output *o = s->outputs[i];
		if (o == cur) continue;
		if (try_output(o, r, w, h, bx, by)) return true;
	}
	return false;
}

struct rec_controls *controls_start(struct grabit_wl_state *s, struct rect r,
									atomic_int *stop_flag, atomic_int *pause_flag) {
	if (!s || !s->layer_shell || !s->compositor || !s->shm || s->n_outputs == 0)
		return NULL;

	int32_t w = ctl_bar_width(), h = CB_H;
	int32_t bx = 0, by = 0;
	if (!place_bar(s, r, w, h, &bx, &by)) {
		log_info("recording: no room for the control bar outside the region; "
				 "re-run `grabit --record` to stop");
		return NULL;
	}

	struct rec_controls *c = calloc(1, sizeof *c);
	if (!c) return NULL;
	c->wls = s;
	c->bw = w;
	c->bh = h;
	c->bx = bx;
	c->by = by;
	c->stop_flag = stop_flag;
	c->pause_flag = pause_flag;
	grabit_wl_outputs_bbox(s, &c->bounds);

	c->outs = calloc(s->n_outputs, sizeof *c->outs);
	if (!c->outs) {
		free(c);
		return NULL;
	}
	c->n = s->n_outputs;

	for (size_t i = 0; i < c->n; i++) {
		struct ctl_output *o = &c->outs[i];
		o->st = c;
		o->go = s->outputs[i];
		o->surface = wl_compositor_create_surface(s->compositor);
		o->layer = grabit_wl_layer_fullscreen(
			s, o->surface, o->go->wl_output, "grabit-rec-controls",
			ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE,
			&layer_listener_g, o);
		if (!o->layer) continue;
		ctl_apply_input_region(o);
		wl_surface_commit(o->surface);
	}

	if (s->seat && (s->seat_caps & WL_SEAT_CAPABILITY_POINTER)) {
		c->pointer = wl_seat_get_pointer(s->seat);
		if (c->pointer) ctl_input_attach(c);
		int32_t max_scale = 1;
		for (size_t i = 0; i < s->n_outputs; i++) {
			if (s->outputs[i]->scale > max_scale) max_scale = s->outputs[i]->scale;
		}
		c->cursor_theme = grabit_cursor_theme_load(s->shm, max_scale);
		if (c->cursor_theme) {
			c->cursor_hand = grabit_cursor_load_hand(c->cursor_theme);
			if (c->cursor_hand)
				c->cursor_surface = wl_compositor_create_surface(s->compositor);
		}
	}

	wl_display_roundtrip(s->display);
	return c;
}

void controls_set_paused(struct rec_controls *c, bool paused) {
	if (!c || c->paused == paused) return;
	c->paused = paused;
	ctl_redraw_all(c);
}

void controls_tick(struct rec_controls *c, int64_t secs) {
	if (!c || c->secs == secs) return;
	c->secs = secs;
	ctl_redraw_all(c);
}

void controls_stop(struct rec_controls *c) {
	if (!c) return;
	if (c->pointer) wl_pointer_release(c->pointer);
	if (c->cursor_surface) wl_surface_destroy(c->cursor_surface);
	if (c->cursor_theme) wl_cursor_theme_destroy(c->cursor_theme);
	for (size_t i = 0; i < c->n; i++) {
		struct ctl_output *o = &c->outs[i];
		grabit_wl_callback_drop(&o->frame_cb);
		grabit_shm_release(&o->buffer, &o->buf_data, &o->buf_size);
		if (o->layer) zwlr_layer_surface_v1_destroy(o->layer);
		if (o->surface) wl_surface_destroy(o->surface);
	}
	free(c->outs);
	if (c->wls && c->wls->display) wl_display_roundtrip(c->wls->display);
	free(c);
}
