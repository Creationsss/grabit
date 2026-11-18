// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "ui/window.h"

#include "ui/widgets.h"

#include "cairo_util.h"
#include "log.h"
#include "ui_theme.h"
#include "wl/wl.h"

#include <math.h>

#include <wayland-client.h>

#include "fractional-scale-v1-client-protocol.h"
#include "viewporter-client-protocol.h"
#include "xdg-shell-client-protocol.h"

static void ui_window_render(struct ui_window *w);

static double ui_window_scale(const struct ui_window *w) {
	return (double)w->scale120 / 120.0;
}

bool ui_window_snapped(const struct ui_window *w) {
	return w->maximized || w->fullscreen || w->tiled;
}

double ui_window_panel_radius(const struct ui_window *w) {
	return ui_window_snapped(w) ? 0 : grabit_ui_radius(GUI_R_PANEL);
}

void ui_window_damage(struct ui_window *w) {
	w->dirty = true;
}

static void frame_done(void *data, struct wl_callback *cb, uint32_t time) {
	(void)time;
	struct ui_window *w = data;
	if (w->frame_cb == cb) grabit_wl_callback_drop(&w->frame_cb);
	if (w->dirty) ui_window_render(w);
}

static const struct wl_callback_listener frame_listener_g = {
	.done = frame_done,
};

static void xdg_surface_configure(void *data, struct xdg_surface *xs, uint32_t serial) {
	struct ui_window *w = data;
	xdg_surface_ack_configure(xs, serial);
	bool snapped = ui_window_snapped(w);
	if (w->pending_w > 0)
		w->width = w->pending_w;
	else if (!snapped && w->float_w > 0)
		w->width = w->float_w;
	if (w->pending_h > 0)
		w->height = w->pending_h;
	else if (!snapped && w->float_h > 0)
		w->height = w->float_h;
	if (w->width < UI_MIN_W) w->width = UI_MIN_W;
	if (w->height < UI_MIN_H) w->height = UI_MIN_H;
	if (!snapped) {
		w->float_w = w->width;
		w->float_h = w->height;
	}
	w->configured = true;
	w->dirty = true;
	ui_window_render(w);
}

static const struct xdg_surface_listener xdg_surface_listener_g = {
	.configure = xdg_surface_configure,
};

static void toplevel_configure(void *data, struct xdg_toplevel *tl, int32_t width,
							   int32_t height, struct wl_array *states) {
	(void)tl;
	struct ui_window *w = data;
	w->pending_w = width;
	w->pending_h = height;
	w->activated = false;
	w->maximized = false;
	w->fullscreen = false;
	w->tiled = 0;
	if (!states) return;
	uint32_t *st;
	for (st = states->data;
		 (const char *)st < (const char *)states->data + states->size; st++) {
		switch (*st) {
		case XDG_TOPLEVEL_STATE_ACTIVATED:
			w->activated = true;
			break;
		case XDG_TOPLEVEL_STATE_MAXIMIZED:
			w->maximized = true;
			break;
		case XDG_TOPLEVEL_STATE_FULLSCREEN:
			w->fullscreen = true;
			break;
		case XDG_TOPLEVEL_STATE_TILED_LEFT:
			w->tiled |= UI_TILED_LEFT;
			break;
		case XDG_TOPLEVEL_STATE_TILED_RIGHT:
			w->tiled |= UI_TILED_RIGHT;
			break;
		case XDG_TOPLEVEL_STATE_TILED_TOP:
			w->tiled |= UI_TILED_TOP;
			break;
		case XDG_TOPLEVEL_STATE_TILED_BOTTOM:
			w->tiled |= UI_TILED_BOTTOM;
			break;
		default:
			break;
		}
	}
}

static void toplevel_close(void *data, struct xdg_toplevel *tl) {
	(void)tl;
	struct ui_window *w = data;
	w->closed = true;
}

static const struct xdg_toplevel_listener toplevel_listener_g = {
	.configure = toplevel_configure,
	.close = toplevel_close,
};

static void frac_preferred(void *data, struct wp_fractional_scale_v1 *f, uint32_t scale) {
	(void)f;
	struct ui_window *w = data;
	if (scale == 0 || (int32_t)scale == w->scale120) return;
	w->scale120 = (int32_t)scale;
	grabit_cursor_set_rescale(&w->cursor, w->wls, (w->scale120 + 119) / 120);
	w->dirty = true;
}

static const struct wp_fractional_scale_v1_listener frac_listener_g = {
	.preferred_scale = frac_preferred,
};

static void surface_enter(void *data, struct wl_surface *s, struct wl_output *output) {
	(void)s;
	struct ui_window *w = data;
	if (w->frac) return;
	for (size_t i = 0; i < w->wls->n_outputs; i++) {
		struct grabit_output *o = w->wls->outputs[i];
		if (o->wl_output != output) continue;
		int32_t want = (o->scale > 0 ? o->scale : 1) * 120;
		if (want == w->scale120) return;
		w->scale120 = want;
		grabit_cursor_set_rescale(&w->cursor, w->wls, want / 120);
		w->dirty = true;
		return;
	}
}

static void surface_leave(void *data, struct wl_surface *s, struct wl_output *output) {
	(void)data;
	(void)s;
	(void)output;
}

static const struct wl_surface_listener surface_listener_g = {
	.enter = surface_enter,
	.leave = surface_leave,
};

