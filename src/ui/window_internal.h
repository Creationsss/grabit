// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_UI_WINDOW_INTERNAL_H
#define GRABIT_UI_WINDOW_INTERNAL_H

#include "ui/window.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <cairo/cairo.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

struct grabit_wl_state;
struct grabit_output;
struct wl_cursor_theme;
struct wl_cursor;
struct xdg_surface;
struct xdg_toplevel;

struct ui_window {
	struct grabit_wl_state *wls;
	struct grabit_output *output;
	void *user;
	void (*on_draw)(cairo_t *, int32_t, int32_t, void *);
	void (*on_pointer)(struct ui_window *, const struct ui_pointer_event *, void *);
	void (*on_key)(struct ui_window *, const struct ui_key_event *, void *);

	int32_t logical_w, logical_h;
	int32_t scale;
	int32_t pixel_w, pixel_h, stride;

	struct wl_surface *surface;
	struct xdg_surface *xdg_surface;
	struct xdg_toplevel *xdg_toplevel;
	struct wl_buffer *buffer;
	void *buf_data;
	size_t buf_size;
	cairo_surface_t *cairo_surf;

	bool configured;
	bool dirty;
	bool finished;
	bool lost;
	struct wl_callback *frame_cb;

	int watch_fd;
	void (*on_watch)(struct ui_window *, void *);
	void *watch_user;

	struct wl_pointer *pointer;
	struct wl_keyboard *keyboard;
	int32_t pointer_x, pointer_y;

	struct wl_cursor_theme *cursor_theme;
	struct wl_cursor *cursor_default, *cursor_hand, *cursor_text;
	struct wl_cursor *cursor_active;
	struct wl_surface *cursor_surface;
	uint32_t enter_serial;

	struct xkb_context *xkb_ctx;
	struct xkb_keymap *xkb_keymap;
	struct xkb_state *xkb_state;
	bool ctrl_held, shift_held;
};

void uiw_redraw(struct ui_window *w);
int uiw_alloc_buffer(struct ui_window *w);
void uiw_free_buffer(struct ui_window *w);
void uiw_apply_cursor(struct ui_window *w);
void uiw_input_attach(struct ui_window *w);

#endif
