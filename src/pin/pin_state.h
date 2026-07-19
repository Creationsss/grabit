// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_PIN_STATE_H
#define GRABIT_PIN_STATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <cairo/cairo.h>
#include <wayland-client.h>

#include "region/region.h"

struct grabit_output;
struct grabit_wl_state;
struct zwlr_layer_surface_v1;
struct wl_cursor;
struct wl_cursor_theme;
struct pin_state;

struct pin_output {
	struct pin_state *st;
	struct grabit_output *go;
	struct wl_surface *surface;
	struct zwlr_layer_surface_v1 *layer;
	struct wl_buffer *buffer;
	void *buf_data;
	size_t buf_size;
	cairo_surface_t *dst;
	int32_t width;
	int32_t height;
	int32_t pixel_w;
	int32_t pixel_h;
	int32_t scale;
	bool configured;
	bool dirty;
	struct rect shown;
	struct wl_callback *frame_cb;
};

struct pin_state {
	struct grabit_wl_state *wls;

	struct pin_output *outs;
	size_t n;
	struct rect bounds;

	cairo_surface_t *image;
	int32_t img_w;
	int32_t img_h;

	struct wl_pointer *pointer;

	int32_t px;
	int32_t py;
	int32_t width;
	int32_t height;

	bool input_grabbed;
	bool clickable;
	bool finished;

	struct pin_output *ptr_on;
	int32_t cx;
	int32_t cy;
	bool dragging;
	int32_t grab_dx;
	int32_t grab_dy;

	struct wl_cursor_theme *cursor_theme;
	struct wl_surface *cursor_surface;
	struct wl_cursor *cursor_hand;
	struct wl_cursor *cursor_move;
	struct wl_cursor *cursor_grabbing;
	struct wl_cursor *current_cursor;
	uint32_t last_pointer_serial;
	int32_t cursor_scale;

	int ipc_fd;
	char ipc_path[256];

	bool transient;
	int dismiss_timer_fd;
	int dismiss_secs;

	const char *hover_caption;
	bool hover_active;
	const char *click_open;

	char caption_fit[256];
	double caption_fit_x_advance;
	int32_t caption_fit_width;
};

#define PIN_CLOSE_BTN_SIZE 24
#define PIN_CLOSE_BTN_INSET 4
#define PIN_TRANSIENT_MARGIN 20

static inline struct rect pin_rect(const struct pin_state *st) {
	return (struct rect){st->px, st->py, st->width, st->height};
}

int pin_render_output_alloc(struct pin_output *o);
void pin_render_output_free(struct pin_output *o);
void pin_render_output_redraw(struct pin_output *o);
void pin_render_redraw_all(struct pin_state *st);
void pin_render_attach_layer(struct pin_output *o);

void pin_input_attach(struct pin_state *st);
void pin_input_apply_region(struct pin_output *o);
void pin_input_apply_regions(struct pin_state *st);
void pin_input_load_cursors(struct pin_state *st);
void pin_input_destroy_cursors(struct pin_state *st);
void pin_input_refresh_cursor(struct pin_state *st);

int pin_ipc_open(struct pin_state *st);
void pin_ipc_close(struct pin_state *st);
void pin_ipc_handle(struct pin_state *st);

int pin_ipc_broadcast(const char *msg);

#endif
