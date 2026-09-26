// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "capture/region_plan.h"

#include "config/config.h"

#include "region/edit_persist.h"
#include "util/rect.h"
#include "wl/wl.h"
#include "wm/wm.h"
#include <string.h>

int region_window_radius(struct config *cfg, const struct rect *win) {
	const char *v = config_get(cfg, "region.window_radius");
	if (!v || !v[0] || strcmp(v, "auto") == 0) return grabit_wm_window_radius(win);
	return config_get_int_clamp(cfg, "region.window_radius", 0, 0, 100);
}

int region_window_border(struct config *cfg, const struct rect *win) {
	const char *v = config_get(cfg, "region.window_borders");
	if (!v || strcmp(v, "true") != 0) return 0;
	int b = grabit_wm_window_border(win);
	if (b <= 0) return 0;
	/* ipc geometry is integer-rounded while the compositor rasterizes the
	   1px border ring at float coordinates, so a crop exactly around the
	   ring can miss it on one side. Capture a 1px background margin too;
	   a sub-pixel shift then only moves background, never clips the ring. */
	return b + 1;
}

bool region_window_borders_included(struct config *cfg) {
	const char *v = config_get(cfg, "region.window_borders");
	if (!v || strcmp(v, "true") != 0) return false;
	return grabit_wm_window_border(NULL) > 0;
}

int region_window_outer_radius(struct config *cfg, const struct rect *win,
							   bool borders_included) {
	int base = region_window_radius(cfg, win);
	if (base <= 0) return 0;
	const char *v = config_get(cfg, "region.window_radius");
	if (v && v[0] && strcmp(v, "auto") != 0) return base;
	int border = grabit_wm_window_border(win);
	if (border < 0) border = 0;
	/* hyprland draws the border ring outside the rounding radius, so the
	   captured outer arc is rounding + border. Without border expansion the
	   frame corner sits one ring inside the outer edge, needing another
	   border px of tolerance on top. */
	return base + border + (borders_included ? 0 : border);
}

enum region_plan region_plan_resolve(struct grabit_wl_state *s, struct config *cfg,
									 const struct region_plan_req *req,
									 struct rect *out) {
	if (req->fullscreen) {
		struct rect fs;
		int plan = grabit_wl_fullscreen_plan(s, req->fullscreen_target, &fs);
		if (plan < 0) return REGION_PLAN_NO_MONITOR;
		if (plan == 0) {
			*out = fs;
			return REGION_PLAN_FIXED;
		}
		return REGION_PLAN_MONITOR_PICK;
	}
	if (req->window) {
		if (grabit_wm_active_window_rect(out) != 0) return REGION_PLAN_NO_WINDOW;
		int b = region_window_border(cfg, out);
		if (b > 0) *out = rect_inflate(*out, b);
		return REGION_PLAN_FIXED;
	}
	if (req->use_last && last_region_parse(config_get(cfg, "region.last"), out))
		return REGION_PLAN_FIXED;
	return REGION_PLAN_SELECT;
}
