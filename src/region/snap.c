// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "region/wlr_state.h"

#include "region/ui.h"
#include "region/wlr_input_state.h"
#include "util/util.h"

#include <math.h>

#define SNAP_ANIM_MS 110.0

static double snap_progress(const struct ro_state *st) {
	if (!st->snap_anim || st->snap_t0 <= 0) return 1.0;
	double ms = (double)(grabit_now_ns() - st->snap_t0) / 1e6;
	if (ms >= SNAP_ANIM_MS) return 1.0;
	double t = ms / SNAP_ANIM_MS;
	double inv = 1.0 - t;
	return 1.0 - inv * inv * inv;
}

bool region_snap_tick(struct ro_state *st) {
	const struct rect *w = region_snap_window(st, st->snap_hover);
	struct rect to = w ? *w : st->snap_from;
	double a_to = w ? 1.0 : 0.0;
	double t = snap_progress(st);
	const struct rect *f = &st->snap_from;
	st->snap_cur.x = f->x + (int32_t)lround((to.x - f->x) * t);
	st->snap_cur.y = f->y + (int32_t)lround((to.y - f->y) * t);
	st->snap_cur.w = f->w + (int32_t)lround((to.w - f->w) * t);
	st->snap_cur.h = f->h + (int32_t)lround((to.h - f->h) * t);
	st->snap_cur_alpha = st->snap_a_from + (a_to - st->snap_a_from) * t;
	bool running = t < 1.0;
	bool changed = running || st->snap_running;
	st->snap_running = running;
	return changed;
}

void region_snap_set_hover(struct ro_state *st, int hover) {
	if (hover == st->snap_hover) return;
	region_snap_tick(st);
	st->snap_from = st->snap_cur;
	st->snap_a_from = st->snap_cur_alpha;
	st->snap_hover = hover;
	const struct rect *w = region_snap_window(st, hover);
	if (w && st->snap_a_from <= 0.001) st->snap_from = *w;
	st->snap_t0 = grabit_now_ns();
	(void)region_snap_tick(st);
}
