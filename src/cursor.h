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

struct wl_cursor_theme *grabit_cursor_theme_load(struct wl_shm *shm, int32_t scale);
struct wl_cursor *grabit_cursor_load_first(struct wl_cursor_theme *theme,
										   const char *const *names);
struct wl_cursor *grabit_cursor_load_hand(struct wl_cursor_theme *theme);
void grabit_cursor_apply(struct wl_pointer *p, uint32_t serial,
						 struct wl_surface *surface, struct wl_cursor *c,
						 int32_t scale);

#endif
