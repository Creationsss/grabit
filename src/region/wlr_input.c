// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "region/wlr_state.h"

#include "capture/capture.h"
#include "cursor.h"
#include "region/annotate.h"
#include "region/toolbar_internal.h"
#include "region/wlr_input_state.h"
#include "wl/wl.h"

#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include <linux/input-event-codes.h>

#include <wayland-client.h>
#include <wayland-cursor.h>
#include <xkbcommon/xkbcommon.h>

#define DOUBLE_CLICK_MS 400

#include "region/input_internal.h"

void ginp_apply_cursor(struct ro_state *st, struct wl_pointer *p, uint32_t serial,
					   struct ro_output *o, struct wl_cursor *c);

bool ginp_eyedropper_sample(struct ro_state *st, uint32_t *out_color) {
	if (!st->cursor_on || !st->frozen) return false;
	struct ro_output *ro = st->cursor_on;
	const struct image *img = &st->frozen[ro->idx];
	if (!img->bytes || img->stride <= 0) return false;
	int32_t scale = ro->go->scale > 0 ? ro->go->scale : 1;
	int32_t px = (st->cursor_x - ro->go->x) * scale;
	int32_t py = (st->cursor_y - ro->go->y) * scale;
	if (px < 0 || py < 0 || px >= img->width || py >= img->height) return false;
	const uint8_t *row = (const uint8_t *)img->bytes + (size_t)py * (size_t)img->stride;
	uint32_t pixel = ((const uint32_t *)row)[px];
	*out_color = pixel & 0xFFFFFFu;
	return true;
}

int ginp_anno_hit_index(const struct ro_state *st, int32_t x, int32_t y) {
	if (!st->out_annos) return -1;
	for (size_t i = st->out_annos->n; i > 0; i--) {
		if (annotation_hit(&st->out_annos->items[i - 1], x, y))
			return (int)(i - 1);
	}
	return -1;
}

int ginp_anno_corner_at(const struct ro_state *st, int32_t x, int32_t y) {
	const struct annotation *a = region_anno_selected(st);
	if (!a) return -1;
	int mask = annotation_corner_mask(a);
	for (int c = 0; c < 4; c++) {
		if (!(mask & (1 << c))) continue;
		int32_t dx = x - annotation_corner_x(a, c);
		int32_t dy = y - annotation_corner_y(a, c);
		if (dx * dx + dy * dy <= HANDLE_RADIUS * HANDLE_RADIUS) return c;
	}
	return -1;
}

bool ginp_toolbar_reachable(const struct ro_state *st) {
	return region_editing(st) && !st->dragging && !st->tb_dragging &&
		   !st->drawing && !st->moving_region && !st->slider_dragging &&
		   !region_anno_dragging(st) &&
		   !st->text_input_active && st->handle_dragging == HANDLE_NONE;
}

void ginp_mode_enter_region(struct ro_state *st) {
	st->region_locked = false;
	st->anno_edit_mode = false;
	st->sel_anno = -1;
	st->color_picker_open = false;
	st->eyedropper_mode = false;
}

void ginp_mode_enter_anno_edit(struct ro_state *st) {
	st->region_locked = true;
	st->anno_edit_mode = true;
	st->color_picker_open = false;
	st->eyedropper_mode = false;
}

void ginp_mode_select_tool(struct ro_state *st, enum tool_kind t) {
	st->current_tool = t;
	st->region_locked = true;
	st->anno_edit_mode = false;
	st->sel_anno = -1;
	st->edit_choices_dirty = true;
}

struct wl_cursor *ginp_pick_cursor(const struct ro_state *st, int32_t abs_x, int32_t abs_y) {
	if (st->tb_dragging && st->cursor_move) return st->cursor_move;
	if (ginp_toolbar_reachable(st) && region_toolbar_contains(st, abs_x, abs_y)) {
		enum tb_action a = region_toolbar_hit(st, abs_x, abs_y);
		if (a != TB_NONE && st->cursor_hand) return st->cursor_hand;
		if (st->cursor_move) return st->cursor_move;
		if (st->cursor_default) return st->cursor_default;
	}
	if (st->region_locked) {
		if (st->moving_region && st->cursor_move) return st->cursor_move;
		if (st->handle_dragging >= 0 && st->handle_dragging < 8 &&
			st->cursor_resize[st->handle_dragging])
			return st->cursor_resize[st->handle_dragging];
		if (st->eyedropper_mode) return st->cursor;
		int h = region_handle_at(st, abs_x, abs_y);
		if (h != HANDLE_NONE && st->cursor_resize[h]) return st->cursor_resize[h];
		if (h != HANDLE_NONE && st->cursor_default) return st->cursor_default;
		if (st->anno_edit_mode) {
			bool grab = region_anno_dragging(st) ||
						ginp_anno_corner_at(st, abs_x, abs_y) >= 0 ||
						ginp_anno_hit_index(st, abs_x, abs_y) >= 0;
			if (grab && st->cursor_move) return st->cursor_move;
			return st->cursor_default ? st->cursor_default : st->cursor;
		}
		if ((st->ctrl_held || !region_editing(st)) &&
			region_inside_selection(st, abs_x, abs_y) && st->cursor_move)
			return st->cursor_move;
		if (!region_editing(st)) return st->cursor;
		if (st->current_tool == TOOL_TEXT && st->cursor_text) return st->cursor_text;
		return st->cursor_default ? st->cursor_default : st->cursor;
	}
	return st->cursor;
}

void ginp_refresh_cursor(struct ro_state *st, struct wl_pointer *p) {
	if (!st->cursor_on) return;
	struct wl_cursor *want = ginp_pick_cursor(st, st->cursor_x, st->cursor_y);
	if (want == st->current_cursor) return;
	st->current_cursor = want;
	if (st->last_cursor_serial == 0) return;
	ginp_apply_cursor(st, p, st->last_cursor_serial, st->cursor_on, want);
}

void ginp_lock_or_finish(struct ro_state *st) {
	bool keep = st->confirm_mode ||
				(region_editing(st) && !st->edit_instant);
	if (keep) {
		st->region_locked = true;
		if (st->pointer) ginp_refresh_cursor(st, st->pointer);
	} else {
		st->finished = true;
	}
}

void ginp_apply_cursor(struct ro_state *st, struct wl_pointer *p, uint32_t serial,
					   struct ro_output *o, struct wl_cursor *c) {
	grabit_cursor_apply(p, serial, st->cursor_surface, c, o->scale);
}

void ginp_slider_set_width_from_cursor(struct ro_state *st) {
	int32_t sx, sy, sw, sh;
	region_toolbar_slider_rect(st, &sx, &sy, &sw, &sh);
	(void)sy;
	(void)sh;
	double frac = sw > 0 ? (double)(st->cursor_x - sx) / (double)sw : 0;
	if (frac < 0) frac = 0;
	if (frac > 1) frac = 1;
	st->current_width = WIDTH_MIN + (int32_t)(frac * (WIDTH_MAX - WIDTH_MIN) + 0.5);
}
