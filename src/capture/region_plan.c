// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "capture/region_plan.h"

#include "config/config.h"
#include "region/edit_persist.h"
#include "region/region.h"
#include "wl/wl.h"

enum region_plan region_plan_resolve(struct grabit_wl_state *s, struct config *cfg,
									 bool fullscreen, const char *fullscreen_target,
									 bool use_last, struct rect *out) {
	if (fullscreen) {
		struct rect fs;
		int plan = grabit_wl_fullscreen_plan(s, fullscreen_target, &fs);
		if (plan < 0) return REGION_PLAN_NO_MONITOR;
		if (plan == 0) {
			*out = fs;
			return REGION_PLAN_FIXED;
		}
		return REGION_PLAN_MONITOR_PICK;
	}
	if (use_last && last_region_parse(config_get(cfg, "region.last"), out))
		return REGION_PLAN_FIXED;
	return REGION_PLAN_SELECT;
}
