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

static void get_gtk_cursor_settings(char **theme, int32_t *size) {
	const char *home = getenv("HOME");
	if (!home) return;
	char path[1024];
	snprintf(path, sizeof(path), "%s/.config/gtk-3.0/settings.ini", home);
	FILE *f = fopen(path, "r");
	if (!f) return;
	char line[256];
	while (fgets(line, sizeof(line), f)) {
		if (strncmp(line, "gtk-cursor-theme-name=", 22) == 0) {
			char *val = line + 22;
			val[strcspn(val, "\r\n")] = 0;
			if (*theme) free(*theme);
			*theme = strdup(val);
		} else if (strncmp(line, "gtk-cursor-theme-size=", 22) == 0) {
			int v = atoi(line + 22);
			if (v >= MIN_CURSOR_SIZE && v <= MAX_CURSOR_SIZE) {
				*size = (int32_t)v;
			}
		}
	}
	fclose(f);
}

struct wl_cursor_theme *grabit_cursor_theme_load(struct wl_shm *shm, int32_t scale) {
	if (!shm) return NULL;
	char *theme_name_alloc = NULL;
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
	
	if (theme_name) theme_name_alloc = strdup(theme_name);
	get_gtk_cursor_settings(&theme_name_alloc, &theme_size);
	
	if (scale < 1) scale = 1;

	char new_path[4096] = "";
	const char *old = getenv("XCURSOR_PATH");
	if (old) snprintf(new_path, sizeof(new_path), "%s:", old);
	strncat(new_path, "~/.icons:~/.local/share/icons:/usr/share/icons:/usr/share/pixmaps", sizeof(new_path) - strlen(new_path) - 1);
	
	const char *xdg = getenv("XDG_DATA_DIRS");
	if (xdg) {
		char xcopy[4096];
		strncpy(xcopy, xdg, sizeof(xcopy) - 1);
		xcopy[sizeof(xcopy) - 1] = '\0';
		char *save;
		for (char *p = strtok_r(xcopy, ":", &save); p; p = strtok_r(NULL, ":", &save)) {
			size_t len = strlen(new_path);
			snprintf(new_path + len, sizeof(new_path) - len, ":%s/icons", p);
		}
	}
	
	struct wl_cursor_theme *t = NULL;
	char *old_copy = old ? strdup(old) : NULL;
	setenv("XCURSOR_PATH", new_path, 1);
	
	t = wl_cursor_theme_load(theme_name_alloc, theme_size * scale, shm);
	if (!t && (!theme_name_alloc || strcmp(theme_name_alloc, "default") != 0)) {
		t = wl_cursor_theme_load("default", theme_size * scale, shm);
	}
	
	if (old_copy) {
		setenv("XCURSOR_PATH", old_copy, 1);
		free(old_copy);
	} else {
		unsetenv("XCURSOR_PATH");
	}
	
	if (theme_name_alloc) free(theme_name_alloc);
	
	return t;
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

	char *theme_alloc = NULL;
	const char *theme = getenv("XCURSOR_THEME");
	
	int32_t theme_size = DEFAULT_CURSOR_SIZE;
	const char *size_env = getenv("XCURSOR_SIZE");
	if (size_env && *size_env) {
		char *end = NULL;
		long v = strtol(size_env, &end, 10);
		if (end != size_env && v >= MIN_CURSOR_SIZE && v <= MAX_CURSOR_SIZE) {
			theme_size = (int32_t)v;
		}
	}
	
	if (theme) theme_alloc = strdup(theme);
	get_gtk_cursor_settings(&theme_alloc, &theme_size);
	
	if (theme_alloc) {
		theme = theme_alloc;
	} else if (!theme || !*theme) {
		theme = "default";
	}

	if (scale < 1) scale = 1;
	int32_t target_size = theme_size * scale;

	const char *home = getenv("HOME");
	char path[512];
	
	const char *themes[] = {theme, "default", "Adwaita", "breeze_cursors", "DMZ-White"};
	
	for (int t = 0; t < 5; t++) {
		if (t > 0 && strcmp(theme, themes[t]) == 0) continue;
		
		const char *search_path = getenv("XCURSOR_PATH");
		if (!search_path || !*search_path) {
			search_path = "~/.icons:~/.local/share/icons:/usr/share/icons:/usr/share/pixmaps";
		}
		
		char paths[2048];
		strncpy(paths, search_path, sizeof(paths) - 1);
		paths[sizeof(paths) - 1] = '\0';
		
		char *saveptr1;
		for (char *p = strtok_r(paths, ":", &saveptr1); p != NULL; p = strtok_r(NULL, ":", &saveptr1)) {
			if (p[0] == '~') {
				if (!home) continue;
				snprintf(path, sizeof path, "%s%s/%s/cursors/%s", home, p + 1, themes[t], name);
			} else {
				snprintf(path, sizeof path, "%s/%s/cursors/%s", p, themes[t], name);
			}
			if (load_xcursor_file(path, target_size, out)) {
				if (theme_alloc) free(theme_alloc);
				return;
			}
		}
		
		const char *xdg_data = getenv("XDG_DATA_DIRS");
		if (xdg_data && *xdg_data) {
			strncpy(paths, xdg_data, sizeof(paths) - 1);
			paths[sizeof(paths) - 1] = '\0';
			for (char *p = strtok_r(paths, ":", &saveptr1); p != NULL; p = strtok_r(NULL, ":", &saveptr1)) {
				snprintf(path, sizeof path, "%s/icons/%s/cursors/%s", p, themes[t], name);
				if (load_xcursor_file(path, target_size, out)) {
					if (theme_alloc) free(theme_alloc);
					return;
				}
			}
		}
	}
	if (theme_alloc) free(theme_alloc);
}

void grabit_cursor_free_raw(struct raw_cursor_image *raw) {
	if (raw && raw->pixels) {
		free(raw->pixels);
		raw->pixels = NULL;
	}
}
