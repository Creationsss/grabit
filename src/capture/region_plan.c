// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "capture/region_plan.h"

#include "config/config.h"
#include "region/edit_persist.h"
#include "region/region.h"
#include "wl/wl.h"
#include "wm/wm.h"

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
		return grabit_wm_active_window_rect(out) == 0 ? REGION_PLAN_FIXED
													  : REGION_PLAN_NO_WINDOW;
	}
	if (req->use_last && last_region_parse(config_get(cfg, "region.last"), out))
		return REGION_PLAN_FIXED;
	return REGION_PLAN_SELECT;
}
