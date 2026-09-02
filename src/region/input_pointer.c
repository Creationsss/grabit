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

static bool enter_output(struct ro_state *st, struct wl_surface *surface,
						 wl_fixed_t sx, wl_fixed_t sy) {
	struct ro_output *o = region_render_find_by_surface(st, surface);
	if (!o) return false;
	st->cursor_on = o;
	st->cursor_x = o->go->x + wl_fixed_to_int(sx);
	st->cursor_y = o->go->y + wl_fixed_to_int(sy);
	return true;
}

static void pointer_enter(void *data, struct wl_pointer *p, uint32_t serial,
						  struct wl_surface *surface, wl_fixed_t sx, wl_fixed_t sy) {
	(void)p;
	struct ro_state *st = data;
	if (st->cleanup) return;
	if (!enter_output(st, surface, sx, sy)) return;
	st->last_cursor_serial = serial;
	st->current_cursor_kind = ginp_pick_cursor(st, st->cursor_x, st->cursor_y);
	ginp_apply_cursor(st, serial, st->current_cursor_kind);
}

static void pointer_leave(void *data, struct wl_pointer *p, uint32_t serial,
						  struct wl_surface *surface) {
	(void)p;
	(void)serial;
	(void)surface;
	struct ro_state *st = data;
	if (st->cleanup) return;
	if (st->dragging || st->tb_dragging || st->slider_dragging ||
		st->color_picker_dragging || st->moving_region ||
		st->handle_dragging != HANDLE_NONE || region_anno_dragging(st) ||
		st->drawing)
		return;
	st->cursor_on = NULL;
	if (region_set_hover(st, -1)) region_render_request_redraw_all(st);
}

static void motion_event(struct ro_state *st, wl_fixed_t sx, wl_fixed_t sy) {
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
		ginp_slider_set_width_from_cursor(st, false);
	} else if (st->color_picker_dragging) {
		uint32_t picked = 0;
		if (region_color_picker_pick(st, st->cursor_x, st->cursor_y, &picked))
			region_apply_color(st, picked, false);
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
		region_snap_set_hover(st, h);
	}

	int hover = -1;
	if (ginp_toolbar_reachable(st)) {
		enum tb_action a = region_toolbar_hit(st, st->cursor_x, st->cursor_y);
		if (a != TB_NONE) hover = (int)a;
	}
	region_set_hover(st, hover);

	ginp_refresh_cursor(st);
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
			region_undo_record_selected_sizes(st, -1);
			st->resizing_anno = true;
		}
		for (size_t i = 0; i < st->out_annos->n; i++) {
			struct annotation *a = &st->out_annos->items[i];
			if (!a->selected) continue;
			if (tool_uses_font(a->tool)) {
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
	int32_t cur = region_active_slider(st, &lo, &hi);
	int32_t step = region_tool_uses_font(st) ? 2 : 1;
	int32_t v = cur - n * step;
	if (v < lo) v = lo;
	if (v > hi) v = hi;
	if (v == cur) return;
	region_apply_slider(st, v, false);
	region_render_request_redraw_all(st);
}

static void pointer_motion(void *data, struct wl_pointer *p, uint32_t time,
						   wl_fixed_t sx, wl_fixed_t sy) {
	(void)p;
	(void)time;
	motion_event(data, sx, sy);
}

static void pointer_button(void *data, struct wl_pointer *p, uint32_t serial,
						   uint32_t time, uint32_t button, uint32_t state) {
	(void)p;
	(void)serial;
	ginp_button_event(data, time, button, state);
}

static void touch_down(void *data, struct wl_touch *t, uint32_t serial, uint32_t time,
					   struct wl_surface *surface, int32_t id, wl_fixed_t sx,
					   wl_fixed_t sy) {
	(void)t;
	(void)serial;
	struct ro_state *st = data;
	if (st->cleanup || st->touch_id != -1) return;
	if (!enter_output(st, surface, sx, sy)) return;
	st->touch_id = id;
	ginp_button_event(st, time, BTN_LEFT, WL_POINTER_BUTTON_STATE_PRESSED);
}

static void touch_up(void *data, struct wl_touch *t, uint32_t serial, uint32_t time,
					 int32_t id) {
	(void)t;
	(void)serial;
	struct ro_state *st = data;
	if (st->cleanup || st->touch_id != id) return;
	st->touch_id = -1;
	ginp_button_event(st, time, BTN_LEFT, WL_POINTER_BUTTON_STATE_RELEASED);
}

static void touch_motion(void *data, struct wl_touch *t, uint32_t time, int32_t id,
						 wl_fixed_t sx, wl_fixed_t sy) {
	(void)t;
	(void)time;
	struct ro_state *st = data;
	if (st->touch_id != id) return;
	motion_event(st, sx, sy);
}

static void touch_frame(void *data, struct wl_touch *t) {
	(void)data;
	(void)t;
}

static void touch_cancel(void *data, struct wl_touch *t) {
	(void)t;
	struct ro_state *st = data;
	if (st->cleanup || st->touch_id == -1) return;
	st->touch_id = -1;
	ginp_region_abort_active(st);
}

const struct wl_touch_listener ginp_touch_listener_g = {
	.down = touch_down,
	.up = touch_up,
	.motion = touch_motion,
	.frame = touch_frame,
	.cancel = touch_cancel,
};

const struct wl_pointer_listener ginp_pointer_listener_g = {
	.enter = pointer_enter,
	.leave = pointer_leave,
	.motion = pointer_motion,
	.button = pointer_button,
	.axis = pointer_axis,
};
