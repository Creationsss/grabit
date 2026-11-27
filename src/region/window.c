// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "region/window_internal.h"

#include "log.h"
#include "region/input_internal.h"
#include "region/keybinds.h"
#include "region/region.h"
#include "region/render_internal.h"
#include "region/wlr_input_state.h"

#include <math.h>
#include <stdlib.h>

#include <linux/input-event-codes.h>

#include <wayland-client.h>

#include "xdg-shell-client-protocol.h"

#define TITLE_DOUBLE_CLICK_MS 400

static struct ro_output *win_out(const struct ro_state *st) {
	return st->n_outs > 0 ? &st->outs[0] : NULL;
}

static bool win_snapped(const struct ro_window *w) {
	return w->frame.maximized || w->frame.fullscreen || w->frame.tiled;
}

static void win_relayout(struct ro_state *st, int32_t w, int32_t h) {
	struct ro_window *win = st->win;
	win->go.logical_width = w;
	win->go.logical_height = h;
	win->go.width = w * (win->go.scale > 0 ? win->go.scale : 1);
	win->go.height = h * (win->go.scale > 0 ? win->go.scale : 1);
	win->frame.width = w;
	win->frame.height = h;
	st->ui_bounds = (struct rect){0, UI_TITLE_H, w, h - UI_TITLE_H};
	struct ro_output *o = win_out(st);
	if (!o) return;
	o->width = w;
	o->height = h;
}

static void xdg_surface_configure(void *data, struct xdg_surface *xs, uint32_t serial) {
	struct ro_state *st = data;
	struct ro_window *win = st->win;
	if (st->cleanup) return;
	xdg_surface_ack_configure(xs, serial);
	bool snapped = win_snapped(win);
	int32_t w = win->frame.width, h = win->frame.height;
	if (win->pending_w > 0)
		w = win->pending_w;
	else if (!snapped && win->float_w > 0)
		w = win->float_w;
	if (win->pending_h > 0)
		h = win->pending_h;
	else if (!snapped && win->float_h > 0)
		h = win->float_h;
	if (!snapped) {
		if (w < win->min_w) w = win->min_w;
		if (h < win->min_h) h = win->min_h;
		win->float_w = w;
		win->float_h = h;
	}
	win_relayout(st, w, h);
	struct ro_output *o = &st->outs[0];
	gren_output_configure(o);
	if (st->cursor_seen) region_window_cursor_from_ui(st);
	o->configured = true;
	gren_output_redraw(o);
}

static const struct xdg_surface_listener xdg_surface_listener_g = {
	.configure = xdg_surface_configure,
};

static void toplevel_configure(void *data, struct xdg_toplevel *tl, int32_t width,
							   int32_t height, struct wl_array *states) {
	(void)tl;
	struct ro_state *st = data;
	struct ro_window *win = st->win;
	win->pending_w = width;
	win->pending_h = height;
	win->frame.activated = false;
	win->frame.maximized = false;
	win->frame.fullscreen = false;
	win->frame.tiled = 0;
	if (!states) return;
	uint32_t *s;
	for (s = states->data; (const char *)s < (const char *)states->data + states->size; s++) {
		switch (*s) {
		case XDG_TOPLEVEL_STATE_ACTIVATED:
			win->frame.activated = true;
			break;
		case XDG_TOPLEVEL_STATE_MAXIMIZED:
			win->frame.maximized = true;
			break;
		case XDG_TOPLEVEL_STATE_FULLSCREEN:
			win->frame.fullscreen = true;
			break;
		case XDG_TOPLEVEL_STATE_TILED_LEFT:
			win->frame.tiled |= UI_TILED_LEFT;
			break;
		case XDG_TOPLEVEL_STATE_TILED_RIGHT:
			win->frame.tiled |= UI_TILED_RIGHT;
			break;
		case XDG_TOPLEVEL_STATE_TILED_TOP:
			win->frame.tiled |= UI_TILED_TOP;
			break;
		case XDG_TOPLEVEL_STATE_TILED_BOTTOM:
			win->frame.tiled |= UI_TILED_BOTTOM;
			break;
		default:
			break;
		}
	}
}

