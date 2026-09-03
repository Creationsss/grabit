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

#include "region/input_state_internal.h"

void region_handle_points(const struct ro_state *st, int32_t hx[8], int32_t hy[8]) {
	int32_t l = st->sel_x, r = st->sel_x + st->sel_w;
	int32_t t = st->sel_y, b = st->sel_y + st->sel_h;
	int32_t mx = (l + r) / 2, my = (t + b) / 2;
	hx[HANDLE_NW] = l;
	hy[HANDLE_NW] = t;
	hx[HANDLE_N] = mx;
	hy[HANDLE_N] = t;
	hx[HANDLE_NE] = r;
	hy[HANDLE_NE] = t;
	hx[HANDLE_E] = r;
	hy[HANDLE_E] = my;
	hx[HANDLE_SE] = r;
	hy[HANDLE_SE] = b;
	hx[HANDLE_S] = mx;
	hy[HANDLE_S] = b;
	hx[HANDLE_SW] = l;
	hy[HANDLE_SW] = b;
	hx[HANDLE_W] = l;
	hy[HANDLE_W] = my;
}

int region_handle_at(const struct ro_state *st, int32_t x, int32_t y) {
	if (!st->region_locked || !st->has_selection) return HANDLE_NONE;
	int32_t hx[8], hy[8];
	region_handle_points(st, hx, hy);
	for (int i = 0; i < 8; i++) {
		int32_t dx = x - hx[i], dy = y - hy[i];
		if (dx * dx + dy * dy <= HANDLE_RADIUS * HANDLE_RADIUS) return i;
	}
	return HANDLE_NONE;
}

static int flip_handle_x(int h) {
	switch (h) {
	case HANDLE_NW:
		return HANDLE_NE;
	case HANDLE_NE:
		return HANDLE_NW;
	case HANDLE_E:
		return HANDLE_W;
	case HANDLE_SE:
		return HANDLE_SW;
	case HANDLE_SW:
		return HANDLE_SE;
	case HANDLE_W:
		return HANDLE_E;
	default:
		return h;
	}
}

static int flip_handle_y(int h) {
	switch (h) {
	case HANDLE_NW:
		return HANDLE_SW;
	case HANDLE_N:
		return HANDLE_S;
	case HANDLE_NE:
		return HANDLE_SE;
	case HANDLE_SE:
		return HANDLE_NE;
	case HANDLE_S:
		return HANDLE_N;
	case HANDLE_SW:
		return HANDLE_NW;
	default:
		return h;
	}
}

void region_clamp_move(struct ro_state *st) {
	if (st->bounds.w <= 0 || st->bounds.h <= 0) return;
	if (st->sel_x < st->bounds.x) st->sel_x = st->bounds.x;
	if (st->sel_y < st->bounds.y) st->sel_y = st->bounds.y;
	if (st->sel_x + st->sel_w > st->bounds.x + st->bounds.w)
		st->sel_x = st->bounds.x + st->bounds.w - st->sel_w;
	if (st->sel_y + st->sel_h > st->bounds.y + st->bounds.h)
		st->sel_y = st->bounds.y + st->bounds.h - st->sel_h;
}

void region_apply_handle_drag(struct ro_state *st) {
	int32_t l = st->sel_x, r = st->sel_x + st->sel_w;
	int32_t t = st->sel_y, b = st->sel_y + st->sel_h;
	int32_t cx = st->cursor_x, cy = st->cursor_y;
	if (st->bounds.w > 0 && st->bounds.h > 0) {
		if (cx < st->bounds.x) cx = st->bounds.x;
		if (cy < st->bounds.y) cy = st->bounds.y;
		if (cx > st->bounds.x + st->bounds.w) cx = st->bounds.x + st->bounds.w;
		if (cy > st->bounds.y + st->bounds.h) cy = st->bounds.y + st->bounds.h;
	}
	switch (st->handle_dragging) {
	case HANDLE_NW:
		l = cx;
		t = cy;
		break;
	case HANDLE_N:
		t = cy;
		break;
	case HANDLE_NE:
		r = cx;
		t = cy;
		break;
	case HANDLE_E:
		r = cx;
		break;
	case HANDLE_SE:
		r = cx;
		b = cy;
		break;
	case HANDLE_S:
		b = cy;
		break;
	case HANDLE_SW:
		l = cx;
		b = cy;
		break;
	case HANDLE_W:
		l = cx;
		break;
	default:
		return;
	}
	if (l > r) {
		int32_t tmp = l;
		l = r;
		r = tmp;
		st->handle_dragging = flip_handle_x(st->handle_dragging);
	}
	if (t > b) {
		int32_t tmp = t;
		t = b;
		b = tmp;
		st->handle_dragging = flip_handle_y(st->handle_dragging);
	}
	st->sel_x = l;
	st->sel_y = t;
	st->sel_w = r - l;
	st->sel_h = b - t;
}
