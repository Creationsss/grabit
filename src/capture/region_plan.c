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

static bool radius_is_auto(struct config *cfg) {
	const char *v = config_get(cfg, "region.window_radius");
	return !v || !v[0] || strcmp(v, "auto") == 0;
}

int region_window_radius(struct config *cfg, const struct rect *win) {
	if (radius_is_auto(cfg)) return grabit_wm_window_radius(win);
	return config_get_int_clamp(cfg, "region.window_radius", 0, 0, 100);
}

static bool borders_wanted(struct config *cfg) {
	const char *v = config_get(cfg, "region.window_borders");
	return v && strcmp(v, "true") == 0;
}

int region_window_border(struct config *cfg, const struct rect *win) {
	if (!borders_wanted(cfg)) return 0;
	int b = grabit_wm_window_border(win);
	return b > 0 ? b : 0;
}

bool region_window_borders_included(struct config *cfg) {
	return region_window_border(cfg, NULL) > 0;
}

int region_window_outer_radius(struct config *cfg, const struct rect *win,
							   bool borders_included) {
	int base = region_window_radius(cfg, win);
	if (base <= 0 || !radius_is_auto(cfg)) return base > 0 ? base : 0;
	int border = grabit_wm_window_border(win);
	if (border <= 0) return base;
	return borders_included ? base + border : base + 2 * border;
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