static void toplevel_close(void *data, struct xdg_toplevel *tl) {
	(void)tl;
	struct ro_state *st = data;
	if (st->cleanup) return;
	ginp_request_cancel(st);
	region_render_request_redraw_all(st);
}

static const struct xdg_toplevel_listener toplevel_listener_g = {
	.configure = toplevel_configure,
	.close = toplevel_close,
};

static void apply_scale(struct ro_state *st) {
	struct ro_window *win = st->win;
	int32_t scale = 0;
	for (size_t k = 0; k < win->n_entered; k++)
		for (size_t i = 0; i < st->wls->n_outputs; i++) {
			struct grabit_output *go = st->wls->outputs[i];
			if (go->wl_output != win->entered[k]) continue;
			int32_t s = go->scale > 0 ? go->scale : 1;
			if (s > scale) scale = s;
		}
	if (scale <= 0 || scale == win->go.scale) return;
	win->go.scale = scale;
	struct ro_output *o = win_out(st);
	if (!o || !o->configured) return;
	win_relayout(st, o->width, o->height);
	gren_output_configure(o);
	if (st->cursor_seen) region_window_cursor_from_ui(st);
	region_render_request_redraw_all(st);
}

static void surface_enter(void *data, struct wl_surface *surface, struct wl_output *output) {
	(void)surface;
	struct ro_state *st = data;
	if (st->cleanup) return;
	struct ro_window *win = st->win;
	for (size_t k = 0; k < win->n_entered; k++)
		if (win->entered[k] == output) return;
	if (win->n_entered < RWIN_MAX_ENTERED) win->entered[win->n_entered++] = output;
	apply_scale(st);
}

static void surface_leave(void *data, struct wl_surface *surface, struct wl_output *output) {
	(void)surface;
	struct ro_state *st = data;
	if (st->cleanup) return;
	struct ro_window *win = st->win;
	for (size_t k = 0; k < win->n_entered; k++) {
		if (win->entered[k] != output) continue;
		win->entered[k] = win->entered[--win->n_entered];
		break;
	}
	apply_scale(st);
}

static const struct wl_surface_listener surface_listener_g = {
	.enter = surface_enter,
	.leave = surface_leave,
};

static void initial_size(const struct ro_state *st, const struct rect *area,
						 int32_t *out_w, int32_t *out_h, int32_t *min_w, int32_t *min_h) {
	int32_t tw, th;
	region_toolbar_size(&tw, &th);
	*min_w = tw + 2 * TB_GAP > UI_MIN_W ? tw + 2 * TB_GAP : UI_MIN_W;
	*min_h = UI_MIN_H;
	int32_t top = UI_TITLE_H + 2 * TB_GAP + th;
	int32_t want_w = area->w + 2 * RWIN_PAD;
	int32_t want_h = area->h + top + RWIN_PAD;
	int32_t max_w = 0, max_h = 0;
	for (size_t i = 0; i < st->wls->n_outputs; i++) {
		const struct grabit_output *o = st->wls->outputs[i];
		if (o->logical_width > max_w) max_w = o->logical_width;
		if (o->logical_height > max_h) max_h = o->logical_height;
	}
	if (max_w > 0 && want_w > max_w * 85 / 100) want_w = max_w * 85 / 100;
	if (max_h > 0 && want_h > max_h * 85 / 100) want_h = max_h * 85 / 100;
	*out_w = want_w < *min_w ? *min_w : want_w;
	*out_h = want_h < *min_h ? *min_h : want_h;
}

