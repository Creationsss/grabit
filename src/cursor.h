// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_CURSOR_H
#define GRABIT_CURSOR_H

#include <stdint.h>

struct wl_shm;
struct wl_cursor_theme;
struct wl_cursor;
struct wl_pointer;
struct wl_surface;

struct grabit_wl_state;
struct wp_cursor_shape_device_v1;

enum grabit_cursor_kind {
	GCUR_DEFAULT = 0,
	GCUR_HAND,
	GCUR_TEXT,
	GCUR_RESIZE_N,
	GCUR_RESIZE_S,
	GCUR_RESIZE_E,
	GCUR_RESIZE_W,
	GCUR_RESIZE_NE,
	GCUR_RESIZE_NW,
	GCUR_RESIZE_SE,
	GCUR_RESIZE_SW,
	GCUR_COUNT,
};

struct grabit_cursor_set {
	struct wl_pointer *pointer;
	struct wp_cursor_shape_device_v1 *device;
	struct wl_cursor_theme *theme;
	struct wl_surface *surface;
	struct wl_cursor *cursors[GCUR_COUNT];
	int32_t scale;
	int last;
};

void grabit_cursor_set_bind(struct grabit_cursor_set *c, struct grabit_wl_state *s,
							struct wl_pointer *pointer, int32_t scale);
void grabit_cursor_set_apply(struct grabit_cursor_set *c, uint32_t serial,
							 enum grabit_cursor_kind kind);
void grabit_cursor_set_invalidate(struct grabit_cursor_set *c);
void grabit_cursor_set_rescale(struct grabit_cursor_set *c, struct grabit_wl_state *s,
							   int32_t scale);
void grabit_cursor_set_finish(struct grabit_cursor_set *c);

struct wl_cursor_theme *grabit_cursor_theme_load(struct wl_shm *shm, int32_t scale);
struct wl_cursor *grabit_cursor_load_first(struct wl_cursor_theme *theme,
										   const char *const *names);
struct wl_cursor *grabit_cursor_load_default(struct wl_cursor_theme *theme);
struct wl_cursor *grabit_cursor_load_hand(struct wl_cursor_theme *theme);
void grabit_cursor_apply(struct wl_pointer *p, uint32_t serial,
						 struct wl_surface *surface, struct wl_cursor *c,
						 int32_t scale);

#endif
