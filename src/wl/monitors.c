// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700

#include "util/util.h"
#include "wl/wl.h"

#include "log.h"
#include "region/region.h"

#include <stdlib.h>
#include <string.h>

#include <stdio.h>

#include <wayland-client.h>

static void monitor_names(const struct grabit_wl_state *s, char *out, size_t cap) {
	size_t off = 0;
	out[0] = '\0';
	for (size_t i = 0; i < s->n_outputs; i++) {
		const struct grabit_output *o = s->outputs[i];
		if (o->dead) continue;
		if (!grabit_join_appendf(out, cap, &off, ", ", "%s (%dx%d)",
								 o->name ? o->name : "?", o->logical_width,
								 o->logical_height))
			break;
	}
}

void grabit_wl_monitor_rects(struct grabit_wl_state *s, struct rect **out, size_t *n_out) {
	*out = NULL;
	*n_out = 0;
	if (s->n_outputs == 0) return;
	struct rect *r = malloc(s->n_outputs * sizeof *r);
	if (!r) return;
	size_t n = 0;
	for (size_t i = 0; i < s->n_outputs; i++) {
		if (s->outputs[i]->dead) continue;
		grabit_output_rect(s->outputs[i], &r[n++]);
	}
	if (n == 0) {
		free(r);
		return;
	}
	*out = r;
	*n_out = n;
}

void grabit_wl_outputs_bbox(struct grabit_wl_state *s, struct rect *out) {
	memset(out, 0, sizeof *out);
	int32_t min_x = INT32_MAX, min_y = INT32_MAX;
	int32_t max_x = INT32_MIN, max_y = INT32_MIN;
	for (size_t i = 0; i < s->n_outputs; i++) {
		if (s->outputs[i]->dead) continue;
		struct rect r;
		grabit_output_rect(s->outputs[i], &r);
		if (r.x < min_x) min_x = r.x;
		if (r.y < min_y) min_y = r.y;
		if (r.x + r.w > max_x) max_x = r.x + r.w;
		if (r.y + r.h > max_y) max_y = r.y + r.h;
	}
	if (min_x == INT32_MAX) return;
	out->x = min_x;
	out->y = min_y;
	out->w = max_x - min_x;
	out->h = max_y - min_y;
}

int grabit_wl_fullscreen_plan(struct grabit_wl_state *s, const char *spec, struct rect *out) {
	size_t live = 0;
	for (size_t i = 0; i < s->n_outputs; i++)
		if (!s->outputs[i]->dead) live++;
	if (live == 0) {
		log_error("fullscreen: no outputs");
		return -1;
	}
	if (spec && strcmp(spec, "all") == 0) {
		grabit_wl_outputs_bbox(s, out);
		return 0;
	}
	if (spec && spec[0]) {
		struct grabit_output *target = NULL;
		char *end = NULL;
		long n = strtol(spec, &end, 10);
		if (end && *end == '\0' && n >= 1 && (size_t)n <= live) {
			size_t idx = 0;
			for (size_t i = 0; i < s->n_outputs; i++) {
				if (s->outputs[i]->dead) continue;
				if (++idx == (size_t)n) {
					target = s->outputs[i];
					break;
				}
			}
		} else {
			target = grabit_wl_output_by_name(s, spec);
		}
		if (!target) {
			char have[512];
			monitor_names(s, have, sizeof have);
			log_error("fullscreen: no monitor matches `%s` (have: %s)", spec,
					  have[0] ? have : "none");
			return -1;
		}
		grabit_output_rect(target, out);
		return 0;
	}
	if (live == 1) {
		grabit_output_rect(grabit_wl_primary_output(s), out);
		return 0;
	}
	return 1;
}