int region_window_create(struct ro_state *st, struct grabit_wl_state *s,
						 const struct region_window_req *req) {
	if (!s->wm_base) {
		log_error("edit: compositor lacks xdg_wm_base; cannot open the editor window");
		return -1;
	}
	struct ro_window *win = calloc(1, sizeof *win);
	if (!win) return -1;
	st->win = win;

	int32_t max_scale = 1;
	for (size_t i = 0; i < s->n_outputs; i++)
		if (s->outputs[i]->scale > max_scale) max_scale = s->outputs[i]->scale;
	const struct grabit_output *prim = grabit_wl_primary_output(s);
	win->go.state = s;
	win->go.name = "window";
	win->go.scale = prim && prim->scale > 0 ? prim->scale : max_scale;
	win->frame.title = req->title ? req->title : "grabit";
	win->frame.activated = true;
	win->zoom = 1.0;
	win->fit = true;
	win->pan_x = req->area.x + req->area.w / 2.0;
	win->pan_y = req->area.y + req->area.h / 2.0;

	int32_t w, h;
	initial_size(st, &req->area, &w, &h, &win->min_w, &win->min_h);
	win->float_w = w;
	win->float_h = h;
	win_relayout(st, w, h);

	struct ro_output *o = &st->outs[0];
	o->st = st;
	o->go = &win->go;
	o->idx = 0;
	o->area = req->area;
	o->surface = wl_compositor_create_surface(s->compositor);
	if (!o->surface) {
		log_error("edit: wl_compositor.create_surface failed");
		return -1;
	}
	wl_surface_add_listener(o->surface, &surface_listener_g, st);
	win->xdg_surface = xdg_wm_base_get_xdg_surface(s->wm_base, o->surface);
	xdg_surface_add_listener(win->xdg_surface, &xdg_surface_listener_g, st);
	win->toplevel = xdg_surface_get_toplevel(win->xdg_surface);
	xdg_toplevel_add_listener(win->toplevel, &toplevel_listener_g, st);
	xdg_toplevel_set_title(win->toplevel, win->frame.title);
	xdg_toplevel_set_app_id(win->toplevel, "grabit");
	xdg_toplevel_set_min_size(win->toplevel, win->min_w, win->min_h);
	wl_surface_commit(o->surface);
	return 0;
}

void region_window_destroy(struct ro_state *st) {
	struct ro_window *win = st->win;
	if (!win) return;
	if (win->toplevel) xdg_toplevel_destroy(win->toplevel);
	if (win->xdg_surface) xdg_surface_destroy(win->xdg_surface);
	free(win);
	st->win = NULL;
}

void region_window_canvas(const struct ro_state *st, struct rect *out) {
	const struct ro_window *win = st->win;
	int32_t tw, th;
	region_toolbar_size(&tw, &th);
	int32_t top = UI_TITLE_H + 2 * TB_GAP + th;
	int32_t w = win->frame.width - 2 * RWIN_PAD;
	int32_t h = win->frame.height - top - RWIN_PAD;
	*out = (struct rect){RWIN_PAD, top, w > 1 ? w : 1, h > 1 ? h : 1};
}

static double fit_zoom(const struct ro_state *st, const struct rect *area) {
	struct rect c;
	region_window_canvas(st, &c);
	if (area->w <= 0 || area->h <= 0) return 1.0;
	double z = fmin((double)c.w / area->w, (double)c.h / area->h);
	if (z > 1.0) z = 1.0;
	return z < RWIN_ZOOM_MIN ? RWIN_ZOOM_MIN : z;
}

static double clamp_pan(double pan, int32_t lo, int32_t len, double half) {
	if (len <= 2.0 * half) return lo + len / 2.0;
	if (pan < lo + half) return lo + half;
	if (pan > lo + len - half) return lo + len - half;
	return pan;
}

void region_window_view(struct ro_output *o) {
	struct ro_state *st = o->st;
	struct ro_window *win = st->win;
	if (!win) return;
	struct rect c;
	region_window_canvas(st, &c);
	if (win->fit) win->zoom = fit_zoom(st, &o->area);
	double z = win->zoom;
	win->pan_x = clamp_pan(win->pan_x, o->area.x, o->area.w, c.w / (2.0 * z));
	win->pan_y = clamp_pan(win->pan_y, o->area.y, o->area.h, c.h / (2.0 * z));
	double ccx = c.x + c.w / 2.0, ccy = c.y + c.h / 2.0;
	o->vs = z * o->scale;
	o->vx = win->pan_x - ccx / z;
	o->vy = win->pan_y - ccy / z;
}

