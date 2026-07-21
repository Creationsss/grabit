// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "cursor.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static bool load_xcursor_file(const char *path, int32_t target_size, struct raw_cursor_image *out) {
	FILE *f = fopen(path, "rb");
	if (!f) return false;

	uint32_t magic;
	if (fread(&magic, 4, 1, f) != 1 || magic != 0x72756358) {
		fclose(f);
		return false;
	}

	uint32_t header_size, version, ntoc;
	if (fread(&header_size, 4, 1, f) != 1 || fread(&version, 4, 1, f) != 1 || fread(&ntoc, 4, 1, f) != 1) {
		fclose(f);
		return false;
	}

	int best_diff = 10000;
	uint32_t best_pos = 0;

	for (uint32_t i = 0; i < ntoc; i++) {
		uint32_t type, subtype, pos;
		if (fread(&type, 4, 1, f) != 1 || fread(&subtype, 4, 1, f) != 1 || fread(&pos, 4, 1, f) != 1) break;
		if (type == 0xfffd0002) {
			int diff = abs((int)subtype - target_size);
			if (diff < best_diff) {
				best_diff = diff;
				best_pos = pos;
			}
		}
	}

	if (best_pos > 0) {
		fseek(f, best_pos, SEEK_SET);
		uint32_t ihdr, itype, isub, iver, iw, ih, ixhot, iyhot, idelay;
		if (fread(&ihdr, 4, 1, f) == 1 && fread(&itype, 4, 1, f) == 1 &&
			fread(&isub, 4, 1, f) == 1 && fread(&iver, 4, 1, f) == 1 &&
			fread(&iw, 4, 1, f) == 1 && fread(&ih, 4, 1, f) == 1 &&
			fread(&ixhot, 4, 1, f) == 1 && fread(&iyhot, 4, 1, f) == 1 &&
			fread(&idelay, 4, 1, f) == 1) {

			size_t psize = (size_t)iw * (size_t)ih * 4;
			out->pixels = malloc(psize);
			if (out->pixels) {
				if (fread(out->pixels, 1, psize, f) == psize) {
					out->width = (int32_t)iw;
					out->height = (int32_t)ih;
					out->hotspot_x = (int32_t)ixhot;
					out->hotspot_y = (int32_t)iyhot;
					fclose(f);
					return true;
				}
				free(out->pixels);
				out->pixels = NULL;
			}
		}
	}
	fclose(f);
	return false;
}

void grabit_cursor_read_raw(const char *name, int32_t scale, struct raw_cursor_image *out) {
	out->pixels = NULL;
	out->width = 0;
	out->height = 0;

	const char *theme = getenv("XCURSOR_THEME");
	if (!theme || !*theme) theme = "default";

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
	int32_t target_size = theme_size * scale;

	const char *home = getenv("HOME");
	char path[512];
	const char *bases[] = {
		"~/.icons",
		"~/.local/share/icons",
		"/usr/share/icons",
		"/usr/share/pixmaps",
		"/usr/local/share/icons"};

	for (int i = 0; i < 5; i++) {
		const char *base = bases[i];
		if (base[0] == '~') {
			if (!home) continue;
			snprintf(path, sizeof path, "%s%s/%s/cursors/%s", home, base + 1, theme, name);
		} else {
			snprintf(path, sizeof path, "%s/%s/cursors/%s", base, theme, name);
		}
		if (load_xcursor_file(path, target_size, out)) return;
	}

	if (strcmp(theme, "default") != 0) {
		for (int i = 0; i < 5; i++) {
			const char *base = bases[i];
			if (base[0] == '~') {
				if (!home) continue;
				snprintf(path, sizeof path, "%s%s/default/cursors/%s", home, base + 1, name);
			} else {
				snprintf(path, sizeof path, "%s/default/cursors/%s", base, name);
			}
			if (load_xcursor_file(path, target_size, out)) return;
		}
	}
}

void grabit_cursor_free_raw(struct raw_cursor_image *raw) {
	if (raw && raw->pixels) {
		free(raw->pixels);
		raw->pixels = NULL;
	}
}
