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

struct rect region_window_with_border(struct grabit_wl_state *s, struct config *cfg,
									  struct rect win) {
	int b = region_window_border(cfg, &win);
	if (b <= 0) return win;
	struct rect grown = rect_inflate(win, b);
	struct grabit_output *o =
		grabit_wl_output_at(s, win.x + win.w / 2, win.y + win.h / 2);
	if (o) {
		struct rect mon;
		grabit_output_rect(o, &mon);
		grown = rect_intersect(grown, mon);
	}
	return grown.w > 0 && grown.h > 0 ? grown : win;
}

int region_window_border(struct config *cfg, const struct rect *win) {
	if (!borders_wanted(cfg)) return 0;
	int b = grabit_wm_window_border(win);
	return b > 0 ? b : 0;
}

int region_window_outer_radius(struct config *cfg, const struct rect *win) {
	int base = region_window_radius(cfg, win);
	if (base <= 0 || !radius_is_auto(cfg)) return base > 0 ? base : 0;
	int border = grabit_wm_window_border(win);
	if (border <= 0) return base;
	return borders_wanted(cfg) ? base + border : base + 2 * border;
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
		*out = region_window_with_border(s, cfg, *out);
		return REGION_PLAN_FIXED;
	}
	if (req->use_last && last_region_parse(config_get(cfg, "region.last"), out))
		return REGION_PLAN_FIXED;
	return REGION_PLAN_SELECT;
}
