// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "record/setup.h"

#include "args.h"
#include "capture/capture.h"
#include "capture/region_plan.h"
#include "log.h"
#include "notify/notify.h"
#include "paths.h"
#include "region/region.h"
#include "wl/wl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *rec_record_path(struct config *cfg, const struct args *a,
					  const char *format, bool keep_locally) {
	enum paths_dest dest = keep_locally ? PATHS_DEST_VIDEOS : PATHS_DEST_TEMP;
	char ext[16];
	snprintf(ext, sizeof ext, ".%s", format);
	return paths_build_output(cfg, a->filename_tpl, ext, dest);
}

void rec_fail_notify(const char *body) {
	notify_send(&(struct notify_opts){
		.summary = "Recording failed",
		.body = body,
		.force = true,
	});
}

int rec_pick_region(struct grabit_wl_state *s, struct config *cfg,
					const struct args *a, struct rect *out) {
	struct region_plan_req req = {
		.fullscreen = a->fullscreen,
		.fullscreen_target = a->fullscreen_target,
		.use_last = a->last_region,
	};
	enum region_plan plan = region_plan_resolve(s, cfg, &req, out);
	if (plan == REGION_PLAN_NO_MONITOR) {
		rec_fail_notify("no matching monitor");
		return -1;
	}
	if (plan == REGION_PLAN_FIXED) return 0;

	struct image *frozen = calloc(s->n_outputs, sizeof *frozen);
	if (!frozen) {
		log_error("oom");
		rec_fail_notify("out of memory");
		return -1;
	}
	for (size_t i = 0; i < s->n_outputs; i++) {
		if (capture_output_full(s, s->outputs[i], false, &frozen[i]) != 0) {
			log_warn("freeze capture of %s failed; selector will be dimmed",
					 s->outputs[i]->name ? s->outputs[i]->name : "?");
			memset(&frozen[i], 0, sizeof frozen[i]);
			continue;
		}
		if (image_apply_transform(&frozen[i], s->outputs[i]->transform) != 0) {
			log_warn("freeze transform of %s failed; output may look skewed",
					 s->outputs[i]->name ? s->outputs[i]->name : "?");
		}
	}

	struct rect *mon = NULL;
	size_t n_mon = 0;
	if (plan == REGION_PLAN_MONITOR_PICK) grabit_wl_monitor_rects(s, &mon, &n_mon);
	int rc = region_select(s, cfg, frozen, false, out, NULL, NULL, NULL, NULL,
						   NULL, NULL, mon, n_mon);
	free(mon);
	if (rc != 0 && rc != REGION_SELECT_CANCELLED)
		rec_fail_notify("could not open the region selector");

	for (size_t i = 0; i < s->n_outputs; i++)
		image_free(&frozen[i]);
	free(frozen);
	return rc;
}