int ui_window_open(struct ui_window *w, struct grabit_wl_state *s,
				   const char *title, int32_t width, int32_t height) {
	if (!s->wm_base) {
		log_error("ui: compositor lacks xdg_wm_base; cannot open a window");
		return -1;
	}

	w->wls = s;
	w->title = title;
	w->width = width < UI_MIN_W ? UI_MIN_W : width;
	w->height = height < UI_MIN_H ? UI_MIN_H : height;
	w->scale120 = 120;

	w->surface = wl_compositor_create_surface(s->compositor);
	if (!w->surface) {
		log_error("ui: wl_compositor.create_surface failed");
		return -1;
	}
	wl_surface_add_listener(w->surface, &surface_listener_g, w);

	if (s->fractional_scale_manager && s->viewporter) {
		w->frac = wp_fractional_scale_manager_v1_get_fractional_scale(
			s->fractional_scale_manager, w->surface);
		wp_fractional_scale_v1_add_listener(w->frac, &frac_listener_g, w);
		w->viewport = wp_viewporter_get_viewport(s->viewporter, w->surface);
	}

	w->xdg_surface = xdg_wm_base_get_xdg_surface(s->wm_base, w->surface);
	xdg_surface_add_listener(w->xdg_surface, &xdg_surface_listener_g, w);
	w->toplevel = xdg_surface_get_toplevel(w->xdg_surface);
	xdg_toplevel_add_listener(w->toplevel, &toplevel_listener_g, w);
	xdg_toplevel_set_title(w->toplevel, title);
	xdg_toplevel_set_app_id(w->toplevel, "grabit");
	xdg_toplevel_set_min_size(w->toplevel, UI_MIN_W, UI_MIN_H);

	wl_surface_commit(w->surface);
	if (wl_display_roundtrip(s->display) < 0) {
		log_error("ui: lost the wayland connection while mapping the window");
		return -1;
	}
	ui_input_attach(w);
	ui_dnd_attach(w);
	return 0;
}

static void ui_window_render(struct ui_window *w) {
	if (!w->configured || w->closed) return;
	if (w->frame_cb) {
		w->dirty = true;
		return;
	}

	double scale = ui_window_scale(w);
	int32_t pw = (int32_t)lround(w->width * scale);
	int32_t ph = (int32_t)lround(w->height * scale);
	if (pw <= 0 || ph <= 0) return;

	struct grabit_shm_slot *slot =
		grabit_shm_pool_next(w->wls->shm, "grabit-ui", &w->pool, pw, ph);
	if (!slot) {
		w->dirty = true;
		return;
	}
	cairo_surface_t *dst = grabit_cairo_image_argb(slot->buf.map, pw, ph, pw * 4);
	if (!dst) return;

	cairo_t *cr = cairo_create(dst);
	ui_font_options(cr);
	cairo_scale(cr, scale, scale);
	ui_frame_draw(w, cr);
	if (w->on_draw) {
		double ch = w->height - UI_TITLE_H;
		cairo_save(cr);
		grabit_cairo_rect_r(cr, 0, 0, w->width, w->height, ui_window_panel_radius(w));
		cairo_clip(cr);
		cairo_translate(cr, 0, UI_TITLE_H);
		cairo_rectangle(cr, 0, 0, w->width, ch);
		cairo_clip(cr);
		w->on_draw(w, cr, w->width, ch);
		cairo_restore(cr);
	}
	cairo_destroy(cr);
	cairo_surface_flush(dst);
	cairo_surface_destroy(dst);

	w->frame_cb = wl_surface_frame(w->surface);
	wl_callback_add_listener(w->frame_cb, &frame_listener_g, w);

	if (w->viewport) {
		wl_surface_set_buffer_scale(w->surface, 1);
		wp_viewport_set_destination(w->viewport, w->width, w->height);
	} else {
		wl_surface_set_buffer_scale(w->surface, w->scale120 / 120);
	}
	int32_t radius = (int32_t)ui_window_panel_radius(w);
	if (w->opaque_w != w->width || w->opaque_h != w->height || w->opaque_r != radius) {
		struct wl_region *opaque = wl_compositor_create_region(w->wls->compositor);
		if (opaque) {
			grabit_wl_region_add_rounded(opaque, 0, 0, w->width, w->height, radius);
			wl_surface_set_opaque_region(w->surface, opaque);
			wl_region_destroy(opaque);
			w->opaque_w = w->width;
			w->opaque_h = w->height;
			w->opaque_r = radius;
		}
	}
	grabit_shm_slot_attach(w->surface, slot);
	wl_surface_damage_buffer(w->surface, 0, 0, pw, ph);
	wl_surface_commit(w->surface);
	w->dirty = false;
}

int ui_window_run(struct ui_window *w) {
	while (!w->closed) {
		if (w->dirty) ui_window_render(w);
		if (grabit_wl_pump(w->wls, ui_input_timeout(w)) != 0) {
			log_warn("ui: lost the wayland connection");
			return -1;
		}
		ui_input_fire_timers(w);
	}
	return 0;
}

void ui_window_finish(struct ui_window *w) {
	ui_dnd_detach(w);
	ui_input_detach(w);
	grabit_wl_callback_drop(&w->frame_cb);
	grabit_shm_pool_finish(&w->pool);
	if (w->frac) wp_fractional_scale_v1_destroy(w->frac);
	if (w->viewport) wp_viewport_destroy(w->viewport);
	if (w->toplevel) xdg_toplevel_destroy(w->toplevel);
	if (w->xdg_surface) xdg_surface_destroy(w->xdg_surface);
	if (w->surface) wl_surface_destroy(w->surface);
	w->frac = NULL;
	w->viewport = NULL;
	w->toplevel = NULL;
	w->xdg_surface = NULL;
	w->surface = NULL;
}
