// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "pin/pin_state.h"

#include "cursor.h"
#include "wl/wl.h"

#include <wayland-client.h>
#include <wayland-cursor.h>

#include "cursor-shape-v1-client-protocol.h"

void pin_cursor_load(struct pin_state *st) {
	if (!st->pointer) return;
	if (st->wls->cursor_shape_manager) {
		st->cursor_shape = wp_cursor_shape_manager_v1_get_pointer(
			st->wls->cursor_shape_manager, st->pointer);
		return;
	}
	if (!st->wls->shm || !st->wls->compositor) return;
	st->cursor_theme = grabit_cursor_theme_load(st->wls->shm, st->cursor_scale);
	if (!st->cursor_theme) return;
	static const char *const move[] = {
		"grab",
		"openhand",
		"fleur",
		"move",
		"all-scroll",
		"left_ptr",
		NULL,
	};
	static const char *const grabbing[] = {
		"grabbing",
		"closedhand",
		"fleur",
		"move",
		"left_ptr",
		NULL,
	};
	st->cursor_hand = grabit_cursor_load_hand(st->cursor_theme);
	st->cursor_move = grabit_cursor_load_first(st->cursor_theme, move);
	st->cursor_grabbing = grabit_cursor_load_first(st->cursor_theme, grabbing);
	st->cursor_surface = wl_compositor_create_surface(st->wls->compositor);
}

void pin_cursor_destroy(struct pin_state *st) {
	if (st->cursor_shape) {
		wp_cursor_shape_device_v1_destroy(st->cursor_shape);
		st->cursor_shape = NULL;
	}
	if (st->cursor_surface) {
		wl_surface_destroy(st->cursor_surface);
		st->cursor_surface = NULL;
	}
	if (st->cursor_theme) {
		wl_cursor_theme_destroy(st->cursor_theme);
		st->cursor_theme = NULL;
	}
}

static void apply_cursor(struct pin_state *st, int kind) {
	if (st->last_pointer_serial == 0 || kind == PIN_CUR_NONE) return;
	if (st->cursor_shape) {
		static const uint32_t shapes[] = {
			WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT,
			WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER,
			WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_GRAB,
			WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_GRABBING,
		};
		wp_cursor_shape_device_v1_set_shape(st->cursor_shape,
											st->last_pointer_serial, shapes[kind]);
		return;
	}
	struct wl_cursor *c = kind == PIN_CUR_HAND	 ? st->cursor_hand
						  : kind == PIN_CUR_MOVE ? st->cursor_move
												 : st->cursor_grabbing;
	grabit_cursor_apply(st->pointer, st->last_pointer_serial,
						st->cursor_surface, c, st->cursor_scale);
}

void pin_cursor_update(struct pin_state *st) {
	if (!st->ptr_on) return;
	int kind = PIN_CUR_NONE;
	if (st->clickable) {
		kind = PIN_CUR_HAND;
	} else if (st->input_grabbed) {
		if (st->dragging)
			kind = PIN_CUR_GRABBING;
		else if (pin_in_close_button(st))
			kind = PIN_CUR_HAND;
		else
			kind = PIN_CUR_MOVE;
	}
	if (kind == st->cursor_kind) return;
	st->cursor_kind = kind;
	apply_cursor(st, kind);
}

void pin_cursor_refresh(struct pin_state *st) {
	st->cursor_kind = PIN_CUR_NONE;
	pin_cursor_update(st);
}
