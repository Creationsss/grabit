// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_CAPTURE_REGION_PLAN_H
#define GRABIT_CAPTURE_REGION_PLAN_H

#include <stdbool.h>

struct config;
struct grabit_wl_state;
struct rect;

enum region_plan {
	REGION_PLAN_NO_MONITOR = -1,
	REGION_PLAN_FIXED,
	REGION_PLAN_SELECT,
	REGION_PLAN_MONITOR_PICK,
};

enum region_plan region_plan_resolve(struct grabit_wl_state *s, struct config *cfg,
									 bool fullscreen, const char *fullscreen_target,
									 bool use_last, struct rect *out);

#endif
