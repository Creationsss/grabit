// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "wm/wm.h"

#include "util/rect.h"
#include "wl/toplevel.h"
#include "wl/wl.h"
#include "wm/hyprland.h"
#include "wm/niri.h"
#include "wm/sway.h"

#include <stdlib.h>

enum wm_kind {
	WM_NONE = 0,
	WM_HYPRLAND,
	WM_NIRI,
	WM_SWAY,
};

static enum wm_kind grabit_wm_detect(void) {
	static enum wm_kind cached = WM_NONE;
	static bool probed;
	if (!probed) {
		probed = true;
		if (grabit_hyprland_present())
			cached = WM_HYPRLAND;
		else if (grabit_niri_present())
			cached = WM_NIRI;
		else if (grabit_sway_present())
			cached = WM_SWAY;
	}
	return cached;
}

const char *grabit_wm_current_name(void) {
	static const char *const NAMES[] = {[WM_NONE] = "this compositor",
										[WM_HYPRLAND] = "hyprland",
										[WM_NIRI] = "niri",
										[WM_SWAY] = "sway"};
	const char *name = NAMES[grabit_wm_detect()];
	return name ? name : NAMES[WM_NONE];
}

int grabit_wm_active_window(char **class_out, char **title_out) {
	switch (grabit_wm_detect()) {
	case WM_HYPRLAND:
		if (grabit_hyprland_active_window(class_out, title_out) == 0) return 0;
		break;
	case WM_NIRI:
		if (grabit_niri_active_window(class_out, title_out) == 0) return 0;
		break;
	case WM_SWAY:
	case WM_NONE:
		break;
	}
	return grabit_wl_active_toplevel(class_out, title_out);
}

int grabit_wm_active_window_rect(struct rect *out) {
	switch (grabit_wm_detect()) {
	case WM_HYPRLAND:
		return grabit_hyprland_active_window_rect(out);
	case WM_NIRI:
		return grabit_niri_active_window_rect(out);
	case WM_SWAY:
		return grabit_sway_active_window_rect(out);
	case WM_NONE:
		break;
	}
	return -1;
}

int grabit_wm_window_radius(const struct rect *win) {
	switch (grabit_wm_detect()) {
	case WM_HYPRLAND:
		return grabit_hyprland_window_radius(win);
	case WM_NIRI:
	case WM_SWAY:
	case WM_NONE:
		break;
	}
	return 0;
}

int grabit_wm_windows(struct rect **out, size_t *n_out) {
	switch (grabit_wm_detect()) {
	case WM_HYPRLAND:
		return grabit_hyprland_windows(out, n_out);
	case WM_NIRI:
		return grabit_niri_windows(out, n_out);
	case WM_SWAY:
		return grabit_sway_windows(out, n_out);
	case WM_NONE:
		break;
	}
	*out = NULL;
	*n_out = 0;
	return -1;
}

struct grabit_output *grabit_wm_active_output(struct grabit_wl_state *s) {
	int32_t x = 0, y = 0;
	char *name = NULL;
	switch (grabit_wm_detect()) {
	case WM_HYPRLAND:
		if (grabit_hyprland_cursorpos(&x, &y) == 0)
			return grabit_wl_output_at(s, x, y);
		break;
	case WM_NIRI:
		name = grabit_niri_focused_output();
		break;
	case WM_SWAY:
		name = grabit_sway_focused_output();
		break;
	case WM_NONE:
		break;
	}
	if (name) {
		struct grabit_output *go = grabit_wl_output_by_name(s, name);
		free(name);
		return go;
	}
	return NULL;
}

int grabit_wm_capture_active_window(bool cursor, const char *png_path) {
	switch (grabit_wm_detect()) {
	case WM_NIRI:
		return grabit_niri_capture_active_window(cursor, png_path);
	case WM_HYPRLAND:
	case WM_SWAY:
	case WM_NONE:
		break;
	}
	return -1;
}
