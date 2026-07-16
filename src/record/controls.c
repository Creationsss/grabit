// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "record/controls.h"

#include "cairo_util.h"
#include "cursor.h"
#include "hyprland.h"
#include "log.h"
#include "region/region.h"
#include "util.h"
#include "wl.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include <linux/input-event-codes.h>

#include <cairo/cairo.h>
#include <wayland-client.h>
#include <wayland-cursor.h>

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#define CB_H 50
#define CB_PAD 6
#define CB_BTN 38
#define CB_GAP 2
#define CB_SEC_GAP 10
#define CB_DOT_W 14
#define CB_TIME_W 48
#define CB_EDGE_GAP 8

#define CB_BTN_START 0
#define CB_BTN_PAUSE 1
#define CB_BTN_STOP 2

struct ctl_output {
	struct rec_controls *st;
	struct grabit_output *go;
	struct wl_surface *surface;
	struct zwlr_layer_surface_v1 *layer;
	struct wl_buffer *buffer;
	void *buf_data;
	size_t buf_size;
	int32_t width;
	int32_t height;
	int32_t pixel_w;
	int32_t pixel_h;
	int32_t scale;
	bool configured;
	bool mapped;
	bool dirty;
	struct rect shown;
	struct wl_callback *frame_cb;
};

struct rec_controls {
	struct grabit_wl_state *wls;
	struct ctl_output *outs;
	size_t n;

	int32_t bx;
	int32_t by;
	int32_t bw;
	int32_t bh;
	struct rect bounds;

	atomic_int *stop_flag;
	atomic_int *pause_flag;
	bool paused;
	int64_t secs;

	struct wl_pointer *pointer;
	struct ctl_output *ptr_on;
	int32_t cx;
	int32_t cy;
	bool dragging;
	int32_t grab_dx;
	int32_t grab_dy;

	struct wl_cursor_theme *cursor_theme;
	struct wl_cursor *cursor_hand;
	struct wl_surface *cursor_surface;
};

static int32_t bar_width(void) {
	return CB_PAD + CB_DOT_W + 4 + CB_TIME_W + CB_SEC_GAP +
		   3 * CB_BTN + 2 * CB_GAP + CB_PAD;
}

static void btn_rect(int btn, int32_t *x, int32_t *y, int32_t *w, int32_t *h) {
	int32_t x0 = CB_PAD + CB_DOT_W + 4 + CB_TIME_W + CB_SEC_GAP;
	*x = x0 + btn * (CB_BTN + CB_GAP);
	*y = (CB_H - CB_BTN) / 2;
	*w = CB_BTN;
	*h = CB_BTN;
}

static int btn_at(int32_t x, int32_t y) {
	for (int b = 0; b < 3; b++) {
		int32_t bx, by, bw, bh;
		btn_rect(b, &bx, &by, &bw, &bh);
		if (rect_contains((struct rect){bx, by, bw, bh}, x, y)) return b;
	}
	return -1;
}

static struct rect bar_rect(const struct rec_controls *c) {
	return (struct rect){c->bx, c->by, c->bw, c->bh};
}

static void apply_input_region(struct ctl_output *o) {
	struct rec_controls *c = o->st;
	struct wl_region *reg = wl_compositor_create_region(c->wls->compositor);
	if (!reg) return;
	if (c->dragging) {
		wl_region_add(reg, 0, 0, o->width, o->height);
	} else {
		struct rect b = bar_rect(c);
		int32_t ix, iy, iw, ih;
		if (grabit_output_rect_intersect(o->go, &b, &ix, &iy, &iw, &ih))
			wl_region_add(reg, ix - o->go->x, iy - o->go->y, iw, ih);
	}
	wl_surface_set_input_region(o->surface, reg);
	wl_region_destroy(reg);
}

