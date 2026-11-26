// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "cursor.h"

#include "wl/wl.h"

#include <stdlib.h>

#include <wayland-client.h>
#include <wayland-cursor.h>

#include "cursor-shape-v1-client-protocol.h"

#define DEFAULT_CURSOR_SIZE 24
#define MIN_CURSOR_SIZE 8
#define MAX_CURSOR_SIZE 256

struct wl_cursor_theme *grabit_cursor_theme_load(struct wl_shm *shm, int32_t scale) {
	if (!shm) return NULL;
	const char *theme_name = getenv("XCURSOR_THEME");
	int32_t theme_size = DEFAULT_CURSOR_SIZE;
	const char *size_env = getenv("XCURSOR_SIZE");
	if (size_env && *size_env) {
		char *end = NULL;
		long v = strtol(size_env, &end, 10);
		if (end != size_env && v >= MIN_CURSOR_SIZE && v <= MAX_CURSOR_SIZE) {
			theme_size = (int32_t)v;
		}
	}
	if (scale < 1) scale = 1;
	return wl_cursor_theme_load(theme_name, theme_size * scale, shm);
}

struct wl_cursor *grabit_cursor_load_first(struct wl_cursor_theme *theme,
										   const char *const *names) {
	if (!theme || !names) return NULL;
	for (size_t i = 0; names[i]; i++) {
		struct wl_cursor *c = wl_cursor_theme_get_cursor(theme, names[i]);
		if (c) return c;
	}
	return NULL;
}

struct wl_cursor *grabit_cursor_load_default(struct wl_cursor_theme *theme) {
	static const char *const default_names[] = {
		"left_ptr",
		"default",
		"arrow",
		NULL,
	};
	return grabit_cursor_load_first(theme, default_names);
}

struct wl_cursor *grabit_cursor_load_hand(struct wl_cursor_theme *theme) {
	static const char *const hand_names[] = {
		"pointer",
		"hand2",
		"pointing_hand",
		"hand",
		"hand1",
		"left_ptr",
		"default",
		NULL,
	};
	return grabit_cursor_load_first(theme, hand_names);
}

void grabit_cursor_apply(struct wl_pointer *p, uint32_t serial,
						 struct wl_surface *surface, struct wl_cursor *c,
						 int32_t scale) {
	if (!p || !surface || !c || c->image_count == 0) return;
	struct wl_cursor_image *img = c->images[0];
	struct wl_buffer *buf = wl_cursor_image_get_buffer(img);
	if (!buf) return;
	if (scale < 1) scale = 1;
	wl_pointer_set_cursor(p, serial, surface,
						  (int32_t)img->hotspot_x / scale,
						  (int32_t)img->hotspot_y / scale);
	wl_surface_set_buffer_scale(surface, scale);
	wl_surface_attach(surface, buf, 0, 0);
	wl_surface_damage_buffer(surface, 0, 0,
							 (int32_t)img->width, (int32_t)img->height);
	wl_surface_commit(surface);
}

static const char *const CURSOR_NAMES[GCUR_COUNT][5] = {
	[GCUR_DEFAULT] = {"left_ptr", "default", "arrow", NULL},
	[GCUR_HAND] = {"pointer", "hand2", "pointing_hand", "hand", NULL},
	[GCUR_TEXT] = {"text", "xterm", "ibeam", NULL},
	[GCUR_RESIZE_N] = {"n-resize", "top_side", "size_ver", NULL},
	[GCUR_RESIZE_S] = {"s-resize", "bottom_side", "size_ver", NULL},
	[GCUR_RESIZE_E] = {"e-resize", "right_side", "size_hor", NULL},
	[GCUR_RESIZE_W] = {"w-resize", "left_side", "size_hor", NULL},
	[GCUR_RESIZE_NE] = {"ne-resize", "top_right_corner", "size_bdiag", NULL},
	[GCUR_RESIZE_NW] = {"nw-resize", "top_left_corner", "size_fdiag", NULL},
	[GCUR_RESIZE_SE] = {"se-resize", "bottom_right_corner", "size_fdiag", NULL},
	[GCUR_RESIZE_SW] = {"sw-resize", "bottom_left_corner", "size_bdiag", NULL},
};

static const uint32_t CURSOR_SHAPES[GCUR_COUNT] = {
	[GCUR_DEFAULT] = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT,
	[GCUR_HAND] = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER,
	[GCUR_TEXT] = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_TEXT,
	[GCUR_RESIZE_N] = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_N_RESIZE,
	[GCUR_RESIZE_S] = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_S_RESIZE,
	[GCUR_RESIZE_E] = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_E_RESIZE,
	[GCUR_RESIZE_W] = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_W_RESIZE,
	[GCUR_RESIZE_NE] = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NE_RESIZE,
	[GCUR_RESIZE_NW] = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NW_RESIZE,
	[GCUR_RESIZE_SE] = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_SE_RESIZE,
	[GCUR_RESIZE_SW] = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_SW_RESIZE,
};

static void load_theme(struct grabit_cursor_set *c, struct grabit_wl_state *s) {
	c->theme = grabit_cursor_theme_load(s->shm, c->scale);
	for (int i = 0; i < GCUR_COUNT; i++)
		c->cursors[i] = c->theme ? grabit_cursor_load_first(c->theme, CURSOR_NAMES[i]) : NULL;
}

void grabit_cursor_set_bind(struct grabit_cursor_set *c, struct grabit_wl_state *s,
							struct wl_pointer *pointer, int32_t scale) {
	c->pointer = pointer;
	c->scale = scale < 1 ? 1 : scale;
	c->last = -1;
	if (!pointer) return;
	if (s->cursor_shape_manager) {
		c->device = wp_cursor_shape_manager_v1_get_pointer(s->cursor_shape_manager,
														   pointer);
		return;
	}
	if (!s->shm || !s->compositor) return;
	load_theme(c, s);
	if (c->theme) c->surface = wl_compositor_create_surface(s->compositor);
}

void grabit_cursor_set_rescale(struct grabit_cursor_set *c, struct grabit_wl_state *s,
							   int32_t scale) {
	if (scale < 1) scale = 1;
	if (!c->theme || scale == c->scale) return;
	wl_cursor_theme_destroy(c->theme);
	c->theme = NULL;
	c->scale = scale;
	load_theme(c, s);
	grabit_cursor_set_invalidate(c);
}

void grabit_cursor_set_invalidate(struct grabit_cursor_set *c) {
	c->last = -1;
}

void grabit_cursor_set_apply(struct grabit_cursor_set *c, uint32_t serial,
							 enum grabit_cursor_kind kind) {
	if (!c->pointer || serial == 0 || kind < 0 || kind >= GCUR_COUNT) return;
	if ((int)kind == c->last) return;
	c->last = (int)kind;
	if (c->device) {
		wp_cursor_shape_device_v1_set_shape(c->device, serial, CURSOR_SHAPES[kind]);
		return;
	}
	struct wl_cursor *cur = c->cursors[kind];
	if (!cur) cur = c->cursors[GCUR_DEFAULT];
	grabit_cursor_apply(c->pointer, serial, c->surface, cur, c->scale);
}

void grabit_cursor_set_finish(struct grabit_cursor_set *c) {
	if (c->device) wp_cursor_shape_device_v1_destroy(c->device);
	if (c->surface) wl_surface_destroy(c->surface);
	if (c->theme) wl_cursor_theme_destroy(c->theme);
	c->device = NULL;
	c->surface = NULL;
	c->theme = NULL;
	c->pointer = NULL;
}
