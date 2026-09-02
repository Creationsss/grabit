// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_PIN_STATE_H
#define GRABIT_PIN_STATE_H

#include "wl/touch.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <cairo/cairo.h>
#include <wayland-client.h>

#include "region/region.h"
#include "util/util.h"

struct grabit_output;
struct grabit_wl_state;
struct zwlr_layer_surface_v1;
struct wl_cursor;
struct wl_cursor_theme;
struct wp_cursor_shape_device_v1;
struct wp_fractional_scale_v1;
struct wp_viewport;
struct pin_state;

struct pin_output {
	struct pin_state *st;
	struct grabit_output *go;
	struct wl_surface *surface;
	struct zwlr_layer_surface_v1 *layer;
	struct wp_viewport *viewport;
	struct wp_fractional_scale_v1 *fractional;
	struct grabit_shm_pool pool;
	struct rect slot_shown[GRABIT_SHM_SLOTS];
	struct rect shown;
	struct rect region;
	int32_t width;
	int32_t height;
	int32_t scale;
	uint32_t frac_scale;
	bool configured;
	bool mapped;
	bool dirty;
	struct wl_callback *frame_cb;
};

struct pin_state {
	struct grabit_wl_state *wls;

	struct pin_output **outs;
	size_t n;
	uint32_t outputs_serial;
	struct grabit_output *target;
	struct rect bounds;

	cairo_surface_t *image;
	int32_t img_w;
	int32_t img_h;

	struct wl_pointer *pointer;
	struct wl_touch *touch;
	struct gtouch_slot touch_slot;

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

	struct wp_cursor_shape_device_v1 *cursor_shape;
	struct wl_cursor_theme *cursor_theme;
	struct wl_surface *cursor_surface;
	struct wl_cursor *cursor_hand;
	struct wl_cursor *cursor_move;
	struct wl_cursor *cursor_grabbing;
	int cursor_kind;
	uint32_t last_pointer_serial;
	int32_t cursor_scale;

	int ipc_fd;
	int ipc_lock_fd;
	char ipc_path[256];
	char ipc_lock_path[256];

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

#define PIN_CUR_NONE 0
#define PIN_CUR_HAND 1
#define PIN_CUR_MOVE 2
#define PIN_CUR_GRABBING 3

static inline struct rect pin_rect(const struct pin_state *st) {
	return (struct rect){st->px, st->py, st->width, st->height};
}

static inline bool pin_in_close_button(const struct pin_state *st) {
	struct rect btn = {st->width - PIN_CLOSE_BTN_SIZE - PIN_CLOSE_BTN_INSET,
					   PIN_CLOSE_BTN_INSET, PIN_CLOSE_BTN_SIZE, PIN_CLOSE_BTN_SIZE};
	return rect_contains(btn, st->cx - st->px, st->cy - st->py);
}

void pin_render_output_free(struct pin_output *o);
void pin_render_output_redraw(struct pin_output *o);
void pin_render_redraw_all(struct pin_state *st);
int pin_render_create_layer(struct pin_output *o);
void pin_render_create_fractional(struct pin_output *o);

void pin_sync_outputs(struct pin_state *st);
void pin_outputs_finish(struct pin_state *st);

void pin_input_attach(struct pin_state *st);
void pin_input_apply_region(struct pin_output *o);
void pin_cursor_load(struct pin_state *st);
void pin_cursor_destroy(struct pin_state *st);
void pin_cursor_update(struct pin_state *st);
void pin_cursor_refresh(struct pin_state *st);

int pin_ipc_open(struct pin_state *st);
void pin_ipc_close(struct pin_state *st);
void pin_ipc_handle(struct pin_state *st);

int pin_ipc_broadcast(const char *msg);

struct transient_extras {
	const char *position;
	const char *output_name;
	const char *hover_caption;
	const char *click_open;
};

int gpin_main(cairo_surface_t *img, bool have_rect, struct rect r,
			  bool transient, int dismiss_secs, const struct transient_extras *te);

#endif