static void draw_bar(cairo_t *cr, const struct rec_controls *c) {
	cairo_set_source_rgba(cr, 0.08, 0.08, 0.08, 0.94);
	cairo_rectangle(cr, 0, 0, c->bw, c->bh);
	cairo_fill(cr);
	cairo_set_source_rgba(cr, 1, 1, 1, 0.16);
	cairo_set_line_width(cr, 1.0);
	cairo_rectangle(cr, 0.5, 0.5, c->bw - 1.0, c->bh - 1.0);
	cairo_stroke(cr);

	double cy = CB_H / 2.0;
	double dot_cx = CB_PAD + CB_DOT_W / 2.0;
	if (c->paused) {
		cairo_set_source_rgba(cr, 1.0, 0.55, 0.32, 1.0);
	} else {
		cairo_set_source_rgba(cr, 0.85, 0.1, 0.1, 1.0);
	}
	cairo_arc(cr, dot_cx, cy, CB_DOT_W / 2.0 - 2.0, 0, 2.0 * M_PI);
	cairo_fill(cr);

	char tbuf[32];
	snprintf(tbuf, sizeof tbuf, "%lld:%02lld",
			 (long long)(c->secs / 60), (long long)(c->secs % 60));
	cairo_select_font_face(cr, "sans-serif",
						   CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
	cairo_set_font_size(cr, 14.0);
	cairo_text_extents_t ext;
	cairo_text_extents(cr, tbuf, &ext);
	cairo_set_source_rgba(cr, 1, 1, 1, 0.92);
	cairo_move_to(cr, CB_PAD + CB_DOT_W + 4, cy - ext.height / 2.0 - ext.y_bearing);
	cairo_show_text(cr, tbuf);

	for (int btn = 0; btn < 3; btn++) {
		int32_t bx, by, bw, bh;
		btn_rect(btn, &bx, &by, &bw, &bh);
		bool enabled = (btn == CB_BTN_START) ? c->paused
					   : (btn == CB_BTN_PAUSE) ? !c->paused
											   : true;
		double pad = 3.0;
		double aa = enabled ? 0.96 : 0.35;
		if (btn == CB_BTN_START) {
			cairo_set_source_rgba(cr, 0.20, 0.58, 0.32, aa);
		} else if (btn == CB_BTN_PAUSE) {
			cairo_set_source_rgba(cr, 0.18, 0.18, 0.18, enabled ? 0.94 : 0.35);
		} else {
			cairo_set_source_rgba(cr, 0.62, 0.22, 0.22, aa);
		}
		cairo_rectangle(cr, bx + pad, by + pad, bw - pad * 2, bh - pad * 2);
		cairo_fill(cr);

		double ia = enabled ? 0.92 : 0.45;
		cairo_set_source_rgba(cr, 0.92, 0.92, 0.92, ia);
		double bcx = bx + bw / 2.0;
		double bcy = by + bh / 2.0;
		double s = bh * 0.6 * 0.36;
		if (btn == CB_BTN_START) {
			cairo_move_to(cr, bcx - s * 0.7, bcy - s);
			cairo_line_to(cr, bcx - s * 0.7, bcy + s);
			cairo_line_to(cr, bcx + s, bcy);
			cairo_close_path(cr);
			cairo_fill(cr);
		} else if (btn == CB_BTN_PAUSE) {
			cairo_rectangle(cr, bcx - s, bcy - s, s * 0.72, s * 2.0);
			cairo_rectangle(cr, bcx + s * 0.28, bcy - s, s * 0.72, s * 2.0);
			cairo_fill(cr);
		} else {
			cairo_rectangle(cr, bcx - s * 0.9, bcy - s * 0.9, s * 1.8, s * 1.8);
			cairo_fill(cr);
		}
	}
}

static void output_redraw(struct ctl_output *o);

static void frame_done(void *data, struct wl_callback *cb, uint32_t time) {
	(void)cb;
	(void)time;
	struct ctl_output *o = data;
	grabit_wl_callback_drop(&o->frame_cb);
	if (o->dirty) output_redraw(o);
}

static const struct wl_callback_listener frame_listener_g = {
	.done = frame_done,
};

static void output_request_redraw(struct ctl_output *o) {
	o->dirty = true;
	if (o->frame_cb) return;
	output_redraw(o);
}

static void output_redraw(struct ctl_output *o) {
	if (!o->configured || !o->buf_data) return;
	struct rec_controls *c = o->st;
	o->dirty = false;

	struct rect cur = {0, 0, 0, 0};
	struct rect b = bar_rect(c);
	int32_t ix, iy, iw, ih;
	if (grabit_output_rect_intersect(o->go, &b, &ix, &iy, &iw, &ih)) {
		cur = (struct rect){(ix - o->go->x) * o->scale,
							(iy - o->go->y) * o->scale,
							iw * o->scale, ih * o->scale};
	}
	if (cur.w == 0 && o->shown.w == 0 && o->mapped) return;

	if (cur.w > 0 || o->shown.w > 0) {
		cairo_surface_t *surf = grabit_cairo_image_argb(o->buf_data, o->pixel_w,
														o->pixel_h, o->pixel_w * 4);
		if (!surf) return;
		cairo_t *cr = cairo_create(surf);
		cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
		if (o->shown.w > 0) {
			cairo_rectangle(cr, o->shown.x, o->shown.y, o->shown.w, o->shown.h);
			cairo_fill(cr);
		}
		if (cur.w > 0) {
			cairo_rectangle(cr, cur.x, cur.y, cur.w, cur.h);
			cairo_fill(cr);
			cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
			cairo_scale(cr, o->scale, o->scale);
			cairo_translate(cr, c->bx - o->go->x, c->by - o->go->y);
			draw_bar(cr, c);
		}
		cairo_destroy(cr);
		cairo_surface_flush(surf);
		cairo_surface_destroy(surf);
	}

	o->frame_cb = wl_surface_frame(o->surface);
	wl_callback_add_listener(o->frame_cb, &frame_listener_g, o);
	wl_surface_attach(o->surface, o->buffer, 0, 0);
	if (o->shown.w > 0)
		wl_surface_damage_buffer(o->surface, o->shown.x, o->shown.y,
								 o->shown.w, o->shown.h);
	if (cur.w > 0)
		wl_surface_damage_buffer(o->surface, cur.x, cur.y, cur.w, cur.h);
	wl_surface_commit(o->surface);
	wl_display_flush(c->wls->display);
	o->shown = cur;
	o->mapped = true;
}

static void redraw_all(struct rec_controls *c) {
	for (size_t i = 0; i < c->n; i++)
		output_request_redraw(&c->outs[i]);
}

static void commit_input_regions(struct rec_controls *c) {
	for (size_t i = 0; i < c->n; i++) {
		struct ctl_output *o = &c->outs[i];
		if (!o->configured) continue;
		apply_input_region(o);
		wl_surface_commit(o->surface);
	}
	wl_display_flush(c->wls->display);
}

static struct ctl_output *find_by_surface(struct rec_controls *c,
										  struct wl_surface *s) {
	for (size_t i = 0; i < c->n; i++) {
		if (c->outs[i].surface == s) return &c->outs[i];
	}
	return NULL;
}

static void bar_move_to(struct rec_controls *c, int32_t x, int32_t y) {
	int32_t x_hi = c->bounds.x + c->bounds.w - c->bw;
	int32_t y_hi = c->bounds.y + c->bounds.h - c->bh;
	if (x > x_hi) x = x_hi;
	if (x < c->bounds.x) x = c->bounds.x;
	if (y > y_hi) y = y_hi;
	if (y < c->bounds.y) y = c->bounds.y;
	if (x == c->bx && y == c->by) return;
	c->bx = x;
	c->by = y;
	redraw_all(c);
}

static void pointer_enter(void *data, struct wl_pointer *p, uint32_t serial,
						  struct wl_surface *surface, wl_fixed_t sx, wl_fixed_t sy) {
	struct rec_controls *c = data;
	struct ctl_output *o = find_by_surface(c, surface);
	if (!o) return;
	c->ptr_on = o;
	c->cx = o->go->x + wl_fixed_to_int(sx);
	c->cy = o->go->y + wl_fixed_to_int(sy);
	grabit_cursor_apply(p, serial, c->cursor_surface, c->cursor_hand, o->scale);
}

static void pointer_leave(void *data, struct wl_pointer *p, uint32_t serial,
						  struct wl_surface *surface) {
	(void)p;
	(void)serial;
	struct rec_controls *c = data;
	if (c->ptr_on && c->ptr_on->surface == surface) c->ptr_on = NULL;
}

static void pointer_motion(void *data, struct wl_pointer *p, uint32_t time,
						   wl_fixed_t sx, wl_fixed_t sy) {
	(void)p;
	(void)time;
	struct rec_controls *c = data;
	if (!c->ptr_on) return;
	c->cx = c->ptr_on->go->x + wl_fixed_to_int(sx);
	c->cy = c->ptr_on->go->y + wl_fixed_to_int(sy);
	if (c->dragging)
		bar_move_to(c, c->cx - c->grab_dx, c->cy - c->grab_dy);
}

static void drag_end(struct rec_controls *c) {
	if (!c->dragging) return;
	c->dragging = false;
	commit_input_regions(c);
}

static void pointer_button(void *data, struct wl_pointer *p, uint32_t serial,
						   uint32_t time, uint32_t button, uint32_t state) {
	(void)p;
	(void)serial;
	(void)time;
	struct rec_controls *c = data;
	if (button != BTN_LEFT) return;
	if (state == WL_POINTER_BUTTON_STATE_RELEASED) {
		drag_end(c);
		return;
	}
	if (!c->ptr_on || !rect_contains(bar_rect(c), c->cx, c->cy)) return;
	switch (btn_at(c->cx - c->bx, c->cy - c->by)) {
	case CB_BTN_START:
		atomic_store(c->pause_flag, 0);
		break;
	case CB_BTN_PAUSE:
		atomic_store(c->pause_flag, 1);
		break;
	case CB_BTN_STOP:
		atomic_store(c->stop_flag, 1);
		break;
	default:
		c->dragging = true;
		c->grab_dx = c->cx - c->bx;
		c->grab_dy = c->cy - c->by;
		commit_input_regions(c);
		break;
	}
}

static void pointer_axis(void *data, struct wl_pointer *p, uint32_t time,
						 uint32_t axis, wl_fixed_t value) {
	(void)data;
	(void)p;
	(void)time;
	(void)axis;
	(void)value;
}

static const struct wl_pointer_listener pointer_listener_g = {
	.enter = pointer_enter,
	.leave = pointer_leave,
	.motion = pointer_motion,
	.button = pointer_button,
	.axis = pointer_axis,
};

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
	apply_input_region(o);
	output_redraw(o);
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

	int32_t w = bar_width(), h = CB_H;
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
		apply_input_region(o);
		wl_surface_commit(o->surface);
	}

	if (s->seat && (s->seat_caps & WL_SEAT_CAPABILITY_POINTER)) {
		c->pointer = wl_seat_get_pointer(s->seat);
		if (c->pointer)
			wl_pointer_add_listener(c->pointer, &pointer_listener_g, c);
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
	redraw_all(c);
}

void controls_tick(struct rec_controls *c, int64_t secs) {
	if (!c || c->secs == secs) return;
	c->secs = secs;
	redraw_all(c);
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
