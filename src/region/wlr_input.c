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

#include "cursor-shape-v1-client-protocol.h"

#define DOUBLE_CLICK_MS 400

#include "region/input_internal.h"

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
	region_clear_selection(st);
	st->color_picker_open = false;
	st->picker_group = TB_NONE;
	st->eyedropper_mode = false;
}

void ginp_mode_enter_anno_edit(struct ro_state *st) {
	st->region_locked = true;
	st->anno_edit_mode = true;
	st->color_picker_open = false;
	st->picker_group = TB_NONE;
	st->eyedropper_mode = false;
}

void ginp_mode_select_tool(struct ro_state *st, enum tool_kind t) {
	st->current_tool = t;
	const struct tool_group *g = toolbar_group_of_tool(t);
	if (g) st->group_tool[toolbar_group_index(g)] = t;
	st->region_locked = true;
	st->anno_edit_mode = false;
	st->picker_group = TB_NONE;
	region_clear_selection(st);
	st->edit_choices_dirty = true;
}

int ginp_pick_cursor(const struct ro_state *st, int32_t abs_x, int32_t abs_y) {
	if (st->tb_dragging) return RCUR_MOVE;
	if (st->picker_group != TB_NONE) {
		int val;
		if (region_tool_picker_hit(st, abs_x, abs_y, &val) != TP_NONE)
			return RCUR_HAND;
	}
	if (ginp_toolbar_reachable(st) && region_toolbar_contains(st, abs_x, abs_y)) {
		enum tb_action a = region_toolbar_hit(st, abs_x, abs_y);
		return a != TB_NONE ? RCUR_HAND : RCUR_MOVE;
	}
	if (st->region_locked) {
		if (st->moving_region) return RCUR_MOVE;
		if (st->handle_dragging >= 0 && st->handle_dragging < 8)
			return RCUR_RESIZE0 + st->handle_dragging;
		if (st->eyedropper_mode) return RCUR_CROSS;
		int h = region_handle_at(st, abs_x, abs_y);
		if (h != HANDLE_NONE) return RCUR_RESIZE0 + h;
		if (st->anno_edit_mode) {
			bool grab = region_anno_dragging(st) ||
						ginp_anno_corner_at(st, abs_x, abs_y) >= 0 ||
						ginp_anno_hit_index(st, abs_x, abs_y) >= 0;
			return grab ? RCUR_MOVE : RCUR_DEFAULT;
		}
		if ((st->ctrl_held || !region_editing(st)) &&
			region_inside_selection(st, abs_x, abs_y))
			return RCUR_MOVE;
		if (!region_editing(st)) return RCUR_CROSS;
		if (st->current_tool == TOOL_TEXT) return RCUR_TEXT;
		return RCUR_DEFAULT;
	}
	return RCUR_CROSS;
}

static struct wl_cursor *kind_cursor(const struct ro_state *st, int kind) {
	struct wl_cursor *c = NULL;
	switch (kind) {
	case RCUR_CROSS:
		return st->cursor;
	case RCUR_TEXT:
		c = st->cursor_text;
		break;
	case RCUR_MOVE:
		c = st->cursor_move;
		break;
	case RCUR_HAND:
		c = st->cursor_hand;
		break;
	default:
		c = st->cursor_resize[kind - RCUR_RESIZE0];
		break;
	}
	if (!c) c = st->cursor_default;
	return c ? c : st->cursor;
}

void ginp_refresh_cursor(struct ro_state *st, struct wl_pointer *p) {
	if (!st->cursor_on) return;
	int want = ginp_pick_cursor(st, st->cursor_x, st->cursor_y);
	if (want == st->current_cursor_kind) return;
	st->current_cursor_kind = want;
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
					   struct ro_output *o, int kind) {
	if (st->cursor_shape) {
		static const uint32_t shapes[] = {
			[RCUR_CROSS] = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_CROSSHAIR,
			[RCUR_TEXT] = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_TEXT,
			[RCUR_DEFAULT] = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT,
			[RCUR_MOVE] = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_MOVE,
			[RCUR_HAND] = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER,
			[RCUR_RESIZE0 + 0] = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NW_RESIZE,
			[RCUR_RESIZE0 + 1] = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_N_RESIZE,
			[RCUR_RESIZE0 + 2] = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NE_RESIZE,
			[RCUR_RESIZE0 + 3] = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_E_RESIZE,
			[RCUR_RESIZE0 + 4] = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_SE_RESIZE,
			[RCUR_RESIZE0 + 5] = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_S_RESIZE,
			[RCUR_RESIZE0 + 6] = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_SW_RESIZE,
			[RCUR_RESIZE0 + 7] = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_W_RESIZE,
		};
		wp_cursor_shape_device_v1_set_shape(st->cursor_shape, serial, shapes[kind]);
		return;
	}
	grabit_cursor_apply(p, serial, st->cursor_surface, kind_cursor(st, kind),
						o->scale);
}

void ginp_slider_set_width_from_cursor(struct ro_state *st) {
	int32_t sx, sy, sw, sh;
	region_toolbar_slider_rect(st, &sx, &sy, &sw, &sh);
	(void)sy;
	(void)sh;
	double frac = sw > 0 ? (double)(st->cursor_x - sx) / (double)sw : 0;
	if (frac < 0) frac = 0;
	if (frac > 1) frac = 1;
	int32_t lo, hi;
	int32_t *f = region_slider_field(st, &lo, &hi);
	*f = lo + (int32_t)(frac * (hi - lo) + 0.5);
}