void region_window_cursor_from_ui(struct ro_state *st) {
	struct ro_output *o = win_out(st);
	if (!st->win || !o || o->vs <= 0) return;
	st->cursor_x = (int32_t)floor(o->vx + st->ui_fx * o->scale / o->vs);
	st->cursor_y = (int32_t)floor(o->vy + st->ui_fy * o->scale / o->vs);
}

double region_view_zoom(const struct ro_state *st) {
	return st->win ? st->win->zoom : 1.0;
}

static void view_changed(struct ro_state *st) {
	struct ro_output *o = win_out(st);
	if (!o) return;
	region_window_view(o);
	if (st->cursor_seen) region_window_cursor_from_ui(st);
	ginp_refresh_cursor(st);
	region_render_request_redraw_all(st);
}

void region_window_zoom_to(struct ro_state *st, double zoom, int32_t ux, int32_t uy) {
	struct ro_window *win = st->win;
	if (!win) return;
	struct rect c;
	region_window_canvas(st, &c);
	if (ux < c.x || uy < c.y || ux >= c.x + c.w || uy >= c.y + c.h) {
		ux = c.x + c.w / 2;
		uy = c.y + c.h / 2;
	}
	double ccx = c.x + c.w / 2.0, ccy = c.y + c.h / 2.0;
	double z = win->zoom;
	double gx = (ux - ccx) / z + win->pan_x;
	double gy = (uy - ccy) / z + win->pan_y;
	if (zoom < RWIN_ZOOM_MIN) zoom = RWIN_ZOOM_MIN;
	if (zoom > RWIN_ZOOM_MAX) zoom = RWIN_ZOOM_MAX;
	win->zoom = zoom;
	win->fit = false;
	win->pan_x = gx - (ux - ccx) / zoom;
	win->pan_y = gy - (uy - ccy) / zoom;
	view_changed(st);
}

void region_window_zoom_at(struct ro_state *st, double factor, int32_t ux, int32_t uy) {
	if (!st->win) return;
	region_window_zoom_to(st, st->win->zoom * factor, ux, uy);
}

void region_window_fit(struct ro_state *st) {
	if (!st->win) return;
	st->win->fit = true;
	view_changed(st);
}

static bool pointer_busy(const struct ro_state *st) {
	return st->dragging || st->drawing || st->tb_dragging || st->slider_dragging ||
		   st->color_picker_dragging || st->moving_region ||
		   st->handle_dragging != HANDLE_NONE || region_anno_dragging(st);
}

static enum ui_hit frame_hit(const struct ro_state *st) {
	if (!st->cursor_on || pointer_busy(st)) return UI_HIT_CONTENT;
	enum ui_hit hit = ui_frame_hit(&st->win->frame, st->ui_x, st->ui_y);
	return hit == UI_HIT_NONE ? UI_HIT_CONTENT : hit;
}

bool region_window_motion(struct ro_state *st) {
	struct ro_window *win = st->win;
	if (!win) return false;
	if (win->panning) {
		double z = win->zoom;
		win->pan_x -= (st->ui_x - win->pan_ux) / z;
		win->pan_y -= (st->ui_y - win->pan_uy) / z;
		win->pan_ux = st->ui_x;
		win->pan_uy = st->ui_y;
		view_changed(st);
		return true;
	}
	enum ui_hit hit = frame_hit(st);
	win->frame.hover = hit;
	if (hit == UI_HIT_CONTENT) return false;
	st->pressed_button = -1;
	region_set_hover(st, -1);
	return true;
}

static uint32_t resize_edge(enum ui_hit hit) {
	switch (hit) {
	case UI_HIT_N:
		return XDG_TOPLEVEL_RESIZE_EDGE_TOP;
	case UI_HIT_S:
		return XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM;
	case UI_HIT_E:
		return XDG_TOPLEVEL_RESIZE_EDGE_RIGHT;
	case UI_HIT_W:
		return XDG_TOPLEVEL_RESIZE_EDGE_LEFT;
	case UI_HIT_NE:
		return XDG_TOPLEVEL_RESIZE_EDGE_TOP_RIGHT;
	case UI_HIT_NW:
		return XDG_TOPLEVEL_RESIZE_EDGE_TOP_LEFT;
	case UI_HIT_SE:
		return XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT;
	case UI_HIT_SW:
		return XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_LEFT;
	default:
		return XDG_TOPLEVEL_RESIZE_EDGE_NONE;
	}
}

