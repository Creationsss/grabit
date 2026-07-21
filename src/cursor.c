// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "cursor.h"

#include <stdlib.h>

#include <wayland-client.h>
#include <wayland-cursor.h>

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
