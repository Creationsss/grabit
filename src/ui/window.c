// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "ui/window_internal.h"

#include "cursor.h"
#include "log.h"
#include "wl.h"

#include <poll.h>
#include <stdlib.h>

#include <wayland-client.h>
#include <wayland-cursor.h>

#include "xdg-shell-client-protocol.h"

static void xdg_surface_configure(void *data, struct xdg_surface *xs, uint32_t serial) {
	struct ui_window *w = data;
	xdg_surface_ack_configure(xs, serial);
	uiw_free_buffer(w);
	if (uiw_alloc_buffer(w) != 0) {
		w->finished = true;
		w->lost = true;
		return;
	}
	w->configured = true;
	uiw_redraw(w);
}

static const struct xdg_surface_listener xdg_surface_listener_g = {
	.configure = xdg_surface_configure,
};

static void xdg_toplevel_configure(void *data, struct xdg_toplevel *tl, int32_t cw,
								   int32_t ch, struct wl_array *states) {
	(void)tl;
	(void)states;
	struct ui_window *w = data;
	if (cw > 0) w->logical_w = cw;
	if (ch > 0) w->logical_h = ch;
}

static void xdg_toplevel_close(void *data, struct xdg_toplevel *tl) {
	(void)tl;
	((struct ui_window *)data)->finished = true;
}

static const struct xdg_toplevel_listener xdg_toplevel_listener_g = {
	.configure = xdg_toplevel_configure,
	.close = xdg_toplevel_close,
};

struct ui_window *ui_window_create(const struct ui_window_opts *opts) {
	if (!opts || !opts->wls || !opts->wls->compositor) {
		log_error("ui: no compositor");
		return NULL;
	}
	if (opts->width <= 0 || opts->height <= 0) {
		log_error("ui: window needs a positive size");
		return NULL;
	}

	struct ui_window *w = calloc(1, sizeof *w);
	if (!w) return NULL;
	w->watch_fd = -1;
	w->wls = opts->wls;
	w->output = opts->output ? opts->output : grabit_wl_primary_output(opts->wls);
	w->user = opts->user;
	w->on_draw = opts->on_draw;
	w->on_pointer = opts->on_pointer;
	w->on_key = opts->on_key;
	w->logical_w = opts->width;
	w->logical_h = opts->height;
	w->scale = w->output && w->output->scale > 0 ? w->output->scale : 1;

	w->xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if (!w->xkb_ctx) {
		log_error("ui: xkb_context_new failed");
		free(w);
		return NULL;
	}

	if (!w->wls->xdg_wm_base) {
		log_error("ui: compositor lacks xdg_wm_base");
		goto fail;
	}
	w->surface = wl_compositor_create_surface(w->wls->compositor);
	if (!w->surface) goto fail;
	w->xdg_surface = xdg_wm_base_get_xdg_surface(w->wls->xdg_wm_base, w->surface);
	if (!w->xdg_surface) goto fail;
	xdg_surface_add_listener(w->xdg_surface, &xdg_surface_listener_g, w);
	w->xdg_toplevel = xdg_surface_get_toplevel(w->xdg_surface);
	if (!w->xdg_toplevel) goto fail;
	xdg_toplevel_add_listener(w->xdg_toplevel, &xdg_toplevel_listener_g, w);
	xdg_toplevel_set_title(w->xdg_toplevel, opts->title ? opts->title : "grabit");
	xdg_toplevel_set_app_id(w->xdg_toplevel, opts->name ? opts->name : "grabit");
	xdg_toplevel_set_min_size(w->xdg_toplevel, w->logical_w, w->logical_h);
	xdg_toplevel_set_max_size(w->xdg_toplevel, w->logical_w, w->logical_h);

	w->cursor_theme = grabit_cursor_theme_load(w->wls->shm, w->scale);
	if (w->cursor_theme) {
		static const char *const def[] = {"left_ptr", "default", "arrow", NULL};
		static const char *const hand[] = {"pointer", "hand2", "pointing_hand", "hand", NULL};
		static const char *const text[] = {"text", "xterm", "ibeam", NULL};
		w->cursor_default = grabit_cursor_load_first(w->cursor_theme, def);
		w->cursor_hand = grabit_cursor_load_first(w->cursor_theme, hand);
		w->cursor_text = grabit_cursor_load_first(w->cursor_theme, text);
		w->cursor_surface = wl_compositor_create_surface(w->wls->compositor);
	}

	if (w->wls->seat_caps & WL_SEAT_CAPABILITY_POINTER)
		w->pointer = wl_seat_get_pointer(w->wls->seat);
	if (w->wls->seat_caps & WL_SEAT_CAPABILITY_KEYBOARD)
		w->keyboard = wl_seat_get_keyboard(w->wls->seat);
	uiw_input_attach(w);

	wl_surface_commit(w->surface);
	return w;

fail:
	ui_window_destroy(w);
	return NULL;
}

void ui_window_close(struct ui_window *w) {
	if (w) w->finished = true;
}

void ui_window_watch_fd(struct ui_window *w, int fd,
						void (*on_ready)(struct ui_window *, void *), void *user) {
	if (!w) return;
	w->watch_fd = fd;
	w->on_watch = on_ready;
	w->watch_user = user;
}

int ui_window_run(struct ui_window *w) {
	if (!w) return -1;
	struct wl_display *dpy = w->wls->display;
	while (!w->finished) {
		struct pollfd pfds[2];
		pfds[0].fd = wl_display_get_fd(dpy);
		pfds[0].events = POLLIN;
		size_t nfds = 1;
		int watch_idx = -1;
		if (w->watch_fd >= 0) {
			pfds[nfds].fd = w->watch_fd;
			pfds[nfds].events = POLLIN;
			watch_idx = (int)nfds++;
		}
		enum grabit_wl_pump r = grabit_wl_pump(dpy, pfds, nfds, &w->finished);
		if (r == GRABIT_WL_PUMP_FATAL) {
			w->lost = true;
			break;
		}
		if (r != GRABIT_WL_PUMP_OK) continue;
		if (watch_idx >= 0 && (pfds[watch_idx].revents & (POLLIN | POLLHUP)) && w->on_watch)
			w->on_watch(w, w->watch_user);
	}
	return w->lost ? -1 : 0;
}

void ui_window_destroy(struct ui_window *w) {
	if (!w) return;
	w->finished = true;
	uiw_free_buffer(w);
	if (w->pointer) wl_pointer_release(w->pointer);
	if (w->keyboard) wl_keyboard_release(w->keyboard);
	if (w->cursor_surface) wl_surface_destroy(w->cursor_surface);
	if (w->cursor_theme) wl_cursor_theme_destroy(w->cursor_theme);
	if (w->xdg_toplevel) xdg_toplevel_destroy(w->xdg_toplevel);
	if (w->xdg_surface) xdg_surface_destroy(w->xdg_surface);
	if (w->surface) wl_surface_destroy(w->surface);
	if (w->xkb_state) xkb_state_unref(w->xkb_state);
	if (w->xkb_keymap) xkb_keymap_unref(w->xkb_keymap);
	if (w->xkb_ctx) xkb_context_unref(w->xkb_ctx);
	if (w->wls && w->wls->display) wl_display_roundtrip(w->wls->display);
	free(w);
}
