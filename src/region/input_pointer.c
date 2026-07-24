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

static void pointer_enter(void *data, struct wl_pointer *p, uint32_t serial,
						  struct wl_surface *surface, wl_fixed_t sx, wl_fixed_t sy) {
	struct ro_state *st = data;
	if (st->cleanup) return;
	struct ro_output *o = region_render_find_by_surface(st, surface);
	if (!o) return;
	st->cursor_on = o;
	st->cursor_x = o->go->x + wl_fixed_to_int(sx);
	st->cursor_y = o->go->y + wl_fixed_to_int(sy);
	st->last_cursor_serial = serial;
	ginp_apply_cursor(st, p, serial, o, ginp_pick_cursor(st, st->cursor_x, st->cursor_y));
}

static void pointer_leave(void *data, struct wl_pointer *p, uint32_t serial,
						  struct wl_surface *surface) {
	(void)p;
	(void)serial;
	(void)surface;
	struct ro_state *st = data;
	if (st->cleanup) return;
	st->cursor_on = NULL;
	if (region_set_hover(st, -1)) region_render_request_redraw_all(st);
}

static void pointer_motion(void *data, struct wl_pointer *p, uint32_t time,
						   wl_fixed_t sx, wl_fixed_t sy) {
	(void)p;
	(void)time;
	struct ro_state *st = data;
	if (st->cleanup) return;
	if (!st->cursor_on) return;
	st->cursor_x = st->cursor_on->go->x + wl_fixed_to_int(sx);
	st->cursor_y = st->cursor_on->go->y + wl_fixed_to_int(sy);

	if (st->tb_dragging) {
		if (!st->tb_lock) st->tb_out = st->cursor_on->go;
		st->tb_x = st->cursor_x - st->tb_grab_dx;
		st->tb_y = st->cursor_y - st->tb_grab_dy;
		st->tb_moved = true;
	} else if (st->slider_dragging) {
		ginp_slider_set_width_from_cursor(st);
	} else if (st->color_picker_dragging) {
		uint32_t picked = 0;
		if (region_color_picker_pick(st, st->cursor_x, st->cursor_y, &picked)) {
			st->current_color = picked;
			st->edit_choices_dirty = true;
		}
	} else if (st->region_locked) {
		if (st->moving_region) {
			int32_t px = st->sel_x, py = st->sel_y;
			st->sel_x = st->cursor_x - st->move_grab_dx;
			st->sel_y = st->cursor_y - st->move_grab_dy;
			region_clamp_move(st);
			if (st->sel_x != px || st->sel_y != py) st->region_moved = true;
		} else if (st->handle_dragging != HANDLE_NONE) {
			region_apply_handle_drag(st);
		} else if (st->anno_drag == ANNO_DRAG_MOVE) {
			int32_t dx = st->cursor_x - st->anno_last_x;
			int32_t dy = st->cursor_y - st->anno_last_y;
			if (st->out_annos)
				for (size_t i = 0; i < st->out_annos->n; i++)
					if (st->out_annos->items[i].selected)
						annotation_translate(&st->out_annos->items[i], dx, dy);
			st->anno_last_x = st->cursor_x;
			st->anno_last_y = st->cursor_y;
		} else if (st->anno_drag >= 0) {
			struct annotation *a = region_anno_selected(st);
			if (a) {
				if (st->anno_drag & 1)
					a->x1 = st->cursor_x;
				else
					a->x0 = st->cursor_x;
				if (st->anno_drag & 2)
					a->y1 = st->cursor_y;
				else
					a->y0 = st->cursor_y;
				annotation_update_bbox(a);
			}
		} else if (st->drawing && tool_uses_points(st->current_tool)) {
			region_pen_append(st, st->cursor_x, st->cursor_y);
		}
	} else {
		if (st->dragging) region_update_selection(st);
		int32_t h = st->dragging ? -1 : region_snap_hit(st, st->cursor_x, st->cursor_y);
		if (h != st->snap_hover) st->snap_hover = h;
	}

	int hover = -1;
	if (ginp_toolbar_reachable(st)) {
		enum tb_action a = region_toolbar_hit(st, st->cursor_x, st->cursor_y);
		if (a != TB_NONE) hover = (int)a;
	}
	region_set_hover(st, hover);

	ginp_refresh_cursor(st, p);
	region_render_request_redraw_all(st);
}

static void pointer_axis(void *data, struct wl_pointer *p, uint32_t time,
						 uint32_t axis, wl_fixed_t value) {
	(void)p;
	(void)time;
	struct ro_state *st = data;
	if (st->cleanup) return;
	if (axis != WL_POINTER_AXIS_VERTICAL_SCROLL) return;
	if (!region_editing(st)) return;
	st->scroll_accum += wl_fixed_to_double(value);
	int32_t n = (int32_t)(st->scroll_accum / 10.0);
	if (n == 0) return;
	st->scroll_accum -= n * 10.0;

	if (st->anno_edit_mode && region_has_selection(st) && st->out_annos) {
		if (!st->resizing_anno) {
			region_undo_group_begin(st);
			for (size_t i = 0; i < st->out_annos->n; i++)
				if (st->out_annos->items[i].selected)
					region_undo_record_anno_size(st, i);
			region_undo_group_end(st);
			st->resizing_anno = true;
		}
		for (size_t i = 0; i < st->out_annos->n; i++) {
			struct annotation *a = &st->out_annos->items[i];
			if (!a->selected) continue;
			if (a->tool == TOOL_TEXT || a->tool == TOOL_COUNTER) {
				int32_t fv = a->font_size - n * 2;
				a->font_size = fv < FONT_MIN ? FONT_MIN : (fv > FONT_MAX ? FONT_MAX : fv);
			} else {
				int32_t wv = a->width - n;
				a->width = wv < WIDTH_MIN ? WIDTH_MIN : (wv > WIDTH_MAX ? WIDTH_MAX : wv);
			}
			annotation_update_bbox(a);
		}
		st->out_annos->gen++;
		region_render_request_redraw_all(st);
		return;
	}

	int32_t lo, hi;
	int32_t *f = region_slider_field(st, &lo, &hi);
	int32_t step = region_tool_uses_font(st) ? 2 : 1;
	int32_t v = *f - n * step;
	if (v < lo) v = lo;
	if (v > hi) v = hi;
	if (v == *f) return;
	*f = v;
	if (!region_tool_uses_font(st)) st->edit_choices_dirty = true;
	region_render_request_redraw_all(st);
}

const struct wl_pointer_listener ginp_pointer_listener_g = {
	.enter = pointer_enter,
	.leave = pointer_leave,
	.motion = pointer_motion,
	.button = ginp_pointer_button,
	.axis = pointer_axis,
};
