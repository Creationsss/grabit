// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "region/wlr_input_state.h"

#include "region/annotate.h"
#include "region/wlr_state.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/timerfd.h>
#include <unistd.h>

#define NUDGE_DELAY_MS 300
#define NUDGE_REPEAT_MS 30
#define NUDGE_STEP_MAX 10
#define NUDGE_ACCEL_TICKS 4

#include "region/input_state_internal.h"

static void nudge_apply(struct ro_state *st, int32_t dx, int32_t dy) {
	if (st->shift_held) {
		st->sel_w += dx;
		st->sel_h += dy;
		if (st->sel_w < 1) st->sel_w = 1;
		if (st->sel_h < 1) st->sel_h = 1;
		if (st->bounds.w > 0 && st->bounds.h > 0) {
			if (st->sel_x + st->sel_w > st->bounds.x + st->bounds.w)
				st->sel_w = st->bounds.x + st->bounds.w - st->sel_x;
			if (st->sel_y + st->sel_h > st->bounds.y + st->bounds.h)
				st->sel_h = st->bounds.y + st->bounds.h - st->sel_y;
		}
	} else {
		st->sel_x += dx;
		st->sel_y += dy;
		region_clamp_move(st);
	}
}

static int32_t nudge_dx(uint32_t held) {
	return ((held & NUDGE_RIGHT) ? 1 : 0) - ((held & NUDGE_LEFT) ? 1 : 0);
}

static int32_t nudge_dy(uint32_t held) {
	return ((held & NUDGE_DOWN) ? 1 : 0) - ((held & NUDGE_UP) ? 1 : 0);
}

void region_nudge_press(struct ro_state *st, uint32_t dir) {
	if (st->nudge_held & dir) return;
	if (st->nudge_held == 0) region_undo_begin(st);
	nudge_apply(st, nudge_dx(dir), nudge_dy(dir));
	if (st->nudge_timer_fd < 0) return;
	if (st->nudge_held == 0) {
		st->nudge_ticks = 0;
		struct itimerspec it = {
			.it_value = {.tv_nsec = NUDGE_DELAY_MS * 1000000L},
			.it_interval = {.tv_nsec = NUDGE_REPEAT_MS * 1000000L},
		};
		timerfd_settime(st->nudge_timer_fd, 0, &it, NULL);
	}
	st->nudge_held |= dir;
}

void region_nudge_release(struct ro_state *st, uint32_t dir) {
	st->nudge_held &= ~dir;
	if (st->nudge_held == 0) region_nudge_disarm(st);
}

void region_nudge_disarm(struct ro_state *st) {
	region_undo_commit(st);
	if (st->nudge_timer_fd < 0) return;
	st->nudge_held = 0;
	st->nudge_ticks = 0;
	struct itimerspec it = {0};
	timerfd_settime(st->nudge_timer_fd, 0, &it, NULL);
}

void region_nudge_tick(struct ro_state *st) {
	uint64_t expirations = 0;
	ssize_t r = read(st->nudge_timer_fd, &expirations, sizeof expirations);
	(void)r;
	if (!st->region_locked || st->nudge_held == 0 || st->text_input_active ||
		region_drag_active(st)) {
		region_nudge_disarm(st);
		return;
	}
	st->nudge_ticks++;
	int32_t step = st->nudge_ticks / NUDGE_ACCEL_TICKS + 1;
	if (step > NUDGE_STEP_MAX) step = NUDGE_STEP_MAX;
	int32_t dx = nudge_dx(st->nudge_held);
	int32_t dy = nudge_dy(st->nudge_held);
	if (dx == 0 && dy == 0) return;
	nudge_apply(st, dx * step, dy * step);
	region_render_request_redraw_all(st);
}