static bool middle_pans(const struct ro_state *st, uint32_t button) {
	return button == BTN_MIDDLE && !region_button_action(&st->keys, KA_CANCEL, button) &&
		   !region_button_action(&st->keys, KA_CONFIRM, button);
}

bool region_window_button(struct ro_state *st, uint32_t serial, uint32_t time,
						  uint32_t button, uint32_t state) {
	struct ro_window *win = st->win;
	if (!win) return false;
	bool pressed = state == WL_POINTER_BUTTON_STATE_PRESSED;
	if (win->panning) {
		if (!pressed && button == win->pan_button) {
			win->panning = false;
			ginp_refresh_cursor(st);
		}
		return true;
	}
	enum ui_hit hit = frame_hit(st);
	if (!pressed) {
		if (button == BTN_LEFT && win->frame.close_armed) {
			win->frame.close_armed = false;
			if (hit == UI_HIT_CLOSE) ginp_request_cancel(st);
			region_render_request_redraw_all(st);
			return true;
		}
		return false;
	}
	if (hit != UI_HIT_CONTENT) {
		if (button != BTN_LEFT) return true;
		if (hit != UI_HIT_CLOSE) st->discard_armed = false;
		if (hit == UI_HIT_CLOSE) {
			win->frame.close_armed = true;
		} else if (hit == UI_HIT_TITLE) {
			if (win->title_click_ms && time - win->title_click_ms <= TITLE_DOUBLE_CLICK_MS) {
				win->title_click_ms = 0;
				if (win->frame.maximized)
					xdg_toplevel_unset_maximized(win->toplevel);
				else
					xdg_toplevel_set_maximized(win->toplevel);
			} else {
				win->title_click_ms = time;
				win->grabbed = true;
				xdg_toplevel_move(win->toplevel, st->wls->seat, serial);
			}
		} else {
			uint32_t edge = resize_edge(hit);
			if (edge != XDG_TOPLEVEL_RESIZE_EDGE_NONE) {
				win->grabbed = true;
				xdg_toplevel_resize(win->toplevel, st->wls->seat, serial, edge);
			}
		}
		region_render_request_redraw_all(st);
		return true;
	}
	if (middle_pans(st, button) && !pointer_busy(st)) {
		win->panning = true;
		win->pan_button = button;
		win->pan_ux = st->ui_x;
		win->pan_uy = st->ui_y;
		ginp_refresh_cursor(st);
		return true;
	}
	return false;
}

int region_window_cursor(const struct ro_state *st) {
	const struct ro_window *win = st->win;
	if (!win) return -1;
	if (win->panning) return RCUR_MOVE;
	switch (frame_hit(st)) {
	case UI_HIT_CONTENT:
		return -1;
	case UI_HIT_N:
		return RCUR_RESIZE0 + HANDLE_N;
	case UI_HIT_S:
		return RCUR_RESIZE0 + HANDLE_S;
	case UI_HIT_E:
		return RCUR_RESIZE0 + HANDLE_E;
	case UI_HIT_W:
		return RCUR_RESIZE0 + HANDLE_W;
	case UI_HIT_NE:
		return RCUR_RESIZE0 + HANDLE_NE;
	case UI_HIT_NW:
		return RCUR_RESIZE0 + HANDLE_NW;
	case UI_HIT_SE:
		return RCUR_RESIZE0 + HANDLE_SE;
	case UI_HIT_SW:
		return RCUR_RESIZE0 + HANDLE_SW;
	default:
		return RCUR_DEFAULT;
	}
}

void region_window_draw_frame(cairo_t *cr, struct ro_output *o) {
	struct ro_window *win = o->st->win;
	cairo_save(cr);
	cairo_scale(cr, o->scale, o->scale);
	ui_frame_draw(&win->frame, cr);
	cairo_restore(cr);
}
